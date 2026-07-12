package systems

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/pocketbase/dbx"
	"github.com/pocketbase/pocketbase/core"
)

// RecoveryProber manages fast, low-downtime watchdogs for systems mapped to watchdog modules.
type RecoveryProber struct {
	mu        sync.RWMutex
	app       core.App
	watchdogs map[string]*systemWatchdog
	running   bool
}

// systemWatchdog stores in-memory state and the cancellation hook for a single system watchdog loop.
type systemWatchdog struct {
	systemID    string
	channelID   string
	hostIP      string
	probePorts  []int
	threshold   int
	graceSecs   int
	maintenance bool
	wolEnabled  bool
	autoWol     bool
	macAddress  string
	bcastIP     string
	wolPort     int
	cancel      context.CancelFunc
}

// NewRecoveryProber instantiates a new watchdog manager.
func NewRecoveryProber(app core.App) *RecoveryProber {
	return &RecoveryProber{
		app:       app,
		watchdogs: make(map[string]*systemWatchdog),
	}
}

// Start loads current config and binds pocketbase hooks to maintain the watchdog registry.
func (rp *RecoveryProber) Start() error {
	rp.mu.Lock()
	if rp.running {
		rp.mu.Unlock()
		return nil
	}
	rp.running = true
	rp.mu.Unlock()

	// Load existing mappings
	records, err := rp.app.FindRecordsByFilter("recovery_channels", "", "", -1, 0)
	if err == nil {
		for _, rec := range records {
			rp.registerWatchdog(rec)
		}
	}

	// Register collection lifecycle hooks
	rp.app.OnRecordAfterCreateSuccess("recovery_channels").BindFunc(func(e *core.RecordEvent) error {
		rp.registerWatchdog(e.Record)
		return e.Next()
	})

	rp.app.OnRecordAfterUpdateSuccess("recovery_channels").BindFunc(func(e *core.RecordEvent) error {
		rp.registerWatchdog(e.Record)
		return e.Next()
	})

	rp.app.OnRecordAfterDeleteSuccess("recovery_channels").BindFunc(func(e *core.RecordEvent) error {
		rp.deregisterWatchdog(e.Record.Id)
		return e.Next()
	})

	return nil
}

// registerWatchdog starts or updates a background watcher goroutine for a configuration mapping.
func (rp *RecoveryProber) registerWatchdog(rec *core.Record) {
	rp.mu.Lock()
	defer rp.mu.Unlock()

	channelID := rec.Id
	systemID := rec.GetString("system")
	hostIP := rec.GetString("host_ip")
	threshold := rec.GetInt("failure_threshold")
	graceSecs := rec.GetInt("boot_grace_seconds")
	maintenance := rec.GetBool("maintenance")
	wolEnabled := rec.GetBool("wol_enabled")
	autoWol := rec.GetBool("auto_wol")
	macAddress := rec.GetString("mac_address")
	bcastIP := rec.GetString("broadcast_address")
	wolPort := rec.GetInt("wol_port")

	if threshold <= 0 {
		threshold = 3
	}
	if graceSecs <= 0 {
		graceSecs = 60
	}
	if wolPort <= 0 {
		wolPort = 9
	}
	if bcastIP == "" {
		bcastIP = "255.255.255.255"
	}

	var probePorts []int
	portsData := rec.GetString("probe_ports")
	if portsData != "" {
		_ = json.Unmarshal([]byte(portsData), &probePorts)
	}
	if len(probePorts) == 0 {
		probePorts = []int{22} // Fallback to SSH
	}

	if old, exists := rp.watchdogs[channelID]; exists {
		old.cancel()
		delete(rp.watchdogs, channelID)
	}

	ctx, cancel := context.WithCancel(context.Background())
	w := &systemWatchdog{
		systemID:    systemID,
		channelID:   channelID,
		hostIP:      hostIP,
		probePorts:  probePorts,
		threshold:   threshold,
		graceSecs:   graceSecs,
		maintenance: maintenance,
		wolEnabled:  wolEnabled,
		autoWol:     autoWol,
		macAddress:  macAddress,
		bcastIP:     bcastIP,
		wolPort:     wolPort,
		cancel:      cancel,
	}

	rp.watchdogs[channelID] = w

	go rp.runWatchdog(ctx, w)
}

// deregisterWatchdog cancels the context of a watchdog loop, stopping the checking loop.
func (rp *RecoveryProber) deregisterWatchdog(channelID string) {
	rp.mu.Lock()
	defer rp.mu.Unlock()

	if w, exists := rp.watchdogs[channelID]; exists {
		w.cancel()
		delete(rp.watchdogs, channelID)
	}
}

// runWatchdog runs the checking state machine loop.
func (rp *RecoveryProber) runWatchdog(ctx context.Context, w *systemWatchdog) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if w.maintenance {
				continue
			}

			// Perform normal 5s check
			online := rp.probePorts(w.hostIP, w.probePorts)
			if !online {
				// Transition to FAST_VERIFY state
				rp.logEvent(w.systemID, w.channelID, "FAST_VERIFY_STARTED", map[string]any{
					"reason": "first normal probe failed",
				})

				success := false
				// Perform verification attempts (2 seconds apart)
				for i := 0; i < w.threshold; i++ {
					select {
					case <-ctx.Done():
						return
					case <-time.After(2 * time.Second):
						if rp.probePorts(w.hostIP, w.probePorts) {
							success = true
							break
						}
					}
					if success {
						break
					}
				}

				if success {
					rp.logEvent(w.systemID, w.channelID, "FAST_VERIFY_RECOVERED", map[string]any{
						"status": "online",
					})
					continue
				}

				// All verify checks failed. Verify network gateway status.
				gatewayIP := rp.getGatewayIP(w.systemID)
				if gatewayIP != "" && !rp.probeGateway(gatewayIP) {
					// Gateway itself is down. Classify as network issue.
					rp.logEvent(w.systemID, w.channelID, "NETWORK_FAILURE", map[string]any{
						"gateway": gatewayIP,
						"reason":  "gateway port probe failed",
					})
					continue
				}

				// Gateway is online. Server down is officially classified.
				rp.logEvent(w.systemID, w.channelID, "FAILURE_CONFIRMED", map[string]any{
					"reason": fmt.Sprintf("failed %d verify checks", w.threshold),
				})

				// If WOL is enabled and automatic, trigger it
				if w.wolEnabled && w.autoWol && w.macAddress != "" {
					rp.logEvent(w.systemID, w.channelID, "WOL_SENT", map[string]any{
						"mac":       w.macAddress,
						"broadcast": w.bcastIP,
						"port":      w.wolPort,
					})

					err := SendMagicPacket(w.macAddress, w.bcastIP, w.wolPort)
					if err != nil {
						rp.logEvent(w.systemID, w.channelID, "WOL_ERROR", map[string]any{
							"error": err.Error(),
						})
					}

					// Wait for boot grace period
					bootSuccess := false
					graceTicker := time.NewTicker(1 * time.Second)
					graceTimeout := time.After(time.Duration(w.graceSecs) * time.Second)

					for bootSuccess == false {
						select {
						case <-ctx.Done():
							graceTicker.Stop()
							return
						case <-graceTimeout:
							graceTicker.Stop()
							bootSuccess = false
							goto graceEnd
						case <-graceTicker.C:
							if rp.probePorts(w.hostIP, w.probePorts) {
								graceTicker.Stop()
								bootSuccess = true
								goto graceEnd
							}
						}
					}

				graceEnd:
					if bootSuccess {
						rp.logEvent(w.systemID, w.channelID, "WOL_SUCCESS", map[string]any{
							"status": "online",
						})
					} else {
						rp.logEvent(w.systemID, w.channelID, "WOL_FAILED", map[string]any{
							"reason": "boot grace period timed out",
						})
					}
				}
			}
		}
	}
}

// probePorts dials TCP ports to check if host is responsive.
func (rp *RecoveryProber) probePorts(host string, ports []int) bool {
	for _, port := range ports {
		address := net.JoinHostPort(host, fmt.Sprintf("%d", port))
		conn, err := net.DialTimeout("tcp", address, 2*time.Second)
		if err == nil {
			conn.Close()
			return true
		}
	}
	return false
}

// probeGateway dials DNS port 53 or HTTP port 80 to verify gateway connectivity.
func (rp *RecoveryProber) probeGateway(gatewayIP string) bool {
	address := net.JoinHostPort(gatewayIP, "53")
	conn, err := net.DialTimeout("tcp", address, 1500*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	address80 := net.JoinHostPort(gatewayIP, "80")
	conn80, err := net.DialTimeout("tcp", address80, 1500*time.Millisecond)
	if err == nil {
		conn80.Close()
		return true
	}
	return false
}

// getGatewayIP queries database configuration to retrieve the gateway IP for a system ID.
func (rp *RecoveryProber) getGatewayIP(systemID string) string {
	chanRec, err := rp.app.FindFirstRecordByFilter("recovery_channels", "system = {:system}", dbx.Params{"system": systemID})
	if err != nil {
		return ""
	}
	moduleID := chanRec.GetString("module")
	if moduleID == "" {
		return ""
	}
	moduleRec, err := rp.app.FindRecordById("recovery_modules", moduleID)
	if err != nil {
		return ""
	}
	return moduleRec.GetString("gateway_ip")
}

// logEvent creates an audit trail entry in recovery_events.
func (rp *RecoveryProber) logEvent(systemID, channelID, event string, metadata map[string]any) {
	collection, err := rp.app.FindCollectionByNameOrId("recovery_events")
	if err != nil {
		return
	}

	var moduleID string
	var channelNum int
	if channelID != "" {
		if rec, err := rp.app.FindRecordById("recovery_channels", channelID); err == nil {
			moduleID = rec.GetString("module")
			channelNum = rec.GetInt("channel_number")
		}
	}

	record := core.NewRecord(collection)
	record.Set("system", systemID)
	if moduleID != "" {
		record.Set("module", moduleID)
		record.Set("channel", channelNum)
	}
	record.Set("event", event)
	record.Set("timestamp", time.Now().UTC())

	metaJSON, _ := json.Marshal(metadata)
	record.Set("metadata", string(metaJSON))

	_ = rp.app.Save(record)
}
