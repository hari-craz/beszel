package systems

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/henrygd/beszel/internal/hub/expirymap"
	"github.com/pocketbase/dbx"
	"github.com/pocketbase/pocketbase/core"
)

// RecoveryProber manages fast, low-downtime watchdogs for systems mapped to watchdog modules.
type RecoveryProber struct {
	mu        sync.RWMutex
	app       core.App
	watchdogs map[string]*systemWatchdog
	running   bool
	ctx       context.Context
	cancel    context.CancelFunc

	// locks coordinates hub-side recovery actions (automatic WOL vs. manual
	// UI actions) so at most one is ever in flight per channel at a time.
	// It intentionally does not - and cannot - coordinate with the ESP32's
	// own autonomous relay actions in real time; see ChannelLockInfo, which
	// is surfaced to the ESP via /recovery/ping as a best-effort hint.
	locks     *expirymap.ExpiryMap[lockInfo]
	nextLease uint64
}

// lockInfo describes the current holder of a channel's recovery lock.
type lockInfo struct {
	Owner     string
	LeaseID   string
	ExpiresAt time.Time
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
	moduleID    string
	channelNum  int
	cancel      context.CancelFunc
}

// NewRecoveryProber instantiates a new watchdog manager.
func NewRecoveryProber(app core.App) *RecoveryProber {
	ctx, cancel := context.WithCancel(context.Background())
	return &RecoveryProber{
		app:       app,
		watchdogs: make(map[string]*systemWatchdog),
		ctx:       ctx,
		cancel:    cancel,
		locks:     expirymap.New[lockInfo](30 * time.Second),
	}
}

// acquireLock grants exclusive ownership of a channel's recovery lock to owner
// for ttl. Any existing unexpired lease - regardless of owner - blocks
// acquisition, so two hub-side actions (automatic or manual) can never run
// concurrently against the same channel, including a duplicate click of the
// same action. Returns the minted lease ID and true on success.
func (rp *RecoveryProber) acquireLock(channelID, owner string, ttl time.Duration) (string, bool) {
	if _, held := rp.locks.GetOk(channelID); held {
		return "", false
	}
	leaseID := strconv.FormatUint(atomic.AddUint64(&rp.nextLease, 1), 36)
	rp.locks.Set(channelID, lockInfo{Owner: owner, LeaseID: leaseID, ExpiresAt: time.Now().Add(ttl)}, ttl)
	return leaseID, true
}

// releaseLock clears channelID's lock only if it still matches leaseID. A
// stale release from a superseded lease (e.g. an automatic-WOL attempt that
// was cancelled after its config changed) is a safe no-op instead of
// deleting a different, newer lease acquired by another actor in the interim.
func (rp *RecoveryProber) releaseLock(channelID, leaseID string) {
	if cur, held := rp.locks.GetOk(channelID); held && cur.LeaseID == leaseID {
		rp.locks.Remove(channelID)
	}
}

// lockStatus is a read-only lookup of the current lock holder, if any.
func (rp *RecoveryProber) lockStatus(channelID string) (owner string, secondsRemaining int, held bool) {
	cur, ok := rp.locks.GetOk(channelID)
	if !ok {
		return "", 0, false
	}
	remaining := int(time.Until(cur.ExpiresAt).Seconds())
	if remaining < 0 {
		remaining = 0
	}
	return cur.Owner, remaining, true
}

// ChannelLockInfo exposes the current lock holder for a channel so it can be
// surfaced to the ESP32 module via /recovery/ping as a best-effort hint.
func (rp *RecoveryProber) ChannelLockInfo(channelID string) (owner string, secondsRemaining int, held bool) {
	return rp.lockStatus(channelID)
}

// AcquireLock is the exported entry point used by manual (admin-triggered)
// recovery actions in the API layer.
func (rp *RecoveryProber) AcquireLock(channelID, owner string, ttl time.Duration) (string, bool) {
	return rp.acquireLock(channelID, owner, ttl)
}

// ReleaseLock is the exported entry point used by manual (admin-triggered)
// recovery actions in the API layer.
func (rp *RecoveryProber) ReleaseLock(channelID, leaseID string) {
	rp.releaseLock(channelID, leaseID)
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

	// Start offline modules detection scanner
	go rp.runOfflineScanner(rp.ctx)

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
	moduleID := rec.GetString("module")
	channelNum := rec.GetInt("channel_number")

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
		moduleID:    moduleID,
		channelNum:  channelNum,
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

// Stop cancels all background work owned by the RecoveryProber: the
// offline-module scanner, the recovery-lock expiry map's cleaner goroutine,
// and every per-channel watchdog goroutine. Intended for test cleanup
// (mirrors AlertManager.Stop()) - production shutdown just exits the
// process, so this isn't wired into normal hub startup/shutdown. Safe to
// call multiple times.
func (rp *RecoveryProber) Stop() {
	rp.mu.Lock()
	defer rp.mu.Unlock()

	for channelID, w := range rp.watchdogs {
		w.cancel()
		delete(rp.watchdogs, channelID)
	}
	rp.locks.StopCleaner()
	rp.cancel()
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
				if gatewayIP != "" {
					gatewayOK := rp.probeGateway(gatewayIP)
					rp.updateGatewayOnlineStatus(w.systemID, gatewayOK)
					if !gatewayOK {
						// Gateway itself is down. Classify as network issue.
						rp.logEvent(w.systemID, w.channelID, "NETWORK_FAILURE", map[string]any{
							"gateway": gatewayIP,
							"reason":  "gateway port probe failed",
						})
						continue
					}
				}

				// Gateway is online. Server down is officially classified.
				rp.logEvent(w.systemID, w.channelID, "FAILURE_CONFIRMED", map[string]any{
					"reason": fmt.Sprintf("failed %d verify checks", w.threshold),
				})

				// If WOL is enabled and automatic, attempt it. attemptAutomaticWOL
				// is lock-protected so it can never run concurrently with a
				// manual WOL/relay action (or a duplicate automatic attempt)
				// on the same channel.
				recoverySuccessful := false
				if w.wolEnabled && w.autoWol && w.macAddress != "" {
					recoverySuccessful = rp.attemptAutomaticWOL(ctx, w)
				}

				// Beszel must not directly control a relay (see
				// BESZEL_ESP32_HARDWARE_RECOVERY_BRIEF.md §9 and the final
				// architecture note: "only the watchdog can verify and
				// authorize an automatic physical recovery"). If WOL did not
				// succeed (or was skipped), physical recovery is the ESP32
				// module's own job - it runs the same verify/escalate ladder
				// locally and independently, without needing (or waiting for)
				// an HTTP nudge from the hub. Log this so the recovery
				// timeline doesn't go silent at this point.
				if !recoverySuccessful && w.moduleID != "" {
					rp.logEvent(w.systemID, w.channelID, "ESP_AUTONOMOUS_EXPECTED", map[string]any{
						"module":  w.moduleID,
						"channel": w.channelNum,
						"reason":  "WOL did not succeed; physical recovery is owned by the ESP32 module's independent watchdog, not triggered automatically by the hub",
					})
				}
			}
			if ctx.Err() != nil {
				return
			}
		}
	}
}

// attemptAutomaticWOL sends a Wake-on-LAN magic packet for w and waits up to
// w.graceSecs for the host to respond. It acquires the channel's recovery
// lock first so it can never race a manual WOL/relay action (or a duplicate
// automatic attempt) on the same channel; if the lock is already held it
// logs WOL_BLOCKED_LOCK and returns false immediately instead of proceeding.
//
// The lease is released via defer, scoping its lifetime to this single
// attempt rather than the long-lived watchdog goroutine. If ctx is
// cancelled mid-wait (e.g. because the channel's config just changed and
// registerWatchdog cancelled this goroutine to start a fresh one), this
// function returns promptly - within the wait loop's ~1s tick granularity -
// and the deferred, lease-ID-checked release frees the lock immediately.
// That is what makes toggling wol_enabled/auto_wol off take effect right
// away, without needing a separate, racier force-release call elsewhere.
func (rp *RecoveryProber) attemptAutomaticWOL(ctx context.Context, w *systemWatchdog) bool {
	leaseID, acquired := rp.acquireLock(w.channelID, "BESZEL_WOL", time.Duration(w.graceSecs+10)*time.Second)
	if !acquired {
		rp.logEvent(w.systemID, w.channelID, "WOL_BLOCKED_LOCK", map[string]any{
			"reason": "another recovery action is already in progress for this channel",
		})
		return false
	}
	defer rp.releaseLock(w.channelID, leaseID)

	rp.logEvent(w.systemID, w.channelID, "WOL_SENT", map[string]any{
		"mac":       w.macAddress,
		"broadcast": w.bcastIP,
		"port":      w.wolPort,
	})

	if err := SendMagicPacket(w.macAddress, w.bcastIP, w.wolPort); err != nil {
		rp.logEvent(w.systemID, w.channelID, "WOL_ERROR", map[string]any{
			"error": err.Error(),
		})
	}

	// Wait for boot grace period
	graceTicker := time.NewTicker(1 * time.Second)
	defer graceTicker.Stop()
	graceTimeout := time.After(time.Duration(w.graceSecs) * time.Second)

	for {
		select {
		case <-ctx.Done():
			return false
		case <-graceTimeout:
			rp.logEvent(w.systemID, w.channelID, "WOL_FAILED", map[string]any{
				"reason": "boot grace period timed out",
			})
			return false
		case <-graceTicker.C:
			if rp.probePorts(w.hostIP, w.probePorts) {
				rp.logEvent(w.systemID, w.channelID, "WOL_SUCCESS", map[string]any{
					"status": "online",
				})
				return true
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

// updateGatewayOnlineStatus persists whether the gateway is currently
// reachable on the recovery_modules record mapped to systemID's channel, so
// the health score and frontend can show gateway status without a separate
// always-on polling loop - this only runs when a channel's fast-verify has
// already failed and gateway health needs checking anyway.
func (rp *RecoveryProber) updateGatewayOnlineStatus(systemID string, online bool) {
	chanRec, err := rp.app.FindFirstRecordByFilter("recovery_channels", "system = {:system}", dbx.Params{"system": systemID})
	if err != nil {
		return
	}
	moduleID := chanRec.GetString("module")
	if moduleID == "" {
		return
	}
	moduleRec, err := rp.app.FindRecordById("recovery_modules", moduleID)
	if err != nil {
		return
	}
	if moduleRec.GetBool("gateway_online") == online {
		return
	}
	moduleRec.Set("gateway_online", online)
	_ = rp.app.Save(moduleRec)
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

// runOfflineScanner runs a periodic check to transition recovery modules to offline if pings stop.
func (rp *RecoveryProber) runOfflineScanner(ctx context.Context) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			records, err := rp.app.FindRecordsByFilter("recovery_modules", "status != 'unapproved'", "", -1, 0)
			if err != nil {
				continue
			}
			now := time.Now().UTC()
			for _, rec := range records {
				lastHeartbeat := rec.GetDateTime("updated").Time()
				status := rec.GetString("status")
				if now.Sub(lastHeartbeat) > 90*time.Second {
					if status != "offline" {
						rec.Set("status", "offline")
						_ = rp.app.Save(rec)
						rp.logEvent("", "", "MODULE_OFFLINE", map[string]any{
							"module": rec.Id,
							"name":   rec.GetString("name"),
							"mac":    rec.GetString("mac_address"),
						})
					}
				} else {
					if status == "offline" || status == "" {
						rec.Set("status", "online")
						_ = rp.app.Save(rec)
						rp.logEvent("", "", "MODULE_ONLINE", map[string]any{
							"module": rec.Id,
							"name":   rec.GetString("name"),
							"mac":    rec.GetString("mac_address"),
						})
					}
				}
			}
		}
	}
}

// triggerESP32Relay makes a POST dispatch call to the ESP32 module relay API endpoint.
func (rp *RecoveryProber) triggerESP32Relay(espIP string, channelNum, pulseMs int) error {
	payload := map[string]any{
		"channel":           channelNum,
		"pulse_duration_ms": pulseMs,
	}
	bodyBytes, err := json.Marshal(payload)
	if err != nil {
		return err
	}
	url := fmt.Sprintf("http://%s/api/relay/trigger", espIP)
	req, err := http.NewRequest("POST", url, bytes.NewBuffer(bodyBytes))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	client := &http.Client{
		Timeout: 5 * time.Second,
	}
	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
	}
	return nil
}
