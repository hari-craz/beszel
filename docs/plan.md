# Implementation Plan — Beszel Hardware Recovery Module

This document outlines the step-by-step roadmap to implement the optional ESP32 Hardware Recovery Module. The features from the project brief are divided into six sequential phases, starting from standalone hardware logic and ending with bidirectional synchronization and Wake-on-LAN integration.

---

## Phase 1: Standalone Hardware Loop (Local Verification & Relays)

**Goal:** Establish a robust local monitoring and recovery machine running entirely on the ESP32, with no external server dependencies.

### 1. Hardware Initialization & GPIO Control
*   **Fail-Safe Relays:** Configure relay GPIO pins to default to `LOW` (de-energized) on boot, reset, or code crashes. Use hardware pull-downs to prevent floating states.
*   **Optocoupler Isolation:** Verify electrical isolation between ESP32 control circuitry and the motherboard header pins.

### 2. Standalone State Machine
*   Implement independent per-channel state machines matching the lifecycle:
    ```text
    ONLINE ──► VERIFYING_FAILURE ──► SHORT_PRESS ──► BOOT_GRACE ──► HARD_HOLD ──► CRITICAL ──► COOLDOWN ──► RETRY
    ```
*   **Debounce Verification:** Probes require *N* consecutive failures before transitioning from `VERIFYING_FAILURE` to `SHORT_PRESS`.
*   **Non-Volatile Storage (NVS):** Save cumulative recovery counters persistently to survive unexpected reboots.

### 3. Local Status & Alerts
*   **I2C LCD Manager:** Async, rate-limited LCD updates (e.g., 20x4 display) using page rotation to show channel health (e.g., `OK`, `VERIFY`, `BOOT`, `FORCE`).
*   **Buzzer Manager:** Event-driven, non-blocking beep patterns (1 short beep on boot, 3 beeps on recovery start, warning patterns on critical failures).

---

## Phase 2: Gateway Health & Maintenance Safeguards (Homelab Safety)

**Goal:** Safeguard the homelab environment from network glitches and configure maintenance lockouts locally on the device.

### 1. Gateway Health Monitoring
*   Separate local network checking from server-specific health. Periodically ping/probe the gateway IP (e.g., router).
*   **`NETWORK_VERIFY` State:** If the gateway drops, pause all automatic server reboots, lock relay actions, and update the LCD to reflect a network fault.
*   **Stabilization:** Introduce a 30-second delay after gateway restoration before resumption of server probing.

### 2. Local Maintenance Lockouts
*   Allow locking out specific channels from automatic recovery.
*   Store maintenance status persistently on NVS so ESP32 reboots do not re-enable recovery actions during OS installations.
*   Implement duration-based expiries that exit maintenance safely by starting fresh probe verification cycles.

### 3. DS18B20 Temperature Subsystem
*   Periodically sample ambient rack temperature.
*   Compare against warning and critical thresholds.
*   Isolate errors: if the sensor fails, transition to `SENSOR_ERROR` but do not trigger any false reboots.

---

## Phase 3: Hub Database & API Registry (PocketBase & Go Backend)

**Goal:** Extend the Beszel Hub backend to register and communicate with ESP32 Recovery Modules.

### 1. Database Collections (PocketBase Migrations)
*   **`recovery_modules`:** Track module ID, name, IP address, firmware, maximum channels, heartbeat, status, and config hashes.
*   **`recovery_channels`:** Map systems to a module ID, channel index, target IP, ports, thresholds, and lockout flags.
*   **`recovery_events`:** Audit trail logging actor, system, action, timestamp, and results.

### 2. API Routes (`internal/hub/api.go`)
*   **`POST /api/beszel/recovery/register`:** Receives discovery registration details for dynamic module approval.
*   **`POST /api/beszel/recovery/heartbeat`:** Periodically receives statistics, latency, states, and returns config revision checks.

### 3. Notification Queue
*   Setup async alerting handlers (SMTP and Shoutrrr/Telegram) specifically triggered by `recovery_events` database additions.

---

## Phase 4: Svelte Web UI Integration (Configuration & Mapping)

**Goal:** Expose recovery configurations and status monitoring to administrators on the dashboard.

### 1. Settings & Systems Panel
*   Add a **Hardware Recovery Settings** panel for discovery, approval, naming, and channel mapping.
*   Implement input forms for port lists, thresholds, boot-grace periods, and stagger weights.

### 2. Live Status Reporting
*   Display real-time state machine indicators (`ONLINE`, `VERIFYING_FAILURE`, `COOLDOWN`, `CRITICAL`) for mapped systems.
*   Display calculated **Recovery Protection Health Score** (0–100%) showing point deductions with helpful context logs.

---

## Phase 5: Wake-on-LAN (WOL) Escalation Path

**Goal:** Implement WOL first logic to prevent mechanical wear of motherboard relays.

### 1. Magic Packet Broadcaster
*   Write a network module on the Beszel Hub to construct and broadcast magic packets over UDP port 9.
*   Provide settings for MAC address, subnet broadcast IP, and verification timeout.

### 2. Cascading Policy Controller
*   Implement the cascading recovery ladder:
    ```text
    1. Send Wake-on-LAN packet
    2. Wait for boot verification timeout
    3. If unsuccessful -> Simulate ESP32 short press
    4. Continue normal state machine ladder
    ```
*   Add Svelte buttons to trigger manual Wake-on-LAN actions and save historical results in the audit log.

---

## Phase 6: Bidirectional Sync & Power-Loss Staggering

**Goal:** Establish conflict-safe config syncing and coordinate staggered boots after power restoration.

### 1. Bidirectional Config Sync
*   Enforce monotonic revision counters and configuration hashes.
*   **Conflict UI:** Display revision conflicts if changes happen in both dashboards simultaneously, letting the admin select the target config.
*   **Deferred application:** Queue relay GPIO adjustments until target channels are idle.
*   **`OFFLINE_PENDING` Status:** Track changes made while the ESP32 is offline and push updates upon reconnection.

### 2. AC Power Outage Sequencer
*   Coordinate server startup staggering when power is restored (e.g., storage boots first, gateway stabilized, then compute systems).
*   **BIOS check:** Prior to simulating a button click, verify if the server is already booting via `Restore on AC Power Loss`.
