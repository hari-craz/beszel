# Project Brief — Beszel Hardware Recovery Module

> **Working name:** Hardware Recovery Module (final name TBD; do not hardcode the product name).
> **Owner:** Hariharasudhan (Harix)
> **Status:** Architecture defined; ready for prototype.
> **Base platform:** Beszel
> **Core idea:** Beszel remains the primary lightweight server monitoring platform. The ESP32 recovery module is an optional hardware add-on for autonomous physical server recovery.

---

## 1. What this is

An optional ESP32-based hardware recovery module for Beszel-managed servers.

Beszel continues to provide server monitoring, resource visibility, Docker monitoring, history, and alerts. The ESP32 module adds a capability normal software monitoring cannot provide: physical interaction with a server's motherboard power-button header.

A single ESP32 can manage approximately 4–6 servers using independent relay or optocoupler channels.

The module is optional:

- Beszel works normally without the ESP32.
- Servers with the hardware module gain autonomous physical recovery.
- The ESP32 must continue protecting servers even if Beszel, the Beszel Hub, or the internet is unavailable.

This project does not replace Beszel and does not build a new monitoring platform.

---

## 2. Product model

### Standard Beszel deployment

```text
Server
  │
Beszel Agent
  │
Beszel Hub
  │
Dashboard + Monitoring + Alerts
```

### Beszel with optional hardware recovery

```text
                    Beszel Hub
                         │
              Monitoring and Visibility
                         │
               Optional Integration
                         │
                         ▼
              ESP32 Recovery Module
          ┌──────┬──────┬──────┬──────┐
          │      │      │      │      │
         CH1    CH2    CH3    CH4   CH5/6
          │      │      │      │      │
       Server1 Server2 Server3 Server4 Servers
```

Beszel is the monitoring layer.

The ESP32 is the hardware survival layer.

---

## 3. Core architecture rule

The ESP32 must never require Beszel to perform automatic recovery.

```text
Beszel available     → monitoring + integration data available
Beszel unavailable   → ESP32 recovery still works
Internet unavailable → local recovery still works
Server OS frozen     → physical relay recovery remains available
```

Beszel may provide an additional failure signal, but a Beszel alert must never directly fire a relay.

The ESP32 always verifies a server failure using its own local probes before taking a physical action.

---

## 4. Hardware design

### Main controller

- ESP32
- Independent power supply
- Hardware watchdog timer enabled
- Local I2C LCD display on every Recovery Module
- Local buzzer for audible recovery and critical alerts
- Wi-Fi connectivity for the initial prototype
- Ethernet-capable ESP32 variant may be considered for a production revision

### Recovery channels

A controller may support 2, 4, 6, or another firmware-declared number of independent server channels. Multiple ESP32 Recovery Modules can be dynamically registered, disabled, and removed.

Each channel contains:

- One relay or optocoupler output
- Fail-safe default state
- Connection across the motherboard power-switch header
- Independent software state machine
- Configured server IP or hostname
- Optional Beszel system mapping

Example:

| Channel | Server | LAN Address | Recovery Output |
|---|---|---|---|
| CH1 | Xeon Node | 192.168.1.10 | Relay 1 |
| CH2 | TrueNAS | 192.168.1.11 | Relay 2 |
| CH3 | Compute Node | 192.168.1.12 | Relay 3 |
| CH4 | Backup Node | 192.168.1.13 | Relay 4 |
| CH5 | Optional Node | Configurable | Relay 5 |
| CH6 | Optional Node | Configurable | Relay 6 |

The output is wired in parallel with the physical motherboard power button.

A short contact simulates a normal power-button press.

A sustained contact simulates holding the power button.

---


## 4A. Local I2C LCD display

Every ESP32 Recovery Module includes a local I2C LCD display.

Recommended prototype display:

- 20x4 I2C LCD preferred for multi-server modules
- 16x2 I2C LCD supported for compact modules
- Typical I2C backpack based on PCF8574
- Display address must be configurable or detected during startup

The LCD is a local status interface only. Recovery logic must continue normally if the LCD is disconnected, fails, or cannot be initialized.

### Display responsibilities

The LCD can show:

- Recovery Module name
- Module online/network state
- Beszel integration state
- Number of configured channels
- Per-server health state
- Active recovery action
- Failure count
- Recovery progress
- Critical state
- Wi-Fi reconnect state
- Module IP address during startup or provisioning

### Example normal display

```text
Rack A Recovery
CH1 XEON       OK
CH2 TRUENAS    OK
CH3 BACKUP     OK
```

For a 20x4 display, channel information may rotate automatically when more servers are configured than can be displayed at once.

### Example failure verification

```text
XEON / CH1
VERIFY FAILURE
FAIL: 3 / 5
NO ACTION YET
```

### Example recovery display

```text
XEON / CH1
RECOVERY ACTIVE
SHORT PRESS
BOOT WAIT: 42s
```

### Example critical display

```text
!!! CRITICAL !!!
BACKUP / CH3
RECOVERY FAILED
RETRY IN 04:32
```

### Display page rotation

Suggested display pages:

```text
PAGE 1 → Module summary
PAGE 2 → CH1 / CH2 status
PAGE 3 → CH3 / CH4 status
PAGE 4 → CH5 / CH6 status
PAGE 5 → Network / integration status
PAGE 6 → Last recovery event
```

Pages may rotate automatically every 3–5 seconds.

When a recovery or critical event occurs, the event page temporarily overrides normal page rotation.

After the event is resolved or acknowledged by firmware timeout, normal rotation resumes.

### Display state indicators

Use short status labels suitable for small displays:

| State | LCD Label |
|---|---|
| ONLINE | OK |
| VERIFYING_FAILURE | VERIFY |
| GRACEFUL_RECOVERY | GRACE |
| SHORT_PRESS | PRESS |
| BOOT_GRACE | BOOT |
| HARD_HOLD | FORCE |
| RECOVERED | RECOVERED |
| CRITICAL | CRITICAL |
| COOLDOWN | COOLDOWN |
| Module offline from integration | LOCAL |

### LCD failure isolation

The display must never be part of the recovery decision path.

```text
LCD working → show status
LCD failed  → watchdog continues normally
```

I2C communication must use bounded timeouts where supported. A failed or disconnected LCD must not block probing, notification processing, network reconnection, or relay control.

Display updates should be event-driven or rate-limited rather than continuously rewriting the full display in the main loop.

### Firmware module

Add a dedicated display manager:

```text
firmware/
├── main/
│   ├── display_manager.cpp
│   └── ...
└── include/
    ├── display_manager.h
    └── ...
```

`display_manager` receives read-only snapshots of module and channel state.

It must not directly operate relays or change recovery states.



## 4B. Local buzzer alert

Every ESP32 Recovery Module includes a local buzzer for audible status and fault alerts.

Recommended hardware:

- Active buzzer for the initial prototype
- Dedicated GPIO control
- Transistor driver if required by buzzer current
- Safe default OFF state during ESP32 boot and reset

The buzzer is an alerting component only. A buzzer failure must not affect server probing, relay control, LCD operation, or automatic recovery.

### Buzzer patterns

| Event | Pattern |
|---|---|
| Module boot complete | 1 short beep |
| Network connected | 2 short beeps |
| Server failure verification | 1 short beep every verification cycle, rate-limited |
| Recovery started | 3 short beeps |
| Short press executed | 1 medium beep |
| Hard recovery started | 2 long beeps |
| Server recovered | 2 short beeps + 1 medium beep |
| CRITICAL recovery failure | Repeating alert pattern |
| ESP/module fault | Distinct repeating fault pattern |

Critical alerts must be rate-limited and must not produce an uncontrolled continuous tone.

### Silence behavior

The firmware must support temporary buzzer mute/silence.

Silencing the buzzer:

- Stops audible alerts.
- Does not clear the fault.
- Does not stop automatic recovery.
- Does not change the LCD status.
- Does not change Beszel integration state.

A future physical button or local configuration action may be used to silence active alerts.

### Firmware isolation

Add a dedicated buzzer manager:

```text
firmware/
├── main/
│   ├── buzzer_manager.cpp
│   └── ...
└── include/
    ├── buzzer_manager.h
    └── ...
```

The `buzzer_manager` receives events and schedules non-blocking beep patterns.

Do not implement buzzer patterns using long blocking `delay()` calls.

```text
Recovery logic ──► Event
                    │
                    ├── LCD display
                    ├── Notification queue
                    └── Buzzer manager
```

The buzzer manager must never directly operate relays or modify recovery state.


## 5. Fail-safe electrical requirements

The recovery hardware must never cause an outage.

Required behavior:

- Relay de-energized means power button NOT pressed.
- ESP32 reset must not activate a relay.
- ESP32 boot must not activate a relay.
- GPIO floating states must not activate a relay.
- Hardware pull resistors must guarantee the safe state.
- Only one intentional recovery action may run per channel at a time.
- Relay outputs must return to the safe state after every operation.
- Prefer electrical isolation between the ESP32 and motherboard power-switch circuits.

The module controls only the low-voltage motherboard power-switch contact. It must not switch mains AC through the motherboard-button relay channels.

---

## 6. Per-server monitoring

Each configured server has its own independent watchdog state.

The ESP32 periodically probes the server over the LAN.

Initial probe methods:

- TCP port 22
- TCP port 80
- TCP port 443

Probe targets are configurable per server.

A single failed probe is not considered a server failure.

The ESP32 requires multiple consecutive failed probe cycles before starting recovery.

Example:

```text
Probe 1 failed
Probe 2 failed
Probe 3 failed
Probe 4 failed
Probe 5 failed
        │
        ▼
Failure confirmed
```

The failure threshold and probe interval must be configurable.

---

## 7. Per-channel state machine

Every server channel runs independently.

```text
ONLINE
  │
  │ repeated probe failures
  ▼
VERIFYING_FAILURE
  │
  │ failure threshold reached
  ▼
GRACEFUL_RECOVERY
  │
  │ server remains unavailable
  ▼
SHORT_PRESS
  │
  ▼
BOOT_GRACE
  │
  ├── server online ──► RECOVERED ──► ONLINE
  │
  └── server offline
             │
             ▼
         HARD_HOLD
             │
             ▼
         SHORT_PRESS
             │
             ▼
         BOOT_GRACE
             │
             ├── online ──► RECOVERED
             │
             └── offline ──► CRITICAL
                                  │
                                  ▼
                              COOLDOWN
                                  │
                                  ▼
                                RETRY
```

CRITICAL must never be a permanent dead-end state.

After a configured cooldown, the channel returns to verification and may retry recovery.

---

## 8. Recovery escalation ladder

For a confirmed failure:

1. Verify the server using repeated local probes.
2. Attempt an optional graceful recovery endpoint if configured.
3. Wait and re-probe.
4. Perform a short power-button press of approximately 300 ms.
5. Enter the configured boot-grace period.
6. Re-probe the server.
7. If still unavailable, hold the power button for approximately 8 seconds.
8. Release the output and return it to the fail-safe state.
9. Wait briefly.
10. Perform a short power-button press.
11. Enter boot grace and re-probe.
12. If the server remains unavailable, mark the channel CRITICAL.
13. Send an alert when connectivity is available.
14. Enter cooldown.
15. Retry after cooldown.

All timing values must be configurable.

---

## 9. Beszel integration

Beszel remains the primary monitoring platform.

The ESP32 integration is optional and additive.

### Beszel responsibilities

- Server monitoring
- CPU usage
- Memory usage
- Disk usage
- Docker monitoring
- Historical metrics
- Standard alerts
- Primary monitoring dashboard

### ESP32 responsibilities

- Independent LAN health verification
- Physical power-button control
- Automatic recovery escalation
- Per-server recovery state
- Recovery counters
- Last recovery information
- Hardware watchdog health

### Integration principle

```text
Beszel detects a problem
          │
          ▼
Optional failure signal
          │
          ▼
ESP32 receives signal
          │
          ▼
ESP32 independently verifies server
          │
          ▼
Only confirmed failure can start recovery
```

Beszel must not directly control a relay.

---

## 10. Integration data

The hardware module should expose or report the following information when integration is enabled:

- Device ID
- Firmware version
- Module uptime
- Wi-Fi/network status
- Last module heartbeat
- Channel number
- Mapped server identifier
- Current channel state
- Last successful probe
- Consecutive failure count
- Last recovery timestamp
- Last recovery action
- Total recovery count
- Critical failure count

Example status payload:

```json
{
  "device_id": "recovery-module-01",
  "firmware_version": "0.1.0",
  "uptime_seconds": 86400,
  "channels": [
    {
      "channel": 1,
      "server": "xeon-node",
      "state": "ONLINE",
      "failure_count": 0,
      "last_action": "SHORT_PRESS",
      "recovery_count": 3
    },
    {
      "channel": 2,
      "server": "truenas",
      "state": "VERIFYING_FAILURE",
      "failure_count": 3,
      "last_action": null,
      "recovery_count": 0
    }
  ]
}
```

The exact Beszel-side integration method should be selected after validating the current supported extension, API, webhook, and custom-metric capabilities of the target Beszel release.

Avoid depending on undocumented internal database structures.

---


## 10A. Dynamic ESP32 module registration and management

The system must support a dynamic number of ESP32 recovery modules.

There is no fixed global limit such as one ESP32 per Beszel Hub. Administrators can add, register, disable, and remove recovery modules as the deployment grows.

```text
Beszel / Integration Layer
          │
          ├── ESP Module 01 ── CH1–CH6 ── Servers
          ├── ESP Module 02 ── CH1–CH4 ── Servers
          ├── ESP Module 03 ── CH1–CH6 ── Servers
          └── ESP Module NN ── Configured Channels
```

Each ESP32 is treated as an independent **Recovery Module**.

### Recovery Module fields

- Unique module ID
- Display name
- Hardware ID / chip ID
- IP address or endpoint
- Firmware version
- Maximum supported channels
- Enabled channel count
- Last heartbeat
- Connectivity state
- Integration status
- Module enabled/disabled state

Example:

```json
{
  "module_id": "esp-a4f912",
  "name": "Rack A Recovery",
  "firmware_version": "0.1.0",
  "max_channels": 6,
  "enabled": true,
  "channels": 4,
  "last_seen": "2026-07-12T19:30:00Z"
}
```

### Add module

An administrator can register a new ESP32 recovery module.

Suggested flow:

```text
Power on ESP32
      │
      ▼
ESP enters provisioning mode
      │
      ▼
Configure network
      │
      ▼
Module receives unique identity
      │
      ▼
Add / approve Recovery Module
      │
      ▼
Configure channels
      │
      ▼
Map channels to Beszel systems
```

The integration must not assume a fixed number of ESP devices.

### Remove module

An administrator can remove a Recovery Module from the integration.

Before deletion:

1. Mark the module disabled.
2. Remove or archive Beszel system mappings.
3. Preserve historical recovery events and counters.
4. Revoke the module integration credential.
5. Remove the module from active management.

Removing a module from the integration must not unexpectedly trigger any relay.

The physical ESP32 may continue its locally configured standalone watchdog behavior until it is intentionally reset or reconfigured. This preserves hardware-layer independence.

### Disable module

A module can be disabled without deleting its history.

```text
ACTIVE
  │
  ▼
DISABLED
```

Disabling integration means the module is no longer considered active by the Beszel integration layer.

Automatic local watchdog behavior is a separate setting and must not silently be disabled by removing the Beszel integration.

### Dynamic channel discovery

Different ESP modules may support different channel counts.

Examples:

```text
ESP-01 → 6 channels
ESP-02 → 4 channels
ESP-03 → 2 channels
ESP-04 → 6 channels
```

The module reports its `max_channels` capability.

The integration dynamically creates or displays the available channel slots instead of assuming six channels.

### Server mapping

A recovery channel can be mapped to a Beszel-managed system.

```text
Beszel System ID
       │
       ▼
Recovery Module ID + Channel
```

Example:

```text
Xeon Node   → esp-a4f912 / CH1
TrueNAS     → esp-a4f912 / CH2
Backup Node → esp-b7c221 / CH1
```

A server must not be automatically assigned to multiple physical recovery channels unless redundant recovery is explicitly supported in a future version.

### Module heartbeat

Each integrated ESP32 periodically reports its status.

```text
ESP Module
    │
    └── heartbeat
          ├── module ID
          ├── firmware version
          ├── uptime
          ├── network state
          └── channel states
```

If a module stops reporting, mark it `OFFLINE`.

An offline Recovery Module must not cause Beszel to mark the protected server offline. Server monitoring and hardware-module monitoring remain separate states.

### Scalability model

The architecture is:

```text
1 Beszel deployment
        │
        ├── N monitored systems
        │
        └── N optional Recovery Modules
                    │
                    └── Dynamic channel count per module
```

The number of ESP32 modules is configuration-driven and dynamically managed.

No ESP32 module ID, count, IP address, or relay count should be hardcoded into the Beszel integration logic.


## 11. Alerting

The ESP32 may provide independent outbound alerts.

Initial channel:

- Telegram

Possible future channels:

- Webhook
- ntfy
- Beszel-integrated event visibility

Alerts should be generated for:

- Failure verification started
- Server failure confirmed
- Graceful recovery attempted
- Short press executed
- Hard hold executed
- Server recovered
- Recovery failed
- Channel entered CRITICAL
- Channel retry started
- ESP32 rebooted unexpectedly
- Network connection restored after an outage

Example:

```text
[RECOVERY MODULE]

Server: Xeon Node
Channel: CH1
Severity: CRITICAL

Automatic recovery failed.
Short press: attempted
Hard recovery: attempted

Cooldown active. Recovery will retry automatically.
```

Telegram or HTTPS notification work must not block the main probing and relay-control loop.

Use a queue or separate task for outbound notifications.

---

## 12. Network failure behavior

The ESP32 must reconnect automatically after Wi-Fi loss.

```text
Wi-Fi lost
    │
    ├── continue safe local state handling
    ├── retry Wi-Fi connection
    ├── never activate relays because of Wi-Fi loss alone
    └── resume LAN probes when network connectivity returns
```

A network outage can make all servers appear unreachable.

Therefore, the firmware should distinguish where practical between:

- Individual server failure
- Gateway/network failure
- ESP32 connectivity failure

If multiple or all configured servers fail simultaneously, the module should enter a network-verification condition before triggering multiple physical recoveries.

This prevents a router failure from rebooting every server.

---


## 12A. ESP32 crash, reboot, and startup behavior

A healthy server must never be disturbed because its Recovery Module crashed, rebooted, lost power, or experienced a firmware fault.

### Fail-safe crash behavior

```text
Server = HEALTHY
ESP32 = CRASHED / OFFLINE
        │
        ▼
ALL RELAY OUTPUTS SAFE / OPEN
        │
        ▼
SERVER CONTINUES RUNNING
```

ESP32 failure means hardware auto-recovery is temporarily unavailable. It does not mean the protected server has failed.

Beszel and the integration layer must maintain separate health states:

```text
Server Health             → ONLINE / OFFLINE
Recovery Protection Health → ONLINE / OFFLINE / DEGRADED
```

Example:

```text
Xeon Server       ONLINE
Recovery Module   OFFLINE

Server is healthy.
Automatic hardware recovery is currently unavailable.
```

### Hardware watchdog self-recovery

The ESP32 hardware watchdog timer must detect a stalled firmware task or unrecoverable main-loop hang and reset the module.

After a watchdog reset:

1. Relay GPIOs remain in the hardware-defined safe state.
2. ESP32 boots.
3. Firmware initializes safe relay states before other services.
4. LCD and buzzer initialize as non-critical components.
5. Network reconnection begins.
6. Configuration is loaded and validated.
7. The module enters STARTUP_GRACE.
8. A boot/online status message is queued.
9. All protected servers begin a fresh verification cycle.
10. Normal watchdog operation resumes.

### Startup grace

The ESP32 must not immediately perform a recovery action after boot.

```text
ESP BOOT
   │
   ▼
SAFE RELAY INITIALIZATION
   │
   ▼
STARTUP GRACE
   │
   ▼
FRESH SERVER PROBES
   │
   ▼
N CONSECUTIVE FAILURES REQUIRED
   │
   ▼
RECOVERY MAY START
```

The startup grace period is configurable. Recommended initial value: 60 seconds.

Failure counters from volatile runtime state must not cause an immediate relay action after reboot.

Persistent recovery history may be retained, but active failure verification must restart safely.

### Module heartbeat

Each integrated Recovery Module sends a periodic heartbeat.

If the heartbeat expires, the integration marks the Recovery Module OFFLINE or DEGRADED.

This must not automatically change the protected server's Beszel health state.

Example alert:

```text
[RECOVERY MODULE WARNING]

Module: Rack A Recovery
Status: OFFLINE

Protected servers may still be healthy.
Automatic hardware recovery is currently unavailable.
```

### Mandatory boot buzzer indication and online-status message

Every time the ESP32 successfully boots, it must provide a local buzzer indication and send an online-status message.

Boot buzzer sequence:

```text
ESP BOOT
   │
   ├── Firmware initialized safely → 1 short beep
   │
   ├── Network connected           → 2 short beeps
   │
   └── Protection active           → 1 long beep
```

If startup verification detects a critical protection problem, the normal `PROTECTION ACTIVE` long beep must not play. The configured warning or critical buzzer pattern is used instead.

Buzzer indications are non-blocking and must never delay relay initialization, startup grace, probing, or notification delivery.

Every time the ESP32 successfully boots, it must send an online-status message.

This applies to:

- Normal power-on
- Manual reset
- Hardware watchdog reset
- Brownout restart
- Firmware restart
- OTA reboot

The message is queued after basic firmware initialization and sent when outbound connectivity becomes available.

A temporary internet outage must not permanently discard the boot message. The notification queue should retry according to the configured retry policy.

Example Telegram message:

```text
[RECOVERY MODULE ONLINE]

Module: Rack A Recovery
Module ID: esp-a4f912
Status: ONLINE
Firmware: 0.1.0
Reset Reason: WATCHDOG
Uptime: 00:00:08
Network: CONNECTED
IP: 192.168.1.50
Configured Channels: 4

CH1 Xeon Node    VERIFYING
CH2 TrueNAS      VERIFYING
CH3 Backup Node  VERIFYING
CH4 Compute Node VERIFYING

Startup grace active.
Hardware recovery protection is initializing.
```

After startup grace and fresh server verification, the module may optionally send a second `PROTECTION ACTIVE` message:

```text
[RECOVERY PROTECTION ACTIVE]

Module: Rack A Recovery
Status: READY

CH1 Xeon Node    ONLINE
CH2 TrueNAS      ONLINE
CH3 Backup Node  ONLINE
CH4 Compute Node ONLINE

4 / 4 channels protected.
```

The boot notification is mandatory.

The later `PROTECTION ACTIVE` notification is configurable to prevent unnecessary message volume.

### Reset reason reporting

Where supported by the ESP32 platform, the online message should include the detected reset reason.

Examples:

- POWER_ON
- SOFTWARE_RESET
- WATCHDOG
- BROWNOUT
- PANIC
- OTA_REBOOT
- UNKNOWN

Repeated watchdog, panic, or brownout resets should generate a higher-severity module-health event after a configurable threshold.


## 13. Multi-server protection rule

The ESP32 must not blindly recover all servers when all probes fail.

Example:

```text
CH1 failed
CH2 failed
CH3 failed
CH4 failed
      │
      ▼
Possible network failure
      │
      ▼
Verify gateway / network health
```

If the gateway is unavailable, server recovery is paused.

If the gateway is reachable but one server is unavailable, normal recovery may proceed for that channel.

Recovery actions should be staggered to avoid multiple servers starting simultaneously and creating a power surge.

---


## 13A. Advanced homelab safety and management features

This section defines additional capabilities required for the intended homelab deployment. These features are designed for environments containing storage servers such as TrueNAS, Docker or compute nodes, older repurposed PCs, and network equipment where an ordinary network outage must not be mistaken for multiple server failures.

---

### 13A.1 Router and gateway health monitoring

The Recovery Module must monitor the local network gateway separately from protected servers.

The gateway is a **network dependency**, not a recovery channel.

Example configuration:

```text
Gateway Name: Main Deco
Gateway IP:   192.168.1.1
Probe Method: ICMP and/or TCP
```

The purpose is to distinguish:

```text
ONE SERVER UNREACHABLE
        │
        ▼
Possible server failure
        │
        ▼
Verify that server
        │
        ▼
Recovery may proceed
```

from:

```text
CH1 unreachable
CH2 unreachable
CH3 unreachable
CH4 unreachable
Gateway unreachable
        │
        ▼
NETWORK FAILURE SUSPECTED
        │
        ▼
DO NOT REBOOT ALL SERVERS
```

#### Gateway failure state

When the gateway becomes unreachable, the ESP enters `NETWORK_VERIFY`.

During `NETWORK_VERIFY`:

- Existing relay outputs remain safe/open.
- New automatic server recovery actions are paused.
- The ESP retries the gateway probe.
- The ESP may probe other known infrastructure addresses.
- The LCD displays the network fault.
- The buzzer plays the configured network-warning pattern.
- A Telegram warning is queued if internet connectivity later becomes available.
- Per-server failure counters must not blindly trigger simultaneous recovery.

Example LCD:

```text
NETWORK VERIFY
GATEWAY: OFFLINE
4 NODES UNREACH
RECOVERY PAUSED
```

The gateway failure itself must not trigger a server power-button relay.

#### Network recovery

When gateway connectivity returns:

1. Mark the gateway reachable.
2. Enter a configurable network stabilization period.
3. Re-probe every protected server.
4. Reset or safely re-evaluate stale failure verification counters.
5. Resume per-channel recovery only for servers that independently fail fresh verification.

Example:

```text
Gateway restored
      │
      ▼
Wait 30 seconds
      │
      ▼
Fresh probe all nodes
      │
      ├── Server online  → ONLINE
      └── Server offline → VERIFYING_FAILURE
```

Gateway IP, probe method, probe interval, failure threshold, and stabilization time must be configurable from the local web interface.

---

### 13A.2 Per-server maintenance and recovery lockout

Every protected server channel must support an independent `MAINTENANCE` mode.

This is required when:

- Installing an operating system
- Reinstalling TrueNAS
- Changing BIOS settings
- Replacing disks
- Testing networking
- Performing Docker or host maintenance
- Intentionally shutting down a server
- Diagnosing hardware

Maintenance mode applies to **one server channel at a time** unless the administrator intentionally enables maintenance for multiple channels.

Example:

```text
CH1 Xeon       ONLINE
CH2 TrueNAS    MAINTENANCE
CH3 Backup     ONLINE
CH4 Compute    ONLINE
```

When a channel is in `MAINTENANCE`:

- The ESP may continue health probing for visibility.
- Probe failures may be recorded.
- The LCD shows `MAINT`.
- Beszel integration reports recovery protection as paused for that server.
- The ESP must NOT short-press the relay automatically.
- The ESP must NOT hard-hold the relay automatically.
- The ESP must NOT interpret server shutdown as a recovery failure.
- Other server channels continue normal automatic protection.

Example state logic:

```text
TrueNAS goes offline
        │
        ▼
Is CH2 in MAINTENANCE?
        │
        ├── YES → Record status only
        │         NO RELAY ACTION
        │
        └── NO  → Normal failure verification
```

#### Maintenance controls

Maintenance mode can be enabled or disabled from:

- The ESP local web configuration page
- The Beszel integration UI, when the integration supports the action securely
- A future authenticated local API

Maintenance state should be stored persistently so an ESP reboot does not unexpectedly re-enable automatic recovery during an OS installation.

#### Maintenance expiry

The administrator may optionally configure an expiry:

```text
Maintenance:
  Enabled: Yes
  Duration: 2 hours
```

When the expiry time is reached, the ESP must not immediately fire a relay.

Instead:

1. Exit maintenance.
2. Enter fresh verification.
3. Apply the normal consecutive-failure threshold.
4. Recover only if the server is still confirmed failed.

The web UI should clearly show maintenance state and optional expiry time.

---

### 13A.3 Optional DS18B20 rack temperature monitoring

Each ESP32 Recovery Module may include a DS18B20 temperature sensor for local rack or server-area temperature monitoring.

Temperature monitoring is an **optional software-controlled feature**.

The sensor may be physically installed while the feature is disabled.

Configuration:

```text
Temperature Monitoring: ENABLED / DISABLED
Sensor Type:            DS18B20
Warning Threshold:      40 °C
Critical Threshold:     50 °C
Sample Interval:        10 seconds
Alert Cooldown:         10 minutes
```

#### Software enable and disable

The local web interface and Beszel integration should display the temperature feature state.

```text
Temperature Sensor
Detected:   YES
Monitoring: ENABLED
Current:    31.8 °C
Status:     NORMAL
```

The administrator can switch:

```text
Temperature Monitoring → ON
Temperature Monitoring → OFF
```

When disabled:

- Temperature is not used for alert decisions.
- No temperature buzzer warning is generated.
- No Telegram temperature warning is generated.
- No server power action is taken.
- The software may still show `SENSOR DETECTED / MONITORING DISABLED`.

Disabling temperature monitoring must not disable server watchdog protection.

#### Temperature states

Suggested states:

```text
NORMAL
WARNING
CRITICAL_TEMP
SENSOR_ERROR
DISABLED
```

Example logic:

```text
Temperature < Warning threshold
        ↓
NORMAL

Temperature >= Warning threshold
        ↓
WARNING
        ↓
LCD + buzzer + Telegram

Temperature >= Critical threshold
        ↓
CRITICAL_TEMP
        ↓
Higher severity LCD + buzzer + Telegram
```

For the initial version, temperature must **not automatically hard-power-off servers**.

Temperature is used for awareness and alerting only.

Example Telegram alert:

```text
[RACK TEMPERATURE WARNING]

Module: Rack A Recovery
Temperature: 43.7 °C
Warning Limit: 40.0 °C
Status: WARNING

Automatic server shutdown is disabled.
Check rack airflow and cooling.
```

#### Sensor failure

If the DS18B20 is disconnected or returns invalid data:

- Mark `SENSOR_ERROR`.
- Do not generate a fake high-temperature event.
- Continue all server monitoring and recovery.
- Show the sensor fault on the LCD and software.
- Send a rate-limited sensor fault notification.

---

### 13A.4 Local web configuration portal

Every ESP32 Recovery Module must provide a local web configuration interface on the LAN.

The main purpose is to configure the module without recompiling or reflashing firmware.

Example:

```text
http://192.168.1.50
```

The portal is LAN-oriented and must not be intentionally exposed through Cloudflare Tunnel or the public internet.

#### Dashboard page

The dashboard should show:

- Module name
- Module ID
- Firmware version
- ESP IP address
- MAC or hardware identity where appropriate
- Uptime
- Reset reason
- Network status
- Beszel integration status
- Gateway status
- Temperature feature state
- Current temperature
- Channel count
- Per-channel server state
- Last recovery event
- Hardware protection health score

Example:

```text
Rack A Recovery
IP: 192.168.1.50
Firmware: 0.1.0
Protection Health: 96%

Gateway: ONLINE
Temperature: 31.8 °C / ENABLED

CH1 Xeon       ONLINE
CH2 TrueNAS    MAINTENANCE
CH3 Backup     ONLINE
CH4 Compute    VERIFYING
```

#### Module settings page

Configurable fields:

- Module display name
- LCD name
- Time zone
- Heartbeat interval
- Startup grace period
- Buzzer enabled/disabled
- Buzzer mute state
- Telegram configuration
- Beszel integration configuration
- Gateway monitoring configuration
- Temperature monitoring enabled/disabled
- Temperature thresholds
- OTA settings

#### Channel configuration page

Each channel can configure:

- Channel enabled/disabled
- Server display name
- Server IP or hostname
- Probe ports
- Probe interval
- Failure threshold
- Short-press duration
- Hard-hold duration
- Boot-grace period
- Critical cooldown
- Relay GPIO
- Beszel system mapping
- Maintenance mode
- Maintenance expiry

Example:

```text
CHANNEL 2

Name:               TrueNAS
Host:               192.168.1.11
Probe Ports:        22, 80, 443
Failure Threshold:  5
Relay:              CH2
Beszel System:      truenas-main
Maintenance:        ENABLED
Maintenance Until:  22:00
```

#### Configuration safety

Configuration changes must be validated before saving.

Examples:

- Two active channels must not use the same relay GPIO.
- Invalid IP addresses must be rejected.
- Hard-hold duration must have safe minimum and maximum limits.
- A channel without a valid relay mapping cannot enable automatic recovery.
- Dangerous changes should require confirmation.

Configuration should be stored persistently.

A corrupted configuration must cause the affected recovery channel to enter a safe disabled/degraded state, not operate an unknown relay.

---


## 13A.4A Bidirectional configuration synchronization

The ESP local web portal and the Beszel-side Recovery Module software must remain synchronized.

A setting changed from either management surface must be reflected on the other surface after successful synchronization.

```text
        Beszel Recovery UI
                │
                │ bidirectional config sync
                ▼
         ESP Recovery Module
                │
                ▼
        ESP Local Web Portal
```

Examples:

```text
Change CH1 name in Beszel
        ↓
ESP receives new configuration
        ↓
ESP validates and applies it
        ↓
ESP local web portal shows new CH1 name
        ↓
LCD uses the new name
```

and:

```text
Change CH2 IP in ESP web portal
        ↓
ESP validates and saves it
        ↓
ESP reports configuration change
        ↓
Beszel integration receives the update
        ↓
Beszel Recovery UI shows the new CH2 IP
```

### Configuration ownership model

The system uses a synchronized configuration model rather than treating either UI as a disconnected configuration copy.

Each Recovery Module maintains:

- `desired_config`
- `reported_config`
- `config_revision`
- `config_hash`
- `last_config_source`
- `last_config_sync`
- `sync_status`

`desired_config` is the configuration the management software wants the ESP to use.

`reported_config` is the configuration the ESP confirms it is currently using.

Example:

```json
{
  "module_id": "esp-a4f912",
  "config_revision": 42,
  "config_hash": "sha256:...",
  "last_config_source": "BESZEL_UI",
  "sync_status": "SYNCED"
}
```

### Beszel-to-ESP change flow

When an administrator changes a setting in the Beszel Recovery Module UI:

1. Validate the requested configuration in software.
2. Create a new configuration revision.
3. Store it as the desired configuration.
4. Mark the module `SYNC_PENDING`.
5. Deliver or expose the new configuration to the ESP over the trusted LAN integration.
6. ESP receives the revision.
7. ESP validates the configuration using its own safety rules.
8. ESP writes the configuration persistently.
9. ESP applies settings that are safe to apply immediately.
10. ESP reports the active configuration, revision, and hash.
11. Software compares desired and reported state.
12. If they match, mark `SYNCED`.
13. If the ESP rejects the change, mark `SYNC_ERROR` and display the reason.

Example:

```text
Beszel UI
CH1 Host: 192.168.1.20
        │
        ▼
Revision 43 created
        │
        ▼
ESP validates revision 43
        │
        ├── VALID   → Apply → ACK revision 43
        │
        └── INVALID → Reject → Return reason
```

### ESP-to-Beszel change flow

When an administrator changes a setting from the ESP local web portal:

1. ESP authenticates the local administrator.
2. ESP validates the change.
3. ESP creates a new local configuration revision.
4. ESP saves and applies the configuration.
5. ESP records `last_config_source = ESP_WEB`.
6. ESP sends the new configuration revision and hash to the integration.
7. Beszel-side software validates and stores the reported configuration.
8. The Recovery Module UI updates to show the new values.
9. Desired and reported configuration are reconciled.
10. The module returns to `SYNCED`.

The Beszel-side UI must not continue showing stale values after an ESP-local configuration change.

### Sync status

Each module displays one of these states:

| Sync state | Meaning |
|---|---|
| SYNCED | Beszel and ESP configuration match |
| SYNC_PENDING | A new configuration is waiting to reach or be applied by the ESP |
| APPLYING | ESP is validating or applying a revision |
| OFFLINE_PENDING | ESP is offline and a software-side change is waiting |
| CONFLICT | Both sides changed from the same older revision |
| SYNC_ERROR | Configuration was rejected or could not be applied |

Example UI:

```text
Rack A Recovery
Config Sync: SYNCED
Revision: 43
Last Change: ESP Web
Last Sync: 8 seconds ago
```

### Offline changes

The ESP may be offline when a configuration is changed in Beszel.

In this case:

```text
Beszel config changed
        ↓
ESP OFFLINE
        ↓
Store desired revision
        ↓
OFFLINE_PENDING
        ↓
ESP reconnects
        ↓
Revision synchronization
        ↓
ESP validates and applies
        ↓
SYNCED
```

The software must clearly show that the configuration has **not yet reached the hardware**.

For safety, the UI must not claim that a relay, maintenance, or recovery setting is active until the ESP acknowledges it.

### Conflict handling

A simple blind `last write wins` policy must not be used for safety-sensitive configuration.

Example conflict:

```text
Revision 50 is synchronized

Beszel changes CH1 relay mapping
          │
          │
ESP local portal changes CH1 recovery timing
          │
          ▼
Both changes originate from revision 50
          │
          ▼
CONFLICT
```

For independent non-overlapping fields, the software may safely merge changes after validation.

For the same field or safety-sensitive settings, require conflict resolution.

Safety-sensitive fields include:

- Relay GPIO mapping
- Channel enabled state
- Automatic recovery enabled state
- Maintenance mode
- Hard-hold duration
- Short-press duration
- Power-return startup behavior
- Startup priority
- Gateway dependency
- Recovery thresholds

The UI should show:

```text
CONFIGURATION CONFLICT

Module: Rack A Recovery
Channel: CH1

Beszel value:
Hard Hold = 8 seconds

ESP Web value:
Hard Hold = 12 seconds

Select configuration to apply.
```

Until resolved, the ESP continues using its last successfully validated active configuration.

### Configuration revision rules

Every accepted configuration change increments a monotonic revision number.

Example:

```text
Revision 41 → Initial synchronized config
Revision 42 → CH1 renamed
Revision 43 → CH2 IP changed
Revision 44 → Temperature monitoring disabled
```

The ESP and integration exchange revision and configuration hash during heartbeat.

```text
ESP heartbeat
├── module_id
├── config_revision
├── config_hash
└── reported state
```

If revision numbers match but hashes differ, mark `SYNC_ERROR` or `CONFLICT` and request a full configuration comparison.

### Immediate and deferred settings

Some settings can apply immediately:

- Server display name
- Probe IP
- Probe ports
- Probe interval
- Temperature monitoring enabled/disabled
- Temperature thresholds
- LCD name
- Buzzer settings

Some changes require special handling:

- Relay GPIO mapping
- Network configuration
- OTA configuration
- Module identity-related settings

A relay mapping change must only be committed while the affected relay is in the safe/open state and no recovery action is active.

If a recovery action is active, the change is stored as pending and applied after the channel reaches a safe state.

Network changes may require an ESP restart. The UI must explicitly show `RESTART REQUIRED`.

### Maintenance mode synchronization priority

Maintenance mode is safety-critical and must synchronize quickly.

Example:

```text
Enable MAINTENANCE in Beszel
        ↓
SYNC_PENDING
        ↓
ESP ACKNOWLEDGES
        ↓
MAINTENANCE ACTIVE
```

The Beszel UI must distinguish:

```text
MAINTENANCE REQUESTED
```

from:

```text
MAINTENANCE ACTIVE ON ESP
```

If the ESP is offline, the UI shows:

```text
MAINTENANCE PENDING
ESP OFFLINE — NOT CONFIRMED ON HARDWARE
```

This prevents the administrator from assuming automatic recovery is disabled when the command has not reached the ESP.

A maintenance change made directly on the ESP local portal takes effect locally after validation and is immediately reported to the software when connectivity exists.

### Temperature setting synchronization

Temperature monitoring configuration is synchronized in both directions.

Fields include:

- Monitoring enabled/disabled
- Warning threshold
- Critical threshold
- Sample interval
- Alert cooldown

Example:

```text
Beszel: Temperature Monitoring → OFF
        ↓
ESP applies OFF
        ↓
No temperature alert decisions
        ↓
ESP reports DISABLED
        ↓
Beszel shows DISABLED / SYNCED
```

If enabled from the ESP web portal, the Beszel Recovery UI must reflect the enabled state and active thresholds.

### Synchronization transport

The exact Beszel integration implementation may use the supported extension/API mechanism selected for the target Beszel release.

The synchronization protocol itself should remain independent of Beszel internals.

Recommended logical endpoints or message operations:

```text
GET  /module/config
PUT  /module/config
POST /module/config/report
POST /module/heartbeat
```

These are logical protocol examples, not a requirement to expose the ESP to the public internet.

All configuration synchronization is LAN/VPN-oriented and authenticated.

### Source tracking and audit history

Every configuration change should record:

- Module ID
- Revision
- Changed fields
- Previous values
- New values
- Change source
- Timestamp
- Apply result

Suggested sources:

```text
BESZEL_UI
ESP_WEB
SYSTEM_MIGRATION
FIRMWARE_DEFAULT
```

Example history:

```text
Rev 43
Source: ESP_WEB
CH2 host:
192.168.1.11 → 192.168.1.21
Result: APPLIED / SYNCED
```

This makes it possible to understand where a setting changed and prevents confusing configuration drift.

### Core synchronization rule

```text
CHANGE IN BESZEL
      ↓
MUST REACH ESP
      ↓
ESP ACKNOWLEDGES ACTIVE CONFIG
      ↓
BOTH SHOW SAME VALUE
```

```text
CHANGE IN ESP WEB
      ↓
TAKES EFFECT LOCALLY AFTER VALIDATION
      ↓
MUST REPORT TO SOFTWARE
      ↓
BOTH SHOW SAME VALUE
```

The system must always distinguish **requested configuration** from **confirmed active hardware configuration**.


### 13A.5 ESP IP address visibility in Beszel

Every integrated Recovery Module must report its current local IP address.

The Beszel integration should display the IP address for each ESP module.

Example:

```text
Recovery Modules

Rack A Recovery
Status: ONLINE
IP Address: 192.168.1.50
Firmware: 0.1.0
Channels: 4 / 6
Protection: 96%
```

The IP address should be updated when the ESP heartbeat reports a new address.

This is especially useful when DHCP changes the ESP address.

The software should clearly distinguish:

- Last reported IP
- Module online/offline state
- Last heartbeat time

An old IP from an offline module must not be presented as guaranteed current.

Recommended deployment practice is to use a DHCP reservation for each Recovery Module.

When appropriate, the displayed IP may provide a LAN-only shortcut to the ESP local web configuration portal.

The integration must not publish the ESP local web interface to the public internet.

---

### 13A.6 Automatic ESP discovery and approval

New ESP32 Recovery Modules should be discoverable dynamically.

The system must not require application code changes to add another ESP.

Suggested discovery architecture:

```text
New ESP boots
      │
      ▼
Connects to LAN
      │
      ▼
Starts local discovery advertisement
      │
      ▼
Integration detects module
      │
      ▼
RECOVERY MODULE AVAILABLE
      │
      ▼
Administrator approves module
      │
      ▼
Module registered
      │
      ▼
Channels mapped to Beszel systems
```

Possible local discovery mechanisms may include mDNS or another LAN-local discovery protocol selected during implementation.

Discovery must not automatically grant trusted control access.

#### Unapproved module state

A newly discovered ESP appears as:

```text
NEW RECOVERY MODULE

Name: Recovery Module
Module ID: esp-a4f912
IP: 192.168.1.50
Firmware: 0.1.0
Channels: 6

Status: WAITING FOR APPROVAL
```

The administrator can:

- Approve
- Rename
- Map channels
- Ignore
- Reject

An unapproved ESP must not be allowed to modify Beszel configuration or gain broad credentials.

#### Duplicate identity handling

If two modules report the same identity:

- Mark an identity conflict.
- Do not silently merge them.
- Display a warning.
- Require administrator action.

---

### 13A.7 Recovery Protection Health Score

The Beszel integration should calculate and display a `Recovery Protection` health score for each ESP module.

Example:

```text
Recovery Protection: 96%
Status: HEALTHY
```

The score represents the health of the **hardware recovery protection system**, not server performance.

It must not replace Beszel CPU, memory, disk, or server availability metrics.

#### Suggested score inputs

Initial scoring model:

| Check | Suggested Weight |
|---|---:|
| ESP heartbeat healthy | 25 |
| Relay safety/config validation | 20 |
| Protected channels healthy/configured | 20 |
| No repeated ESP crash/reset pattern | 10 |
| Network/Wi-Fi stability | 10 |
| Gateway monitoring healthy | 5 |
| Temperature subsystem healthy when enabled | 5 |
| Firmware/integration status healthy | 5 |

Total: 100 points.

Example:

```text
Heartbeat                  25 / 25
Relay configuration        20 / 20
Channel protection         15 / 20
Reset stability            10 / 10
Network stability           6 / 10
Gateway health              5 / 5
Temperature                 5 / 5
Firmware/integration        5 / 5

Recovery Protection = 91%
```

#### Suggested health labels

```text
90–100% → HEALTHY
75–89%  → DEGRADED
50–74%  → WARNING
0–49%   → CRITICAL
```

The UI should explain why points were lost.

Example:

```text
Recovery Protection: 76% DEGRADED

- CH4 has no valid relay mapping
- 3 unexpected ESP resets in 24 hours
- Wi-Fi heartbeat instability detected
```

The score must never directly trigger a server hard power action.

It is a visibility and protection-readiness metric.

---

### 13A.8 Power-failure awareness and staggered startup

The Recovery Module should support optional power-failure awareness.

The goal is to recognize a site or rack power event and avoid starting every server simultaneously when power returns.

Possible input sources:

- Isolated AC-presence sensing input
- UPS dry-contact or supported status signal
- Other electrically isolated power-state input

The exact electrical design must be selected safely during hardware design.

Do not connect mains AC directly to an ESP32 GPIO.

#### Power loss state

When external power loss is detected:

```text
AC / UPS power failure detected
            │
            ▼
POWER_EVENT state
            │
            ├── Record event
            ├── LCD warning
            ├── Buzzer warning
            ├── Queue notification
            └── Suspend blind multi-node recovery
```

The ESP must consider whether its own power source remains available during the outage.

For useful outage coordination, the ESP should be powered from a UPS or another protected low-voltage supply.

#### Power return

When power return is detected:

1. Record `POWER_RESTORED`.
2. Wait for a configurable power stabilization period.
3. Verify gateway/network availability.
4. Start the configured startup sequence.
5. Stagger server startup.
6. Verify each stage before continuing where configured.

Example homelab startup order:

```text
POWER RESTORED
      │
      ▼
Wait 30 seconds
      │
      ▼
Router / Deco available?
      │
      ▼
Wait for network stabilization
      │
      ▼
TrueNAS / Storage
      │
      ▼
Wait 120 seconds + verify
      │
      ▼
Xeon / Main Server
      │
      ▼
Wait 90 seconds + verify
      │
      ▼
Compute Nodes
```

#### Startup groups and priority

Each protected server can have:

- Startup enabled/disabled
- Startup priority
- Startup group
- Delay before action
- Delay after action
- Verification requirement

Example:

```text
Priority 0 → Network infrastructure
Priority 1 → Storage / TrueNAS
Priority 2 → Main Docker / Xeon server
Priority 3 → Compute nodes
Priority 4 → Backup or optional nodes
```

The ESP only controls equipment physically connected to its recovery channels.

A router or Deco device should not be assumed controllable unless a separate safe and explicitly supported power-control method exists.

Therefore, `Router → wait` normally means **wait for the router/gateway to become reachable**, not press a motherboard relay for the router.

#### BIOS interaction

For servers, BIOS `Restore on AC Power Loss = Power On` remains recommended.

The ESP startup coordinator should verify whether each server has already started automatically before simulating a short power-button press.

```text
Power restored
      │
      ▼
Wait for server auto-boot
      │
      ├── Server online → no relay action
      │
      └── Still off after grace → verify and start configured recovery/startup action
```

This prevents the ESP from pressing the power button while a server is already booting.

---

### 13A.9 Combined decision example

The following example shows how the features work together.

```text
TrueNAS becomes unreachable
          │
          ▼
Is CH2 in MAINTENANCE?
     │               │
    YES              NO
     │               │
Record only      Is gateway healthy?
                     │
                ┌────┴────┐
                │         │
               NO        YES
                │         │
         NETWORK_VERIFY   Fresh probes
         Recovery paused      │
                              ▼
                     Failure threshold reached
                              │
                              ▼
                     Start recovery ladder
                              │
                              ├── LCD status
                              ├── Buzzer event
                              ├── Telegram event
                              └── Recovery history
```

Temperature monitoring operates alongside this logic but does not authorize a hard server power action in the initial version.

The Recovery Protection Health Score reports whether this protection system is ready and healthy, while Beszel remains responsible for primary server monitoring and metrics.



## 13A.10 Wake-on-LAN control from the Beszel Recovery UI

The Beszel-side Recovery Module integration should add **Wake-on-LAN (WOL)** as a server power-on capability.

WOL is separate from the ESP32 physical relay recovery system.

```text
Beszel Recovery UI
        │
        ├── Wake-on-LAN ──► Magic Packet ──► Server NIC
        │
        └── ESP Recovery ──► Relay ─────────► Power Button Header
```

The preferred power-on order is:

```text
1. Wake-on-LAN
        ↓
2. Wait and verify server boot
        ↓
3. If WOL fails and ESP protection is available
        ↓
4. ESP short power-button press
        ↓
5. Continue normal recovery ladder if required
```

This reduces unnecessary physical relay operations.


### Per-server WOL enable and disable from Beszel

Wake-on-LAN is an optional capability for each individual server.

The system must never assume that every Beszel-managed server supports WOL.

Each server has an explicit Beszel-side setting:

```text
Wake-on-LAN
[ ENABLED / DISABLED ]
```

Example:

```text
Xeon Node
WOL: ENABLED

TrueNAS
WOL: ENABLED

Old Dell Server
WOL: DISABLED

Compute Laptop
WOL: DISABLED
```

The administrator can enable or disable WOL independently for every server from the Beszel Recovery UI.

### WOL disabled behavior

When WOL is disabled for a server:

- The `Wake Server` WOL action is hidden or clearly disabled.
- Automatic recovery does not send a magic packet.
- Power-restoration logic skips WOL for that server.
- WOL failure counters are not created.
- The recovery state machine proceeds to the next recovery method allowed by policy.
- ESP relay recovery remains available when the server is mapped to an active Recovery Module channel.

Example:

```text
Old Dell Server OFFLINE
        │
        ▼
WOL ENABLED?
        │
       NO
        │
        ▼
ESP PROTECTION AVAILABLE?
     │             │
    YES            NO
     │             │
Short press     Alert user
```

The software should display:

```text
Old Dell Server

Status: OFFLINE
Wake-on-LAN: DISABLED
Reason: Server does not support WOL

ESP Protection: ONLINE
Module: Rack A Recovery
Channel: CH3

[ Recover with ESP ]
```

### WOL enabled behavior

When WOL is enabled:

1. Validate that a MAC address is configured.
2. Validate the broadcast/network configuration.
3. Allow manual WOL.
4. Apply automatic WOL only if `Automatic WOL` is also enabled.
5. Verify server boot after sending the magic packet.
6. Use ESP fallback only if configured and available.

The UI must distinguish:

```text
WOL Capability: ENABLED
Automatic WOL:  ENABLED
```

from:

```text
WOL Capability: ENABLED
Automatic WOL:  DISABLED
```

This allows a server to support manual Wake-on-LAN without allowing automatic recovery to wake it.

### WOL configuration UI

Suggested Beszel Recovery UI:

```text
POWER RECOVERY

Wake-on-LAN
[✓] Enable WOL

MAC Address
[ 00:11:22:33:44:55 ]

Broadcast Address
[ 192.168.1.255 ]

WOL Port
[ 9 ]

[✓] Automatic WOL

Boot Verification
[ 90 ] seconds

[✓] Use ESP fallback if WOL fails

[ Save ]
```

For a server without WOL:

```text
POWER RECOVERY

Wake-on-LAN
[ ] Enable WOL

ESP Hardware Recovery
Module: Rack A Recovery
Channel: CH3
Status: PROTECTED
```

Disabling WOL must not disable ESP hardware recovery.

Enabling WOL must not automatically disable ESP fallback.

They are separate recovery capabilities.

### WOL policy examples

Server with WOL and ESP:

```text
Xeon Node

WOL: ENABLED
Automatic WOL: ENABLED
ESP Fallback: ENABLED

Recovery:
WOL → Verify → ESP short press → Verify → hard recovery
```

Server without WOL but with ESP:

```text
Old Dell Server

WOL: DISABLED
ESP Protection: ENABLED

Recovery:
ESP short press → Verify → hard recovery
```

Server with WOL but no ESP:

```text
Remote Node

WOL: ENABLED
ESP Protection: NOT INSTALLED

Recovery:
WOL → Verify → Alert if failed
```

Server with neither:

```text
Unprotected Node

WOL: DISABLED
ESP Protection: NOT INSTALLED

Recovery:
Monitor → Alert only
```

### WOL configuration validation

If WOL is enabled without a valid MAC address, the software must reject the configuration or mark WOL `MISCONFIGURED`.

Suggested states:

- `DISABLED`
- `READY`
- `MISCONFIGURED`
- `WOL_SENT`
- `VERIFYING_BOOT`
- `WOL_SUCCESS`
- `WOL_FAILED`
- `BLOCKED_MAINTENANCE`
- `BLOCKED_NETWORK`

A `MISCONFIGURED` WOL state must not block ESP recovery.

### WOL configuration ownership

WOL enable/disable and MAC/broadcast configuration are Beszel-side server settings.

They do not need to be synchronized to the ESP for normal operation because Beszel sends the WOL packet.

The ESP only needs to know its own physical recovery channel and local recovery policy.

If a future firmware version adds ESP-originated WOL, WOL configuration can be added to the bidirectional synchronization model.


### WOL server configuration

Each Beszel-managed server may have an optional WOL configuration.

Required fields:

- WOL enabled/disabled
- MAC address
- Broadcast address
- WOL port
- Network interface or network/site mapping where required
- Boot verification timeout
- ESP fallback enabled/disabled

Example:

```json
{
  "system": "xeon-node",
  "wol_enabled": true,
  "mac_address": "00:11:22:33:44:55",
  "broadcast_address": "192.168.1.255",
  "wol_port": 9,
  "boot_verify_seconds": 90,
  "esp_fallback_enabled": true
}
```

WOL configuration is stored on the Beszel-side integration.

The ESP does not require the server MAC address unless a future firmware feature explicitly allows the ESP to send WOL locally.

### Beszel UI action

For a WOL-enabled system, the Recovery UI may display:

```text
Xeon Node
Status: OFFLINE

[ Wake Server ]
```

When the administrator selects `Wake Server`:

1. Confirm the server is configured for WOL.
2. Send the magic packet.
3. Record a `WOL_SENT` event.
4. Mark the action `VERIFYING_BOOT`.
5. Wait for the configured boot-verification period.
6. Check the Beszel system/agent state.
7. If the server becomes available, record `WOL_SUCCESS`.
8. If it remains unavailable, record `WOL_FAILED`.
9. Offer or automatically use ESP fallback according to policy.

Example:

```text
WOL SENT
   │
   ▼
VERIFYING BOOT
   │
   ├── Server online → WOL SUCCESS
   │
   └── Still offline → WOL FAILED
                            │
                            ▼
                   ESP fallback available?
                       │             │
                      YES            NO
                       │             │
                 Short press     Alert user
```

### Manual and automatic WOL

WOL supports two modes:

```text
MANUAL
Administrator selects Wake Server

AUTOMATIC
Recovery policy attempts WOL before physical relay recovery
```

Automatic WOL must be configurable per server.

Example:

```text
Power Recovery Policy

WOL:                 ENABLED
Automatic WOL:       ENABLED
ESP Relay Fallback:  ENABLED
```

A storage server or special system may use a different policy.

Example:

```text
TrueNAS

WOL:                 ENABLED
Automatic WOL:       DISABLED
ESP Relay Fallback:  MANUAL ONLY
```

### WOL and maintenance mode

Maintenance mode has priority over automatic WOL.

```text
Server offline
      │
      ▼
MAINTENANCE?
   │       │
  YES      NO
   │       │
No auto   WOL policy may run
WOL
```

Manual WOL during maintenance should require explicit confirmation.

The UI must not silently wake a server intentionally shut down for maintenance.

### WOL and gateway/network failure

WOL must not be used as a blind response when the gateway or network is in `NETWORK_VERIFY`.

If all systems become unreachable because the network failed, the software must not send repeated WOL packets to every server.

After network stabilization, the software re-evaluates server state and applies each server's configured recovery policy.

### WOL and power restoration

After AC power restoration:

1. Wait for power stabilization.
2. Wait for gateway/network readiness.
3. Allow BIOS `Restore on AC Power Loss` to start configured servers.
4. Verify server state.
5. For a server still offline, try WOL if enabled.
6. Verify boot.
7. Use ESP relay fallback only when allowed by policy.

Example:

```text
POWER RESTORED
      │
      ▼
NETWORK READY
      │
      ▼
WAIT FOR BIOS AUTOBOOT
      │
      ├── ONLINE → No action
      │
      └── OFFLINE
             │
             ▼
          TRY WOL
             │
             ├── ONLINE → Success
             │
             └── OFFLINE
                    │
                    ▼
              ESP RELAY FALLBACK
```

### WOL status in software

The Recovery UI should show:

```text
Xeon Node

Beszel Status:       OFFLINE
WOL:                 ENABLED
Last WOL:            2 minutes ago
Last WOL Result:     FAILED
ESP Protection:      ONLINE
ESP Module:          Rack A Recovery
ESP Channel:         CH1
```

Suggested WOL states:

- `DISABLED`
- `READY`
- `WOL_SENT`
- `VERIFYING_BOOT`
- `WOL_SUCCESS`
- `WOL_FAILED`
- `BLOCKED_MAINTENANCE`
- `BLOCKED_NETWORK`
- `ESP_FALLBACK`

### WOL audit events

Record:

- Actor
- Server
- MAC identifier reference
- Action source
- Timestamp
- WOL result
- ESP fallback result if used

Suggested events:

```text
WOL_REQUESTED
WOL_SENT
WOL_SUCCESS
WOL_FAILED
WOL_BLOCKED_MAINTENANCE
WOL_BLOCKED_NETWORK
ESP_FALLBACK_STARTED
```

### Security and safety

- WOL is a power-on action and must be permission-controlled.
- Do not expose unrestricted WOL actions publicly.
- Validate MAC and broadcast configuration.
- Rate-limit repeated WOL attempts.
- Do not create an infinite WOL loop.
- Maintenance mode blocks automatic WOL.
- Network failure detection blocks blind mass WOL.
- Every WOL action is audited.

### Core recovery preference

For servers supporting WOL, use the least invasive recovery mechanism first:

```text
WOL
 ↓
Graceful software recovery when reachable
 ↓
ESP short press
 ↓
ESP hard-hold recovery
```

The exact path depends on the observed server state. A server that is confirmed powered on but OS-unresponsive should not waste repeated WOL attempts; the recovery state machine may move to graceful or physical recovery.


## 14. Firmware architecture

Suggested firmware modules:

```text
firmware/
├── main/
│   ├── main.cpp
│   ├── config_manager.cpp
│   ├── network_manager.cpp
│   ├── probe_manager.cpp
│   ├── channel_manager.cpp
│   ├── recovery_manager.cpp
│   ├── relay_controller.cpp
│   ├── notification_queue.cpp
│   ├── integration_client.cpp
│   └── ota_manager.cpp
├── include/
│   ├── config_manager.h
│   ├── network_manager.h
│   ├── probe_manager.h
│   ├── channel_manager.h
│   ├── recovery_manager.h
│   ├── relay_controller.h
│   ├── notification_queue.h
│   ├── integration_client.h
│   └── ota_manager.h
└── platformio.ini
```

Do not implement the entire firmware as one large Arduino loop.

Each server should be represented by a channel configuration and runtime state object.

---

## 15. Example channel configuration

```json
{
  "channel": 1,
  "enabled": true,
  "name": "xeon-node",
  "host": "192.168.1.10",
  "probe_ports": [22, 80, 443],
  "failure_threshold": 5,
  "probe_interval_seconds": 10,
  "short_press_ms": 300,
  "hard_hold_ms": 8000,
  "boot_grace_seconds": 60,
  "critical_cooldown_seconds": 300,
  "relay_gpio": 25,
  "beszel_system_id": "optional"
}
```

Configuration must not contain hardcoded Wi-Fi passwords, Telegram tokens, or integration secrets in source control.

---

## 16. Security

- No public inbound relay-control endpoint.
- No unauthenticated remote power commands.
- No secrets committed to Git.
- ESP32 integration credentials must be configurable.
- Beszel integration is read/status oriented by default.
- Physical recovery is decided locally by the ESP32 state machine.
- OTA updates must be authenticated.
- Production firmware should support a known-good OTA fallback.
- Manual relay control, if added later, must be LAN/VPN-only and authenticated.

---

## 17. Prototype roadmap

### Phase 1 — Single-server recovery

- ESP32
- One relay/optocoupler channel
- One test PC
- TCP probing
- Failure debounce
- Short press
- Boot grace
- Hard hold
- Recovery retry

### Phase 2 — Multi-channel controller

- Expand to multiple channels
- Add dynamic Recovery Module registration and removal
- Add module heartbeat and online/offline state
- Independent state machines
- Per-channel configuration
- Staggered recovery
- Gateway health verification

### Phase 3 — Six-channel hardware

- Validate GPIO allocation
- 6 isolated outputs
- Fail-safe boot behavior
- Dedicated enclosure
- Independent power supply

### Phase 4 — Alerts

- Non-blocking Telegram queue
- Recovery and critical alerts
- Network restoration alerts

### Phase 5 — Beszel integration

- Map recovery channels to Beszel systems
- Report watchdog health and recovery metadata
- Validate supported Beszel integration interfaces
- Add optional Beszel failure signal
- ESP32 retains independent verification

### Phase 5A — Homelab safety and local management

- Gateway/router health monitoring
- Per-server maintenance lockout
- Optional DS18B20 temperature monitoring
- Software temperature enable/disable
- Local ESP web configuration portal
- Bidirectional Beszel ↔ ESP configuration synchronization
- Desired/reported configuration state with revision and hash
- Conflict detection and safety-sensitive change handling
- ESP IP visibility in Beszel integration
- Automatic LAN discovery and approval
- Recovery Protection Health Score
- Wake-on-LAN controls and automatic WOL-before-relay policy
- Optional isolated power-failure awareness
- Staggered server startup sequencing

### Phase 6 — Hardening

- ESP hardware watchdog
- OTA with fallback
- Persistent recovery counters
- Configuration validation
- Brownout testing
- Router outage testing
- ESP reset testing
- Relay fail-safe testing

---

## 18. Acceptance tests

The prototype is not complete until these cases are tested:

1. Beszel Hub is stopped; ESP recovery still works.
2. Internet is disconnected; local recovery still works.
3. Telegram is unreachable; probing is not delayed.
4. Wi-Fi disconnects and reconnects automatically.
5. ESP32 reboots; no relay activates during boot.
6. One server fails; only its assigned relay operates.
7. Router fails; all servers are not blindly rebooted.
8. Server recovers after short press; hard hold is not executed.
9. Server remains dead; hard recovery executes.
10. Recovery fails; CRITICAL enters cooldown and retries.
11. Two servers fail; recovery actions are staggered.
12. Beszel alert is received; ESP independently verifies before acting.
13. Beszel integration is disabled; watchdog behavior is unchanged.
14. A new ESP32 module is registered without changing application code.
15. An ESP32 module is disabled; its historical recovery records remain.
16. An ESP32 module is removed; its credentials are revoked and no relay is triggered.
17. Two ESP32 modules with different channel counts are discovered and represented correctly.
18. A Recovery Module goes offline; protected server monitoring remains independent.
19. LCD is disconnected; probing and automatic recovery continue normally.
20. I2C LCD stops responding; relay-control timing and watchdog logic remain unaffected.
21. A recovery event occurs; the LCD shows the active channel and recovery stage.
22. Buzzer is disconnected; monitoring and recovery continue normally.
23. A recovery starts; the configured non-blocking buzzer pattern is played.
24. A CRITICAL event occurs; the buzzer alerts repeatedly with rate limiting.
25. The buzzer is silenced; the fault and automatic recovery remain active.
26. ESP32 crashes while all servers are healthy; all relay outputs remain safe and servers continue running.
27. ESP32 hardware watchdog resets the module; no relay activates during reset or boot.
28. Every ESP32 boot queues and sends an ONLINE status message when outbound connectivity becomes available.
29. ESP32 reboots while a previous failure counter existed; startup grace and a fresh verification cycle are required before recovery.
30. Recovery Module heartbeat expires; module protection is marked OFFLINE without changing the protected server's Beszel health state.
31. Repeated watchdog, panic, or brownout resets produce a higher-severity module-health event.
32. Every ESP32 boot produces the configured safe startup buzzer sequence.
33. Protection becomes active; the ready beep plays only after fresh verification.
34. A startup critical fault occurs; the ready beep is suppressed and the warning pattern is used.
35. Gateway fails and all servers become unreachable; no mass server recovery occurs.
36. Gateway returns; fresh server verification occurs before recovery resumes.
37. TrueNAS channel enters MAINTENANCE; server shutdown does not trigger relay action.
38. Maintenance expires while a server is offline; fresh failure verification is required before recovery.
39. Temperature monitoring is disabled in software; temperature alerts stop while server recovery remains active.
40. DS18B20 fails; the sensor enters SENSOR_ERROR and no false over-temperature recovery action occurs.
41. Local web configuration changes a channel mapping without firmware recompilation.
42. Invalid duplicate relay GPIO configuration is rejected.
43. ESP DHCP address changes; the latest heartbeat updates the IP shown in the integration.
44. A new ESP appears through LAN discovery and remains untrusted until administrator approval.
45. Duplicate ESP identities produce an identity-conflict warning.
46. Recovery Protection score explains degraded protection conditions.
47. Power returns after an outage; startup actions are staggered by configured priority.
48. A server auto-boots through BIOS after AC return; ESP detects recovery and does not unnecessarily press its power button.
49. Router remains unavailable after power return; dependent server startup coordination waits according to policy.
50. A channel IP is changed in the Beszel Recovery UI; ESP applies and reports the same active value.
51. A channel probe port is changed in the ESP web portal; the Beszel Recovery UI updates to the same value.
52. Beszel configuration changes while ESP is offline; UI shows OFFLINE_PENDING until ESP acknowledges the revision.
53. Maintenance is requested from Beszel while ESP is offline; UI clearly states that maintenance is not confirmed on hardware.
54. Both sides change the same safety-sensitive field from one revision; the module enters CONFLICT and retains the last validated active configuration.
55. ESP and Beszel report the same revision but different hashes; synchronization error is detected.
56. Relay GPIO mapping changes during active recovery; the change remains pending until the relay/channel reaches a safe state.
57. Temperature monitoring is disabled from Beszel; ESP applies it and the local portal shows DISABLED.
58. Temperature monitoring is enabled from the ESP portal; Beszel displays the enabled state and synchronized thresholds.
59. Every accepted configuration change records its source, revision, changed fields, and apply result.
60. Manual Wake Server sends a WOL magic packet and records the action.
61. A server wakes after WOL; ESP relay fallback is not used.
62. WOL fails; ESP relay fallback starts only when the server policy allows it.
63. A server is in MAINTENANCE; automatic WOL is blocked.
64. Gateway/network failure is active; blind mass WOL is blocked.
65. Power returns; BIOS autoboot is given time before WOL is attempted.
66. WOL repeatedly fails; rate limiting prevents an infinite WOL loop.
67. A server is known to be powered on but OS-unresponsive; recovery does not repeatedly waste time on WOL.
68. WOL is disabled for a server without WOL support; automatic recovery skips magic-packet delivery.
69. WOL-disabled server has ESP protection; recovery proceeds directly to the configured ESP recovery path.
70. WOL is enabled without a valid MAC address; software rejects or marks the configuration MISCONFIGURED.
71. Manual WOL is enabled while Automatic WOL is disabled; manual wake remains available but automatic recovery does not send WOL.
72. Disabling WOL does not disable the server's ESP hardware recovery mapping.

---

## 19. Final architecture

```text
                     BESZEL
        Monitoring / Metrics / History / Alerts
                        │
                 Optional Integration
                        │
                        ▼
             ESP32 RECOVERY MODULE
        Independent Hardware Survival Controller
       ┌────────┬────────┬────────┬────────┐
       │        │        │        │        │
      CH1      CH2      CH3      CH4     CH5/6
       │        │        │        │        │
    Server 1 Server 2 Server 3 Server 4  Servers
```

**Beszel monitors the server.**

**The optional ESP32 module physically recovers the server.**

**Beszel may inform the watchdog, but only the watchdog can verify and authorize an automatic physical recovery.**
