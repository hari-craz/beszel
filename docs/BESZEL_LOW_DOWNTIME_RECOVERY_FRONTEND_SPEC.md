# Low-Downtime Recovery Extension for Beszel

> **Document type:** Implementation specification
> **Target:** Existing Beszel Hub and existing Beszel frontend
> **Hardware extension:** Optional ESP32 Recovery Modules
> **Primary objective:** Start the correct recovery action within 15 seconds of a confirmed server failure where network conditions allow.
> **Important:** This is an extension to Beszel. Do not build a separate dashboard or replacement frontend.

---

## 1. Purpose

This document defines the low-downtime recovery architecture for integrating optional ESP32 hardware recovery modules into Beszel.

Beszel remains the primary server monitoring platform and user interface.

The extension adds:

- Fast failure verification
- Failure classification
- Wake-on-LAN where supported
- ESP32 physical power-button recovery
- Per-server maintenance mode
- Gateway failure awareness
- Dynamic ESP module management
- Bidirectional ESP configuration synchronization
- Recovery status and controls inside the existing Beszel frontend

The main goal is to reduce the time between a real server failure and the start of a safe recovery action.

The target is:

```text
Real server failure
        │
        ▼
First failed health signal
        │
        ▼
Fast verification
        │
        ▼
Failure classified
        │
        ▼
Correct recovery action starts

TARGET: approximately 6–15 seconds
```

The server's own BIOS, POST, storage initialization, operating-system boot, and application startup time are outside this detection target.

---

## 2. Non-goals

Do not:

- Build a second monitoring dashboard.
- Replace Beszel's server pages.
- Replace Beszel's charts.
- Replace Beszel's alerts.
- Create a separate React application.
- Create a separate admin panel for normal day-to-day use.
- Require an ESP32 for normal Beszel monitoring.
- Make Beszel a dependency for autonomous ESP recovery.
- Trigger a relay from one failed probe.
- Reboot every server because the router failed.

The ESP local web portal exists for provisioning, local emergency configuration, and hardware-level access.

Normal management should happen inside the existing Beszel frontend.

---

## 3. Existing Beszel frontend is the primary UI

All Recovery Module features must be integrated into the existing Beszel user experience.

Implementation must first inspect the target Beszel release and reuse its current:

- Routing conventions
- Layout
- Navigation
- Cards
- Dialogs
- Forms
- Buttons
- Badges
- Tables
- Toasts
- Loading states
- Error handling
- Theme variables
- Responsive behavior
- Existing data-access patterns

Do not introduce a visually separate "UnitaryX dashboard" or a new design system.

The extension should look like a native Beszel feature.

Before implementation, identify the exact frontend framework, component locations, route structure, and current system-detail page structure in the checked-out Beszel revision. Do not guess file paths from this document.

---

## 4. Frontend integration model

The existing Beszel frontend should gain three main recovery surfaces.

### 4.1 Recovery status on a system card

A monitored system may show a small recovery protection indicator.

Example:

```text
┌────────────────────────────────────┐
│ Xeon Node                    ONLINE │
│ CPU 22%   RAM 61%   Disk 48%       │
│                                    │
│ Recovery Protection          96%   │
│ ESP: Rack A / CH1            READY │
└────────────────────────────────────┘
```

For systems without an ESP:

```text
Recovery Protection: NOT INSTALLED
```

For a system with WOL only:

```text
Recovery: WOL READY
```

For an ESP-protected system:

```text
Recovery: HARDWARE PROTECTED
```

For maintenance:

```text
Recovery: MAINTENANCE
```

For an offline ESP:

```text
Recovery: DEGRADED
ESP MODULE OFFLINE
```

The recovery indicator must not replace Beszel's existing server status.

Server health and recovery protection health are separate.

---

### 4.2 Recovery section on the existing system details page

Add a `Recovery` section or tab using Beszel's existing page patterns.

Example:

```text
Recovery

Protection Health                    96% HEALTHY

Wake-on-LAN                          ENABLED
Automatic WOL                        ENABLED
Last WOL                             SUCCESS

Hardware Recovery                    ONLINE
Recovery Module                      Rack A Recovery
ESP IP                               192.168.1.50
Channel                              CH1

Maintenance                          OFF

Current Recovery State               ONLINE
Last Recovery                        2 days ago
Recovery Count                       3
```

Available actions depend on configuration and permissions:

```text
[ Wake Server ]
[ Enable Maintenance ]
[ Open Recovery Module ]
```

When a server is offline and ESP recovery is available:

```text
[ Recover with ESP ]
```

Dangerous actions must use the existing Beszel confirmation-dialog pattern.

---

### 4.3 Recovery Modules management page

Add a Recovery Modules page to the existing Beszel navigation using the current navigation conventions.

Example:

```text
Recovery Modules

Rack A Recovery
ONLINE
IP: 192.168.1.50
Firmware: 0.1.0
Channels: 4 / 6
Protection Health: 96%
Config Sync: SYNCED

Rack B Recovery
OFFLINE
Last Seen: 4 minutes ago
Channels: 2 / 4
Protection Health: 45%
Config Sync: OFFLINE_PENDING
```

Actions:

```text
[ Add / Approve Module ]
[ Open Module ]
[ Disable ]
[ Remove ]
```

A newly discovered ESP appears as:

```text
NEW RECOVERY MODULE

Module ID: esp-a4f912
IP: 192.168.1.72
Firmware: 0.1.0
Channels: 6

WAITING FOR APPROVAL

[ Approve ] [ Ignore ] [ Reject ]
```

---

## 5. Low-downtime target

The initial timing target is:

| Stage | Target |
|---|---:|
| Normal probe interval | 5 seconds |
| Fast verification interval | 2 seconds |
| Fast verification attempts | 3 |
| Typical failure confirmation | 6–15 seconds |
| Recovery action dispatch | Less than 1 second after classification |
| WOL packet send | Immediate after policy decision |
| ESP short press | Immediate after safe policy decision |
| Linux boot verification | Server-specific |
| TrueNAS boot verification | Server-specific |

The system must support per-server timing profiles.

Example:

```text
Fast Linux Node
Boot verification: 60 seconds

Xeon Docker Node
Boot verification: 120 seconds

TrueNAS
Boot verification: 300 seconds
```

Do not use one global boot timeout for every server.

---

## 6. Adaptive Fast Recovery Engine

Normal monitoring and failure verification use different probe behavior.

### Normal state

```text
State: ONLINE

Probe every 5 seconds
```

When the first relevant failure occurs:

```text
ONLINE
   │
   │ first failed health signal
   ▼
FAST_VERIFY
```

### Fast verification state

In `FAST_VERIFY`, probe approximately every 2 seconds.

The engine collects multiple independent signals.

Possible signals:

- Beszel agent/system state
- TCP port 22
- TCP port 80
- TCP port 443
- Configured application port
- ESP local TCP probe
- Gateway health
- Optional future physical power-state input

Example:

```text
T+0s   First failure
T+2s   Fast verification 1
T+4s   Fast verification 2
T+6s   Fast verification 3
        │
        ▼
Classify state
```

A single 2-second network interruption must not trigger physical recovery.

---

## 7. Multi-signal failure classification

The recovery engine should classify the observed failure before selecting an action.

Required states:

```text
HEALTHY
SERVER_OFFLINE
LIKELY_POWERED_OFF
OS_UNRESPONSIVE
NETWORK_FAILURE
MAINTENANCE
UNKNOWN
RECOVERING
```

### Example decision table

| Beszel | TCP probes | Gateway | Maintenance | Classification |
|---|---|---|---|---|
| Online | Pass | Online | Off | HEALTHY |
| Offline | Fail | Online | Off | SERVER_OFFLINE |
| Offline | Fail | Offline | Off | NETWORK_FAILURE |
| Offline | Fail | Online | On | MAINTENANCE |
| Offline | Partial | Online | Off | UNKNOWN / VERIFY |
| Offline | Fail | Online | Off + physical OFF signal future | LIKELY_POWERED_OFF |
| Offline | Fail | Online | Off + physical ON signal future | OS_UNRESPONSIVE |

Do not classify a server as physically powered off using network probes alone.

Without a physical power-state signal, use `SERVER_OFFLINE` or `UNKNOWN`.

---

## 8. Fast recovery decision engine

The engine selects the least invasive valid action.

### Server in maintenance

```text
Failure detected
      │
      ▼
MAINTENANCE = ON
      │
      ▼
Record state
Show offline status
NO AUTOMATIC WOL
NO AUTOMATIC RELAY
```

### Network failure

```text
Multiple servers fail
        +
Gateway fails
        │
        ▼
NETWORK_FAILURE
        │
        ▼
Pause automatic recovery
```

Do not mass-WOL or mass-reboot servers.

### WOL-enabled server

```text
Confirmed server offline
        │
        ▼
WOL enabled?
        │ YES
        ▼
Automatic WOL enabled?
        │ YES
        ▼
Send magic packet immediately
        │
        ▼
VERIFYING_BOOT
```

If WOL succeeds, no ESP relay is used.

If WOL fails and ESP fallback is enabled:

```text
WOL_FAILED
    │
    ▼
ESP_SHORT_PRESS
```

### Server without WOL

```text
Confirmed server offline
        │
        ▼
WOL DISABLED
        │
        ▼
ESP available?
    │          │
   YES         NO
    │          │
Short press   Alert only
```

This is important for older servers that do not support WOL.

### Known powered-on but frozen server

If a future physical power-state signal confirms the machine is powered on:

```text
POWER ON
NETWORK / OS DEAD
        │
        ▼
Skip repeated WOL
        │
        ▼
Try graceful recovery if reachable
        │
        ▼
ESP physical recovery
```

Do not waste 60–90 seconds repeatedly sending WOL to a machine already known to be powered on.

---

## 9. Recovery timing profiles

Every server has a recovery profile.

Suggested fields:

```json
{
  "normal_probe_interval_seconds": 5,
  "fast_verify_interval_seconds": 2,
  "fast_verify_attempts": 3,
  "wol_enabled": true,
  "automatic_wol": true,
  "wol_verify_seconds": 60,
  "esp_fallback_enabled": true,
  "short_press_ms": 300,
  "short_press_verify_seconds": 120,
  "hard_hold_ms": 8000,
  "hard_recovery_verify_seconds": 180
}
```

Suggested presets:

### Fast Linux node

```text
Normal probe:              5s
Fast verify:               2s × 3
WOL verify:               30s
Short press verify:       60s
Hard recovery verify:     90s
```

### Xeon Docker server

```text
Normal probe:              5s
Fast verify:               2s × 3
WOL verify:               60s
Short press verify:      120s
Hard recovery verify:    180s
```

### TrueNAS

```text
Normal probe:              5s
Fast verify:               2s × 3
WOL verify:               90s
Short press verify:      300s
Hard recovery verify:    300s
```

These are starting presets, not hardcoded universal values.

---

## 10. Downtime timeline examples

### 10.1 Old server without WOL

```text
T+0s    Server fails
T+5s    Normal probe fails
T+7s    Fast verify 1 fails
T+9s    Fast verify 2 fails
T+11s   Fast verify 3 fails
T+11s   Gateway confirmed healthy
T+11s   Failure classified
T+12s   ESP short press
T+12s   Boot verification begins
```

Recovery action starts in approximately 12 seconds.

Actual service restoration depends on server boot time.

### 10.2 WOL-enabled server

```text
T+0s    Server fails
T+5s    Failure observed
T+11s   Failure confirmed
T+12s   WOL packet sent
T+12s   Boot verification begins
```

If the server boots in 45 seconds:

```text
Approximate service recovery:
57 seconds from failure
```

### 10.3 Router failure

```text
T+0s    Router fails
T+5s    Multiple server probes fail
T+7s    Gateway verification fails
T+9s    Gateway still unavailable
        │
        ▼
NETWORK_FAILURE
        │
        ▼
No WOL flood
No relay actions
```

This prevents the low-downtime engine from becoming a fast mass-reboot engine.

---

## 11. Beszel frontend recovery timeline

The existing Beszel frontend should show the active recovery state in near real time using the data-refresh or realtime mechanism supported by the target Beszel version.

Example:

```text
Xeon Node

OFFLINE

Recovery State
FAST VERIFY

Verification 2 / 3
Gateway ONLINE

Elapsed: 8 seconds
```

Then:

```text
Recovery State
WOL SENT

Waiting for server...
42 / 60 seconds
```

Then:

```text
Recovery State
ESP FALLBACK

Rack A Recovery / CH1
Short press executed
Waiting for boot...
```

Finally:

```text
RECOVERED

Recovery Method: ESP SHORT PRESS
Failure Detected: 19:32:10
Recovery Started: 19:32:22
Server Online: 19:33:18

Detection Time: 12 seconds
Recovery Time: 56 seconds
Total Downtime: 68 seconds
```

The frontend should clearly distinguish:

- Detection time
- Time until recovery action
- Boot/service verification time
- Total observed downtime

---

## 12. Existing Beszel visual language

The feature must reuse Beszel's current visual language.

Requirements:

- Use existing card primitives.
- Use existing button variants.
- Use existing dialog components.
- Use existing form components.
- Use existing status badge conventions.
- Use existing spacing and typography.
- Respect existing light/dark themes.
- Follow existing responsive breakpoints.
- Reuse current toast/error patterns.
- Avoid a separate CSS framework.
- Avoid globally overriding Beszel styles.

Suggested recovery labels:

```text
HEALTHY
VERIFYING
RECOVERING
MAINTENANCE
DEGRADED
CRITICAL
```

Use the target Beszel release's existing semantic status styling where possible instead of inventing hardcoded colors.

---

## 13. Frontend feature placement

The exact source paths must be determined from the checked-out Beszel revision.

Logical changes are:

```text
Existing Systems List
        │
        └── Add recovery protection summary

Existing System Details
        │
        └── Add Recovery section / tab

Existing Navigation
        │
        └── Add Recovery Modules entry

Existing Dialog System
        │
        ├── WOL confirmation
        ├── ESP recovery confirmation
        ├── Maintenance configuration
        └── Config conflict resolution

Existing Forms
        │
        ├── WOL configuration
        ├── Recovery policy
        ├── ESP channel mapping
        └── Timing profile
```

Do not hardcode guessed frontend file names in the implementation plan.

The coding agent must inspect the current repository first and map these logical locations to real files.

---

## 14. Server recovery settings in Beszel

Each server should expose a Recovery configuration section.

Example:

```text
RECOVERY SETTINGS

Fast Detection
[✓] Enabled

Normal Probe Interval
[ 5 ] seconds

Fast Verify Interval
[ 2 ] seconds

Fast Verify Attempts
[ 3 ]

Wake-on-LAN
[✓] Enabled

Automatic WOL
[✓] Enabled

MAC Address
[ 00:11:22:33:44:55 ]

WOL Verify Timeout
[ 60 ] seconds

ESP Hardware Recovery
Module: [ Rack A Recovery ▼ ]
Channel: [ CH1 ▼ ]

ESP Fallback
[✓] Enabled

Short Press
[ 300 ] ms

Boot Verification
[ 120 ] seconds

Maintenance
[ ] Enabled

[ Save ]
```

For a server without WOL:

```text
Wake-on-LAN
[ ] Enabled

ESP Hardware Recovery
Module: Rack A Recovery
Channel: CH3

Recovery Path
ESP SHORT PRESS → VERIFY → HARD RECOVERY
```

---

## 15. Recovery Module details in Beszel

Example:

```text
Rack A Recovery

ONLINE
Recovery Protection: 96%
IP: 192.168.1.50
Firmware: 0.1.0
Config Sync: SYNCED
Last Heartbeat: 4 seconds ago

Gateway
Main Deco
192.168.1.1
ONLINE

Temperature
Monitoring: ENABLED
Current: 31.8 °C
Status: NORMAL

Channels

CH1  Xeon Node    ONLINE       PROTECTED
CH2  TrueNAS      MAINTENANCE  PAUSED
CH3  Old Dell     ONLINE       PROTECTED
CH4  Compute      VERIFYING    PROTECTED

[ Configure Module ]
[ Open Local ESP Portal ]
```

`Open Local ESP Portal` uses the ESP's last reported LAN IP.

The UI must warn that the address is LAN-local and may be stale if the module is offline.

---

## 16. Bidirectional configuration sync

The existing Beszel frontend and ESP local portal manage one synchronized configuration model.

```text
Beszel UI
    ⇅
Desired / Reported Configuration
    ⇅
ESP32
    ⇅
ESP Local Portal
```

Every configuration revision contains:

- Module ID
- Revision number
- Configuration hash
- Change source
- Changed fields
- Timestamp

Sync states:

```text
SYNCED
SYNC_PENDING
APPLYING
OFFLINE_PENDING
CONFLICT
SYNC_ERROR
```

The Beszel frontend must display sync state.

Example:

```text
Config Sync: OFFLINE_PENDING

ESP module is offline.
Your configuration change is saved but is NOT active on hardware.
```

For maintenance:

```text
MAINTENANCE REQUESTED
```

must not be shown as:

```text
MAINTENANCE ACTIVE
```

until the ESP acknowledges the configuration.

This distinction is safety-critical.

---

## 17. ESP autonomous recovery remains independent

Low downtime must not make the ESP depend on Beszel.

The ESP keeps its own local recovery state machine.

If Beszel Hub fails:

```text
Beszel unavailable
        │
        ▼
ESP continues local probes
        │
        ▼
Gateway verification
        │
        ▼
Failure confirmation
        │
        ▼
Physical recovery
```

The ESP should use the same low-downtime concept locally:

```text
Normal probe → 5 seconds
First failure → Fast verify
Fast verify → 2 seconds × 3
Gateway healthy → Confirm failure
Recovery policy → Act
```

The exact Beszel status signal is an additional signal, not the only signal.

---

## 18. Recovery coordination and action ownership

Only one recovery owner may actively control a server at a time.

Required lock:

```text
recovery_lock(server_id)
```

Possible owners:

```text
BESZEL_WOL
ESP_AUTONOMOUS
ESP_MANUAL
POWER_RESTORE_SEQUENCE
```

Example:

```text
Beszel sends WOL
        │
        ▼
Recovery lock active
        │
        ▼
ESP sees server offline
        │
        ▼
ESP waits during WOL verification window
```

If WOL fails:

```text
WOL_FAILED
    │
    ▼
Transfer recovery stage to ESP fallback
```

This prevents Beszel sending WOL while the ESP simultaneously performs a hard hold.

The ESP must still retain a safety path if Beszel disappears while a coordination lease is active.

Therefore, coordination locks require an expiry/lease timeout.

---

## 19. Recovery event model

Record each recovery as a timeline.

Example event types:

```text
FAILURE_SIGNAL
FAST_VERIFY_STARTED
FAST_VERIFY_FAILED
FAILURE_CONFIRMED
NETWORK_FAILURE
WOL_REQUESTED
WOL_SENT
WOL_SUCCESS
WOL_FAILED
ESP_RECOVERY_REQUESTED
ESP_SHORT_PRESS
ESP_HARD_HOLD
BOOT_VERIFY_STARTED
SERVER_RECOVERED
RECOVERY_FAILED
MAINTENANCE_BLOCKED_RECOVERY
```

Each event should contain:

```json
{
  "system_id": "xeon-node",
  "module_id": "esp-a4f912",
  "channel": 1,
  "event": "ESP_SHORT_PRESS",
  "timestamp": "2026-07-12T19:32:22+05:30",
  "source": "ESP_AUTONOMOUS",
  "recovery_id": "rec-...",
  "metadata": {}
}
```

The frontend uses these events to render the recovery timeline.

---

## 20. Recovery metrics

Add recovery-specific metrics without replacing Beszel's normal monitoring metrics.

Per server:

- Mean detection time
- P95 detection time
- Mean time to recovery action
- Mean observed downtime
- WOL success count
- WOL failure count
- ESP short-press recovery count
- Hard-recovery count
- Failed recovery count

Per ESP:

- Protection health score
- Heartbeat stability
- Unexpected reset count
- Channel availability
- Recovery success count
- Relay safety/config status

Example:

```text
Last 30 Days

Failures detected                 8
Average detection time          11s
Average recovery action time    12s
Average observed downtime       74s
WOL success rate                75%
ESP recovery success rate      100%
Hard recoveries                   1
```

These metrics help tune timeouts per server.

---

## 21. Safety rules for low-downtime mode

Fast recovery must never mean reckless recovery.

Hard rules:

1. One failed probe never fires a relay.
2. Gateway failure blocks mass automatic recovery.
3. Maintenance blocks automatic WOL and relay recovery.
4. WOL-disabled servers skip WOL.
5. A server already known to be powered on should not receive repeated WOL attempts.
6. Relay actions require a validated ESP/channel mapping.
7. ESP relay outputs remain safe/open on boot, reset, and crash.
8. Configuration pending on an offline ESP is not shown as active.
9. Recovery actions use per-server rate limits.
10. Hard recovery uses a recovery budget.
11. Beszel and ESP must not perform conflicting actions simultaneously.
12. Recovery coordination leases must expire safely.
13. TrueNAS and storage systems use longer boot-verification profiles.
14. A server returning online immediately cancels later destructive recovery stages.
15. Temperature warnings do not automatically hard-power-off servers in the initial version.

---

## 22. Recommended implementation phases

### Phase 1 — Inspect and map the existing Beszel frontend

- Pin the exact Beszel commit/release.
- Identify frontend framework and build tooling.
- Identify system list components.
- Identify system details route/page.
- Identify navigation implementation.
- Identify dialog and form primitives.
- Identify PocketBase/data access patterns used by the current release.
- Document real source paths.
- Do not build a second frontend.

### Phase 2 — Recovery data model and read-only UI

- Recovery Module records
- Module heartbeat
- Channel mappings
- WOL capability
- Recovery protection status
- Recovery Modules page
- Recovery section on system details
- Recovery indicator on system cards

No physical control yet.

### Phase 3 — Fast verification engine

- 5-second normal probe
- 2-second fast verify
- Three fast verification attempts
- Gateway verification
- Failure classification
- Recovery event timeline
- Detection metrics

### Phase 4 — WOL

- Per-server WOL enable/disable
- Manual WOL
- Automatic WOL
- Boot verification
- Rate limiting
- Maintenance blocking
- Recovery timeline UI

### Phase 5 — ESP integration

- Dynamic ESP registration
- Discovery and approval
- Heartbeat
- ESP IP display
- Channel mapping
- Bidirectional configuration sync
- Recovery health score

### Phase 6 — Physical recovery

- ESP short press
- Boot verification
- Hard hold
- Recovery coordination lock
- WOL-to-ESP fallback
- Autonomous ESP recovery

### Phase 7 — Homelab safety

- Maintenance expiry
- Gateway stabilization
- DS18B20 software enable/disable
- Power-return awareness
- Staggered startup
- Recovery budgets

### Phase 8 — Tune downtime

Measure real systems:

- TrueNAS
- Xeon server
- Old Dell systems
- Compute nodes

Tune each server's verification and boot profile using real recovery metrics.

---

## 23. Acceptance criteria

1. Existing Beszel monitoring works without an ESP.
2. Existing Beszel frontend remains the primary UI.
3. No separate recovery dashboard application is required.
4. Recovery UI follows the current Beszel design conventions.
5. A real server failure enters fast verification after the first failed signal.
6. Fast verification completes in approximately 6–15 seconds under normal LAN conditions.
7. One failed probe does not trigger WOL or a relay.
8. Gateway failure prevents mass recovery.
9. WOL-disabled server skips WOL.
10. WOL-enabled server may use automatic WOL.
11. WOL success prevents ESP fallback.
12. WOL failure may transfer to ESP fallback.
13. Maintenance blocks automatic recovery.
14. Beszel shows requested versus hardware-confirmed maintenance correctly.
15. ESP autonomous recovery works when Beszel is unavailable.
16. Beszel and ESP do not execute conflicting recovery stages simultaneously.
17. Recovery coordination locks expire safely.
18. A server returning online cancels pending destructive actions.
19. TrueNAS can use a longer boot-verification profile than a Linux node.
20. Recovery timeline shows detection, action, boot verification, and recovery.
21. Frontend shows detection time separately from total downtime.
22. ESP IP is visible in the Recovery Module UI.
23. ESP local portal changes synchronize back to Beszel.
24. Beszel configuration changes synchronize to the ESP.
25. Offline ESP configuration changes show `OFFLINE_PENDING`.
26. Recovery Protection Health Score explains degraded conditions.
27. Temperature monitoring can be enabled or disabled in software.
28. Router failure is not interpreted as six simultaneous server failures.
29. Power restoration can use staggered startup order.
30. The implementation first maps real frontend source paths from the pinned Beszel revision instead of assuming paths from this specification.

---

## 24. Final target architecture

```text
                     EXISTING BESZEL FRONTEND
        Systems / System Details / Recovery Modules
                              │
                              ▼
                         BESZEL HUB
            Monitoring + Recovery Coordination
                  │                       │
                  │ WOL                   │ Config / Status
                  ▼                       ▼
              SERVER NIC         ESP32 RECOVERY MODULE
                                          │
                                Fast local verification
                                          │
                                Gateway health check
                                          │
                                  Recovery state machine
                                          │
                           ┌──────────────┼──────────────┐
                           ▼              ▼              ▼
                         CH1            CH2            CH3–6
                           │              │              │
                        Server         TrueNAS         Servers
```

### Low-downtime objective

```text
Failure
  ↓
First signal within normal probe interval
  ↓
Fast verification
  ↓
Classify failure
  ↓
Start least-invasive valid recovery action

TARGET:
6–15 seconds to confirmed decision/action start
under normal LAN conditions.
```

The system should optimize **time to correct recovery action**, not simply make relay actions faster.

**Beszel remains the monitoring platform and primary frontend. The ESP32 remains the independent hardware survival layer.**
