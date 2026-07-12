# Implementation Plan — Low-Downtime Hardware Recovery Extension

This document outlines the phased roadmap to implement the Low-Downtime Hardware Recovery Extension for Beszel, integrating the core hardware specifications with the frontend visual layouts.

---

## Real Codebase Path Mapping (React + TSX)

Following the inspection phase, the frontend is mapped to the existing **React + TSX** build system:
*   **System Details Page:** [`internal/site/src/components/routes/system.tsx`](file:///c:/Users/harik/OneDrive/Desktop/Git/beszel/internal/site/src/components/routes/system.tsx)
*   **Systems List Table:** [`internal/site/src/components/systems-table/systems-table.tsx`](file:///c:/Users/harik/OneDrive/Desktop/Git/beszel/internal/site/src/components/systems-table/systems-table.tsx) & [`systems-table-columns.tsx`](file:///c:/Users/harik/OneDrive/Desktop/Git/beszel/internal/site/src/components/systems-table/systems-table-columns.tsx)
*   **Main Navigation Bar:** [`internal/site/src/components/navbar.tsx`](file:///c:/Users/harik/OneDrive/Desktop/Git/beszel/internal/site/src/components/navbar.tsx)
*   **React Router Configurations:** [`internal/site/src/components/router.tsx`](file:///c:/Users/harik/OneDrive/Desktop/Git/beszel/internal/site/src/components/router.tsx)
*   **System Addition Dialogs:** [`internal/site/src/components/add-system.tsx`](file:///c:/Users/harik/OneDrive/Desktop/Git/beszel/internal/site/src/components/add-system.tsx)

---

## Phased Implementation Roadmap

### Phase 1: Codebase Inspection & Mapping (Completed)
*   **Target:** Inspect the frontend repository framework and identify integration files.
*   **Outcome:** Confirmed Vite/React framework instead of Svelte, and mapped all files for layout and routing extensions.

### Phase 2: Database Schema & Read-Only UI
*   **Hub Migrations:** Create PocketBase collections for `recovery_modules`, `recovery_channels`, and `recovery_events`.
*   **API Registration:** Add read endpoints on the Hub for module status and mapping info.
*   **UI Additions (Read-Only):**
    *   Add "Recovery Protection" status badges to the table row in `systems-table-columns.tsx`.
    *   Create a "Recovery" section inside `routes/system.tsx` showing configuration mappings (WOL, ESP module, state).
    *   Add the "Recovery Modules" management route in `router.tsx` and render it in `navbar.tsx`.

### Phase 3: Adaptive Fast Recovery Engine
*   **Probing Lifecycles:** Configure a dual-mode probing schedule:
    *   **Normal state:** Probes target systems every 5 seconds.
    *   **Fast verification state (`FAST_VERIFY`):** Probes systems every 2 seconds for 3 attempts (Debounce cycle).
*   **Failure Classification Engine:** Evaluate multi-signal indicators (Beszel agent, TCP probes, gateway health) to categorize system failures:
    *   `HEALTHY`
    *   `SERVER_OFFLINE`
    *   `LIKELY_POWERED_OFF`
    *   `OS_UNRESPONSIVE`
    *   `NETWORK_FAILURE`
    *   `MAINTENANCE`
    *   `UNKNOWN`
    *   `RECOVERING`
*   **Metrics Engine:** Add database tracking for mean detection time, recovery times, and success rates.

### Phase 4: Wake-on-LAN (WOL) First Recovery
*   **Go Broadcaster:** Implement Go magic-packet broadcasting over UDP port 9 in the Hub.
*   **UI Control:** Provide settings inputs for MAC, Broadcast Address, Port, and a `Wake Server` button.
*   **WOL Cascading Rule:** Trigger WOL first $\rightarrow$ wait for server boot verification timeout. If WOL fails (and fallback is allowed), escalate to the ESP physical recovery path.

### Phase 5: ESP Module Integration & Heartbeats
*   **LAN Discovery:** Implement discovery endpoints (`POST /api/beszel/recovery/register`) to listen for newly powered ESP modules on the LAN.
*   **Heartbeat Registry:** Enable periodic module heartbeats tracking module uptime, reset reasons, and IP alterations.
*   **Config Sync:** Implement bidirectional config sync using revisions and SHA256 hashes. Add an `OFFLINE_PENDING` warning state on the UI if configuration changes are saved but the ESP is offline.

### Phase 6: Physical Recovery & Coordination Locks
*   **Relay Actuators:** Implement C++/Arduino commands for short button presses (300ms) and hard power-downs (8000ms).
*   **Coordination Lease Locks (`recovery_lock`):** Prevent conflicting actions (e.g. Hub broadcasting WOL while ESP32 triggers a hard reset). Leases use strict timeouts to prevent deadlocks if connectivity is lost.
*   **Fallback Sequence:** Stagger escalations cleanly: WOL $\rightarrow$ Graceful endpoint request $\rightarrow$ ESP short press $\rightarrow$ ESP hard-hold recovery.

### Phase 7: Homelab Safeties & Gateway Monitoring
*   **Network Verify State:** Implement gateway IP monitoring on the ESP. If the gateway goes down, pause all automatic recovery channels.
*   **Maintenance Lockout:** Track maintenance lockout flags persistently in the ESP's non-volatile storage (NVS) to prevent reboots during manual updates.
*   **DS18B20 Alerts:** Read ambient rack temp. Sensor errors (`SENSOR_ERROR`) must not trigger false server power actions.
*   **Staggered Boot Sequencing:** Stagger system power-on sequence on AC return. Confirm BIOS autoboot (`Restore on AC Power Loss`) status before simulating a relay click.

### Phase 8: Deployment Diagnostics & Profile Tuning
*   Deploy diagnostic profiles to tune timeouts for individual servers (e.g., fast boot profile for Linux nodes, long 300s boot verification profile for TrueNAS).
*   Compile historical performance statistics on the Beszel dashboard.
