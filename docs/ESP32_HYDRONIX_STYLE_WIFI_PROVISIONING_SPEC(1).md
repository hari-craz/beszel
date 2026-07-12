# ESP32 Recovery Module — Hydronix-Style Wi-Fi Provisioning Specification

> **Document type:** Standalone ESP provisioning specification
> **Target device:** ESP32 Recovery Module
> **Provisioning portal:** 192.168.4.1
> **Purpose:** Configure Wi-Fi and Beszel connectivity without recompiling firmware.

## 1. Purpose

Every ESP32 Recovery Module must provide a first-time setup experience similar to the Hydronix device onboarding flow.

The user must not hardcode Wi-Fi credentials or recompile firmware for each ESP.

```text
FIRST BOOT
   ↓
ESP creates Wi-Fi hotspot
   ↓
User connects with phone/laptop
   ↓
Open 192.168.4.1
   ↓
Configure Wi-Fi + Beszel
   ↓
SAVE & CONNECT
   ↓
ESP joins LAN
   ↓
ESP appears in Beszel
```

## 2. First boot detection

On every boot, the ESP checks for a valid saved configuration.

```text
ESP BOOT
   ↓
Load saved configuration
   │
   ├── VALID
   │     ↓
   │   NORMAL MODE
   │
   └── MISSING / INVALID
         ↓
       SETUP MODE
```

A first-time device automatically enters `SETUP MODE`.

**Mandatory rule: the automatic `192.168.4.1` provisioning flow happens only during first-time setup when no valid saved provisioning configuration exists.**

After successful provisioning, normal boots must never automatically start the setup hotspot because of a temporary Wi-Fi, router, or Beszel outage.

## 3. Setup access point

In setup mode, the ESP creates its own Wi-Fi access point.

Example:

```text
SSID: Beszel_Recovery_A4F9
Portal IP: 192.168.4.1
```

The suffix is derived from a short, non-secret module identifier.

Examples:

```text
Beszel_Recovery_A4F9
Beszel_Recovery_82C1
Beszel_Recovery_F921
```

This allows multiple new ESP modules to be identified during installation.

Do not include API keys, registration secrets, or full authentication tokens in the SSID.

## 4. LCD behavior during setup

The I2C LCD displays setup information.

Example:

```text
SETUP MODE
Beszel_Recovery_A4F9
OPEN 192.168.4.1
WAITING...
```

If the display is smaller, pages may rotate:

```text
SETUP MODE
192.168.4.1
```

then:

```text
WiFi:
Recovery_A4F9
```

LCD failure must not stop the provisioning portal.

## 5. Buzzer behavior

When setup mode is ready, the buzzer plays a short non-blocking setup-ready pattern.

Suggested pattern:

```text
SHORT → SHORT → MEDIUM
```

The buzzer must not use blocking delays that stop the web portal or network stack.

## 6. Relay safety during provisioning

During setup mode:

```text
ALL RELAYS = SAFE / OPEN
```

No automatic recovery action is allowed.

Entering setup mode must never:

- Short-press a server power button.
- Hard-hold a server power button.
- Start a recovery sequence.
- Resume a previous volatile recovery action.

Automatic recovery can begin only after valid channel configuration, safe relay initialization, startup grace, and fresh server verification.

## 7. User onboarding flow

```text
Power ESP
   ↓
ESP creates Beszel_Recovery_XXXX
   ↓
Open Wi-Fi settings on phone/laptop
   ↓
Connect to ESP Wi-Fi
   ↓
Open browser
   ↓
192.168.4.1
   ↓
ESP Setup Portal
```

The setup portal must work without internet access.

Captive-portal redirection may be supported where practical.

If automatic captive portal opening fails, the user can manually open `192.168.4.1`.

## 8. First-time setup page

Suggested page:

```text
┌──────────────────────────────────────┐
│ Beszel Recovery Module               │
│ First-Time Setup                     │
├──────────────────────────────────────┤
│ Module ID                            │
│ esp-a4f912                           │
│                                      │
│ Wi-Fi Network                        │
│ [ Scan Networks ]                    │
│ [ HomeLab_IoT              ▼ ]        │
│                                      │
│ Wi-Fi Password                       │
│ [ •••••••••••••••••••••• ]          │
│                                      │
│ Module Name                          │
│ [ Rack A Recovery          ]          │
│                                      │
│ Beszel Hub Address                   │
│ [ 192.168.1.20             ]          │
│                                      │
│ Beszel Hub Port                      │
│ [ 8090                     ]          │
│                                      │
│ Registration Token                   │
│ [ Optional / if required   ]          │
│                                      │
│ [ SAVE & CONNECT ]                   │
└──────────────────────────────────────┘
```

The page must be responsive and usable from a phone.

## 9. Setup fields

Initial setup collects:

- Wi-Fi SSID
- Wi-Fi password
- Recovery Module display name
- Beszel Hub IP address or hostname
- Beszel Hub port
- Optional bootstrap/registration token

The ESP-generated Module ID is read-only.

Relay mapping and server recovery configuration do not need to be completed during initial Wi-Fi onboarding.

Those settings can be configured later through Beszel or the normal ESP local portal.

## 10. Wi-Fi scanning

The ESP scans nearby Wi-Fi networks.

Example:

```text
AVAILABLE NETWORKS

HomeLab_IoT       -42 dBm
Deco_Main         -55 dBm
JioFiber          -71 dBm
```

Selecting a network fills the SSID field.

Manual SSID entry remains available for hidden networks.

The scan operation must not block the web interface for an excessive period.

## 11. Save and connect flow

When the user selects `SAVE & CONNECT`:

```text
Validate form
   ↓
Save candidate configuration
   ↓
Attempt Wi-Fi connection
   │
   ├── FAILED
   │     ↓
   │   Show error
   │     ↓
   │   Restore/keep setup portal
   │
   └── SUCCESS
         ↓
       Obtain LAN IP
         ↓
       Test Beszel connectivity
         ↓
       Save valid Wi-Fi state
         ↓
       Restart / enter normal mode
```

The portal should display connection progress.

Example:

```text
CONNECTING

Wi-Fi: HomeLab_IoT

Please wait...
```

## 12. Successful Wi-Fi setup

After joining the LAN:

```text
SETUP COMPLETE

Wi-Fi: HomeLab_IoT
IP: 192.168.1.50

Module:
Rack A Recovery

Beszel:
CONNECTED

Device will restart...
```

LCD:

```text
WIFI CONNECTED
192.168.1.50
BESZEL CONNECTED
RESTARTING...
```

After restart, the ESP sends its mandatory online-status notification.

## 13. Invalid Wi-Fi password

A wrong password must not require firmware reflashing.

```text
Wi-Fi connection failed
        ↓
Do not mark setup complete
        ↓
Keep/restore setup AP
        ↓
Show connection error
```

Example:

```text
CONNECTION FAILED

Unable to connect to:
HomeLab_IoT

Check the Wi-Fi password
and try again.

[ BACK TO SETUP ]
```

Saved working credentials must not be destroyed by one temporary connection failure.

## 14. Beszel unavailable during setup

Wi-Fi and Beszel connectivity are separate states.

```text
Wi-Fi: CONNECTED
Beszel: UNREACHABLE
```

If Wi-Fi succeeds but Beszel is unavailable:

- Save the valid Wi-Fi configuration.
- Obtain and display the ESP LAN IP.
- Start normal local services.
- Continue standalone recovery behavior when safely configured.
- Retry Beszel connectivity in the background.
- Announce/register the module when Beszel becomes available.

The portal should display:

```text
Wi-Fi connected successfully.

Beszel Hub is currently unreachable.

The Recovery Module will continue locally
and retry the Beszel connection.
```

Beszel failure must not be reported as a Wi-Fi password failure.

## 15. Normal mode

After provisioning:

```text
ESP BOOT
   ↓
Load saved Wi-Fi
   ↓
Connect to LAN
   ↓
Receive LAN IP
   ↓
Initialize recovery services
   ↓
Start local web portal
   ↓
Connect and synchronize with Beszel
```

Example:

```text
Provisioning:
192.168.4.1

Normal LAN portal:
192.168.1.50
```

## 16. Normal ESP local web portal

The same ESP-hosted web interface remains available at the module's LAN IP.

The normal portal provides:

- Module status
- Module ID
- Firmware version
- Current IP address
- Wi-Fi state
- Beszel connection state
- Configuration sync state
- Channel mapping
- Server IP/hostname
- Probe ports
- Probe timings
- Relay timings
- Maintenance mode
- Gateway monitoring
- Temperature monitoring ON/OFF
- Temperature thresholds
- Buzzer settings

Changes made from this portal synchronize to Beszel.

Changes made in Beszel synchronize back to the ESP.

```text
Beszel UI
    ⇅
Configuration Sync
    ⇅
ESP Local Portal
```

## 17. Automatic discovery after setup

After joining the LAN, the ESP announces itself as an available Recovery Module.

```text
ESP joins LAN
   ↓
Gets IP
   ↓
Starts discovery advertisement
   ↓
Beszel detects ESP
   ↓
NEW RECOVERY MODULE
   ↓
Administrator approves
```

Example Beszel state:

```text
NEW RECOVERY MODULE

Module: Rack A Recovery
Module ID: esp-a4f912
IP: 192.168.1.50
Firmware: 0.1.0
Channels: 6

WAITING FOR APPROVAL

[ APPROVE ]
[ IGNORE ]
[ REJECT ]
```

Discovery does not automatically grant trusted control access.

## 18. Wi-Fi reconnect behavior

The automatic setup hotspot is a **first-time provisioning feature only**.

After the first successful setup, temporary or extended Wi-Fi loss does not automatically start the `192.168.4.1` setup portal.

```text
Wi-Fi lost
   ↓
Keep saved credentials
   ↓
Retry connection
   ↓
Continue safe local state handling
```

The ESP must not erase credentials after one failed reconnect.

Wi-Fi loss alone must never activate a relay.

## 19. Manual configuration reset after first setup

After first-time provisioning, the ESP does not automatically return to setup mode.

If Wi-Fi credentials, router, or network configuration change, the administrator must deliberately use the physical `CONFIG` button to reopen the provisioning access point.

Suggested behavior:

```text
Hold CONFIG button
for configured duration
        ↓
Enter setup mode
        ↓
Start Beszel_Recovery_XXXX
        ↓
Open 192.168.4.1
```

A short accidental press must not trigger setup mode.

The CONFIG button must never share recovery relay behavior.

## 20. Setup mode is not factory reset

Entering setup mode and factory reset are separate operations.

### Setup mode

```text
Reopen provisioning portal
Keep existing configuration
Allow network/settings changes
```

### Factory reset

```text
Explicit deliberate action
Erase provisioning configuration
Revoke or invalidate local integration state where required
Return device to first-boot state
```

Factory reset should require a separate long-press sequence or authenticated software action.

## 21. Security

The provisioning design must:

- Never expose Wi-Fi passwords in logs.
- Never return saved Wi-Fi passwords to Beszel.
- Never show the full saved password in the normal portal.
- Store secrets using the ESP platform's appropriate persistent storage.
- Use a bootstrap/registration token only when required.
- Avoid putting secrets in the SSID.
- Keep relay outputs safe during setup.
- Rate-limit sensitive setup actions where practical.

The setup AP should support a configurable provisioning password or device-specific onboarding credential for production use.

## 22. Provisioning state machine

```text
UNPROVISIONED
      │
      ▼
SETUP_AP_ACTIVE
      │
      ▼
SETUP_PORTAL_ACTIVE
      │
      ▼
CONFIG_VALIDATING
      │
      ├── invalid ──► SETUP_PORTAL_ACTIVE
      │
      ▼
WIFI_CONNECTING
      │
      ├── failed ───► SETUP_PORTAL_ACTIVE
      │
      ▼
WIFI_CONNECTED
      │
      ▼
BESZEL_CHECK
      │
      ├── unavailable ─► STANDALONE_NORMAL
      │
      └── available ───► REGISTRATION_PENDING
                              │
                              ▼
                         NORMAL_MODE
```

## 23. Acceptance tests

1. A new ESP with no configuration starts its setup access point.
2. The setup portal is available at `192.168.4.1`.
3. The portal works without internet access.
4. LCD shows setup mode and portal IP.
5. Buzzer plays the non-blocking setup-ready pattern.
6. All relay outputs remain safe/open during provisioning.
7. Nearby Wi-Fi networks can be scanned.
8. Hidden SSIDs can be entered manually.
9. Invalid Wi-Fi credentials return the user to setup.
10. Invalid Wi-Fi credentials do not require firmware reflashing.
11. Valid Wi-Fi credentials allow the ESP to join the LAN.
12. The assigned LAN IP is displayed after connection.
13. Beszel being offline does not invalidate valid Wi-Fi credentials.
14. The ESP retries Beszel connectivity in the background.
15. After setup, the local portal is available at the ESP LAN IP.
16. The ESP announces itself for Beszel discovery.
17. An unapproved ESP does not receive trusted management access.
18. Temporary Wi-Fi loss does not erase saved credentials.
19. Wi-Fi loss does not trigger a relay.
20. After successful first-time provisioning, normal reboots do not automatically start the setup hotspot.
21. Wi-Fi, router, or Beszel outages do not automatically return a provisioned ESP to `192.168.4.1`.
22. The CONFIG button deliberately reopens provisioning mode after first setup.
23. Entering setup mode does not factory reset the ESP.
22. Entering setup mode does not trigger a relay.
23. Factory reset requires a separate deliberate action.
24. ESP portal configuration changes synchronize to Beszel.
25. Beszel configuration changes synchronize to the ESP portal.

## 24. Final provisioning flow

```text
NEW ESP32
   ↓
Beszel_Recovery_XXXX Wi-Fi
   ↓
192.168.4.1
   ↓
Wi-Fi scan
   ↓
Select network
   ↓
Enter password
   ↓
Enter module name
   ↓
Enter Beszel Hub details
   ↓
SAVE & CONNECT
   ↓
ESP joins LAN
   ↓
ESP receives LAN IP
   ↓
ESP local portal moves to LAN IP
   ↓
Beszel discovers module
   ↓
Administrator approves
   ↓
Beszel ↔ ESP synchronization
   ↓
RECOVERY MODULE READY
```

The provisioning experience must remain simple: power the ESP, connect to its temporary Wi-Fi, open `192.168.4.1`, configure it, and continue.


## 25. Strict first-time-only provisioning rule

The automatic provisioning state transition is:

```text
NO VALID SAVED PROVISIONING
            ↓
FIRST-TIME SETUP
            ↓
START AP
            ↓
192.168.4.1
            ↓
SAVE VALID CONFIG
            ↓
PROVISIONED = TRUE
```

After `PROVISIONED = TRUE`:

```text
ESP BOOT
   ↓
Load saved configuration
   ↓
Try saved Wi-Fi
   │
   ├── Connected → NORMAL MODE
   │
   └── Failed
         ↓
      RETRY MODE
         ↓
      LCD / buzzer warning
         ↓
      Continue safe local behavior
         ↓
      DO NOT AUTO-START 192.168.4.1
```

The provisioning hotspot may only return after an explicit administrator action such as the physical `CONFIG` button or a deliberate factory reset.

```text
FIRST TIME          → AUTO SETUP AP
NORMAL REBOOT       → NO SETUP AP
WIFI DOWN           → NO SETUP AP
ROUTER DOWN         → NO SETUP AP
BESZEL DOWN         → NO SETUP AP
CONFIG BUTTON       → MANUAL SETUP AP
FACTORY RESET       → FIRST-TIME SETUP AP
```
