#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

// --- Configuration & Constants ---
#define FIRMWARE_VERSION "1.0.0"
#define MAX_CHANNELS_LIMIT 6
#define CONFIG_BUTTON_PIN 0 // standard boot button

// Pin settings
#define ONE_WIRE_BUS 4
#define BUZZER_PIN 12
const int RELAY_PINS[MAX_CHANNELS_LIMIT] = {18, 19, 25, 26, 27, 32};

LiquidCrystal_I2C lcd(0x27, 20, 4);
bool hasLCD = false;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Preferences preferences;
WebServer server(80);

// State tracking
enum ChannelState {
  STATE_ONLINE,
  STATE_VERIFYING_FAILURE,
  STATE_GRACEFUL_RECOVERY,
  STATE_SHORT_PRESS,
  STATE_BOOT_GRACE,
  STATE_HARD_HOLD,
  STATE_CRITICAL,
  STATE_COOLDOWN
};

struct MonitoredChannel {
  int channelNumber;
  char hostIP[64];
  int ports[3];
  int portCount;
  bool maintenance;
  // hwRecoveryDisabled gates autonomous SHORT_PRESS/HARD_HOLD escalation for
  // this channel. Unlike maintenance, this is a persistent policy set from
  // Beszel/the local portal, not a temporary human intervention. When true,
  // the channel keeps probing/logging its state but never presses the relay.
  bool hwRecoveryDisabled;
  // hubLockUntilMs is a best-effort hint reported by the hub (via
  // /recovery/ping) that a Beszel-initiated recovery action (e.g. automatic
  // WOL) is currently in flight for this channel. It is bounded by the ESP's
  // heartbeat interval and is not a real-time guarantee - see
  // processProberStateMachines()'s STATE_BOOT_GRACE handling.
  unsigned long hubLockUntilMs;
  ChannelState state;
  int consecutiveFailures;
  unsigned long lastProbeTime;
  unsigned long stateStartTime;
  int recoveryAttempts;
};

// Global memory cache
MonitoredChannel activeChannels[MAX_CHANNELS_LIMIT];
int activeChannelCount = 0;
bool isProvisioned = false;
char wifiSSID[64] = "";
char wifiPassword[64] = "";
char moduleName[64] = "Rack A Recovery";
char hubURL[128] = "";
int localConfigRevision = 0;
char localConfigHash[65] = "";

unsigned long heartbeatIntervalMs = 30000;
unsigned long lastPingTime = 0;
unsigned long lastDisplayPageRotation = 0;
int currentDisplayPage = 0;

// Startup grace: no channel may escalate past STATE_VERIFYING_FAILURE until
// this deadline, so a fresh boot (or watchdog reset) never fires a relay
// within seconds of coming up.
#define STARTUP_GRACE_MS 60000UL
unsigned long startupGraceUntilMs = 0;

// Gateway / NETWORK_VERIFY: distinguishes "one server is down" from "the
// network itself is down" so a router outage doesn't cause every channel to
// be hard-rebooted. This mirrors the hub's own gateway check, but runs
// locally so the ESP's autonomous engine stays safe even when Beszel is
// unreachable.
char gatewayIP[16] = "";
bool networkVerifyActive = false;
int gatewayConsecutiveOk = 0;
unsigned long lastGatewayProbeTime = 0;

// Button timers
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;

// Buzzer timing parameters
unsigned long buzzerPatternStartTime = 0;
int buzzerPatternBeeps = 0;
int buzzerPatternGap = 0;
bool buzzerState = false;

void triggerBuzzer(int beepCount, int durationMs);
void updateLCDDisplay();
void processProberStateMachines();
void syncWithHub();
void checkConfigButton();

// Setup HTML onboarding page
const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Beszel Recovery Onboarding</title>
<style>
body { font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif; background:#121214; color:#e4e4e7; margin:0; padding:20px; display:flex; justify-content:center; }
.card { background:#18181b; border:1px solid #27272a; padding:24px; border-radius:8px; width:100%; max-width:400px; box-shadow:0 4px 6px rgba(0,0,0,0.1); }
h2 { margin-top:0; color:#3b82f6; }
.field { margin-bottom:16px; }
label { display:block; font-size:12px; font-weight:600; text-transform:uppercase; color:#a1a1aa; margin-bottom:6px; }
input, select { width:100%; padding:10px; background:#27272a; border:1px solid #3f3f46; border-radius:6px; color:#fff; box-sizing:border-box; }
input:focus, select:focus { border-color:#3b82f6; outline:none; }
button { width:100%; background:#3b82f6; color:#fff; padding:12px; border:none; border-radius:6px; font-weight:600; cursor:pointer; }
button:hover { background:#2563eb; }
.scan-btn { background:#27272a; border:1px solid #3f3f46; color:#a1a1aa; padding:6px 12px; border-radius:4px; font-size:12px; margin-bottom:12px; cursor:pointer; }
.scan-btn:hover { background:#3f3f46; }
</style>
</head>
<body>
<div class='card'>
<h2>Beszel Recovery Setup</h2>
<div class='field'><label>Module ID (MAC)</label><input type='text' id='mac_id' disabled></div>
<button class='scan-btn' onclick='scanNetworks()'>Scan WiFi Networks</button>
<form method='POST' action='/api/setup/save'>
<div class='field'><label>WiFi Network (SSID)</label><select name='ssid' id='ssid_select'></select></div>
<div class='field'><label>WiFi Password</label><input type='password' name='password' placeholder='Enter Password' required></div>
<div class='field'><label>Module Name</label><input type='text' name='name' value='Rack A Recovery' required></div>
<div class='field'><label>Beszel Hub URL</label><input type='text' name='hub_url' placeholder='http://192.168.1.10:8090/api/beszel/recovery/ping' required></div>
<div class='field'><label>Heartbeat Interval (sec)</label><input type='number' name='heartbeat' value='30' min='5' max='300' required></div>
<button type='submit'>SAVE & CONNECT</button>
</form>
</div>
<script>
function scanNetworks() {
  var sel = document.getElementById('ssid_select');
  sel.innerHTML = '<option>Scanning...</option>';
  fetch('/api/wifi/scan').then(r => r.json()).then(data => {
    sel.innerHTML = '';
    data.forEach(n => {
      var opt = document.createElement('option');
      opt.value = n.ssid;
      opt.text = n.ssid + ' (' + n.rssi + ' dBm)';
      sel.appendChild(opt);
    });
  });
}
window.onload = function() {
  document.getElementById('mac_id').value = window.location.hostname;
  scanNetworks();
};
</script>
</body>
</html>
)rawliteral";

// --- Onboarding Setup Web Endpoints ---
void handleGetSetup() {
  server.send(200, "text/html", SETUP_HTML);
}

void handleWifiScan() {
  int n = WiFi.scanNetworks();
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject item = arr.createNestedObject();
    item["ssid"] = WiFi.SSID(i);
    item["rssi"] = WiFi.RSSI(i);
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSaveSetup() {
  String ssid = server.arg("ssid");
  String pass = server.arg("password");
  String name = server.arg("name");
  String hub = server.arg("hub_url");
  int heartbeat = server.arg("heartbeat").toInt();
  if (heartbeat < 5) heartbeat = 5;

  preferences.begin("watchdog", false);
  preferences.putInt("heartbeat", heartbeat * 1000);
  preferences.putBool("provisioned", true);
  preferences.putString("ssid", ssid);
  preferences.putString("password", pass);
  preferences.putString("name", name);
  preferences.putString("hub_url", hub);
  preferences.putInt("revision", 0);
  preferences.putString("hash", "");
  preferences.end();

  server.send(200, "text/html", "<html><body><h3>Onboarding parameters saved! Restarting...</h3></body></html>");
  delay(2000);
  ESP.restart();
}

void handleRelayTrigger() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
    return;
  }
  StaticJsonDocument<256> doc;
  deserializeJson(doc, server.arg("plain"));
  int targetChannel = doc["channel"];
  int durationMs = doc["pulse_duration_ms"];

  if (targetChannel < 1 || targetChannel > MAX_CHANNELS_LIMIT) {
    server.send(400, "application/json", "{\"error\":\"Invalid channel\"}");
    return;
  }

  int pin = RELAY_PINS[targetChannel - 1];
  digitalWrite(pin, LOW);
  delay(durationMs);
  digitalWrite(pin, HIGH);
  triggerBuzzer(1, 200);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// --- Setup Access Point Mode ---
void startProvisioningAP() {
  WiFi.mode(WIFI_AP);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 4);
  String apSSID = "Beszel_Recovery_" + suffix;

  WiFi.softAP(apSSID.c_str());

  if (hasLCD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SETUP ACTIVE");
    lcd.setCursor(0, 1);
    lcd.print(apSSID);
    lcd.setCursor(0, 2);
    lcd.print("IP: 192.168.4.1");
  }
  triggerBuzzer(3, 100);

  server.on("/", HTTP_GET, handleGetSetup);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/setup/save", HTTP_POST, handleSaveSetup);
  server.begin();
}

void handleNormalStatus() {
  String html = "<html><head><title>Recovery Status</title><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body style='font-family:sans-serif; background:#121214; color:#e4e4e7; padding:20px;'>";
  html += "<h2>Recovery Module Active</h2>";
  html += "<p><b>IP Address:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Hub URL:</b> " + String(hubURL) + "</p>";
  html += "<p><b>Heartbeat Interval:</b> " + String(heartbeatIntervalMs / 1000) + "s</p>";
  html += "<p><b>Monitored Channels:</b> " + String(activeChannelCount) + "</p>";
  html += "<p><b>Config Revision:</b> " + String(localConfigRevision) + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

  for (int i = 0; i < MAX_CHANNELS_LIMIT; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH);
  }
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin();
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    hasLCD = true;
    lcd.init();
    lcd.backlight();
    lcd.clear();
    Serial.println("LCD initialized.");
  } else {
    Serial.println("LCD not found. Running headless.");
  }

  sensors.begin();

  // Read saved credentials
  preferences.begin("watchdog", true);
  isProvisioned = preferences.getBool("provisioned", false);
  if (isProvisioned) {
    strncpy(wifiSSID, preferences.getString("ssid", "").c_str(), sizeof(wifiSSID) - 1);
    strncpy(wifiPassword, preferences.getString("password", "").c_str(), sizeof(wifiPassword) - 1);
    strncpy(moduleName, preferences.getString("name", "Rack A Recovery").c_str(), sizeof(moduleName) - 1);
    strncpy(hubURL, preferences.getString("hub_url", "").c_str(), sizeof(hubURL) - 1);
    heartbeatIntervalMs = preferences.getInt("heartbeat", 30000);
    localConfigRevision = preferences.getInt("revision", 0);
    strncpy(localConfigHash, preferences.getString("hash", "").c_str(), sizeof(localConfigHash) - 1);
  }
  preferences.end();

  if (!isProvisioned) {
    startProvisioningAP();
  } else {
    // Normal operation boot
    WiFi.begin(wifiSSID, wifiPassword);
    if (hasLCD) {
      lcd.setCursor(0, 0);
      lcd.print("Connecting WiFi...");
      lcd.setCursor(0, 1);
      lcd.print(wifiSSID);
    }

    int count = 0;
    while (WiFi.status() != WL_CONNECTED && count < 15) {
      delay(500);
      count++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      WiFi.gatewayIP().toString().toCharArray(gatewayIP, sizeof(gatewayIP));
      if (hasLCD) {
        lcd.clear();
        lcd.print("WiFi Online");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP().toString());
      }
      triggerBuzzer(2, 100);
    } else {
      if (hasLCD) {
        lcd.clear();
        lcd.print("WiFi Timeout");
        lcd.setCursor(0, 1);
        lcd.print("Retrying background");
      }
    }

    // Safe relay initialization already happened above (all pins HIGH/open).
    // Hold off any autonomous recovery action until fresh verification has
    // had a chance to run, so a boot/reset never presses a relay within
    // seconds of coming up.
    startupGraceUntilMs = millis() + STARTUP_GRACE_MS;

    server.on("/", HTTP_GET, handleNormalStatus);
    server.on("/api/relay/trigger", HTTP_POST, handleRelayTrigger);
    server.begin();
    delay(1000);
  }
}

void loop() {
  server.handleClient();
  checkConfigButton();

  if (isProvisioned) {
    processProberStateMachines();
    syncWithHub();
    updateLCDDisplay();
  }

  // Non-blocking buzzer scheduling
  if (buzzerPatternBeeps > 0) {
    unsigned long now = millis();
    if (now - buzzerPatternStartTime >= buzzerPatternGap) {
      buzzerPatternStartTime = now;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      if (!buzzerState) {
        buzzerPatternBeeps--;
      }
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void checkConfigButton() {
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    if (!buttonWasPressed) {
      buttonWasPressed = true;
      buttonPressStartTime = millis();
    } else {
      // 3 seconds long-press triggers setup config AP reopening
      if (millis() - buttonPressStartTime >= 3000) {
        preferences.begin("watchdog", false);
        preferences.putBool("provisioned", false);
        preferences.end();
        if (hasLCD) {
          lcd.clear();
          lcd.print("Reset Config...");
        }
        triggerBuzzer(4, 80);
        delay(1500);
        ESP.restart();
      }
    }
  } else {
    buttonWasPressed = false;
  }
}

void triggerBuzzer(int beepCount, int durationMs) {
  buzzerPatternBeeps = beepCount;
  buzzerPatternGap = durationMs;
  buzzerPatternStartTime = millis();
  buzzerState = false;
}

bool probeTCPPort(const char* ip, int port) {
  WiFiClient client;
  client.setTimeout(1200);
  if (client.connect(ip, port)) {
    client.stop();
    return true;
  }
  return false;
}

// probeGateway checks whether the local network gateway is reachable, using
// the same TCP-connect approach (ports 53/80) as the hub's own gateway
// check. If no gateway IP is known yet, treat it as reachable rather than
// blocking escalation on missing configuration.
bool probeGateway() {
  if (strlen(gatewayIP) == 0) return true;
  if (probeTCPPort(gatewayIP, 53)) return true;
  if (probeTCPPort(gatewayIP, 80)) return true;
  return false;
}

// checkNetworkVerifyRecovery re-probes the gateway while NETWORK_VERIFY is
// active and only clears it after a couple of consecutive successful
// probes, to avoid flapping back into automatic recovery on a still-shaky
// connection.
void checkNetworkVerifyRecovery() {
  if (!networkVerifyActive) return;
  unsigned long now = millis();
  if (now - lastGatewayProbeTime < 5000) return;
  lastGatewayProbeTime = now;

  if (probeGateway()) {
    gatewayConsecutiveOk++;
    if (gatewayConsecutiveOk >= 2) {
      networkVerifyActive = false;
      gatewayConsecutiveOk = 0;
    }
  } else {
    gatewayConsecutiveOk = 0;
  }
}

void processProberStateMachines() {
  unsigned long now = millis();
  checkNetworkVerifyRecovery();
  for (int i = 0; i < activeChannelCount; i++) {
    MonitoredChannel& ch = activeChannels[i];
    if (ch.maintenance) {
      ch.state = STATE_ONLINE;
      continue;
    }

    unsigned long interval = (ch.state == STATE_VERIFYING_FAILURE) ? 2000 : 5000;
    if (now - ch.lastProbeTime >= interval) {
      ch.lastProbeTime = now;
      bool hostResponded = false;
      for (int p = 0; p < ch.portCount; p++) {
        if (probeTCPPort(ch.hostIP, ch.ports[p])) {
          hostResponded = true;
          break;
        }
      }

      switch (ch.state) {
        case STATE_ONLINE:
          if (!hostResponded) {
            ch.state = STATE_VERIFYING_FAILURE;
            ch.consecutiveFailures = 1;
            ch.stateStartTime = now;
            triggerBuzzer(1, 100);
          }
          break;

        case STATE_VERIFYING_FAILURE:
          if (hostResponded) {
            ch.state = STATE_ONLINE;
            ch.consecutiveFailures = 0;
          } else {
            ch.consecutiveFailures++;
            if (ch.consecutiveFailures >= 3) {
              // Startup grace: keep verifying/logging, but never act within
              // STARTUP_GRACE_MS of boot.
              if (now < startupGraceUntilMs) {
                break;
              }
              // Per-channel policy: this channel is monitoring-only.
              if (ch.hwRecoveryDisabled) {
                break;
              }
              // Already known network-down - don't re-probe every tick.
              if (networkVerifyActive) {
                break;
              }
              if (!probeGateway()) {
                // Gateway itself is unreachable: this looks like a network
                // outage, not N simultaneous server failures. Suspend
                // automatic recovery network-wide until it recovers.
                networkVerifyActive = true;
                gatewayConsecutiveOk = 0;
                lastGatewayProbeTime = now;
                break;
              }
              ch.state = STATE_SHORT_PRESS;
              ch.stateStartTime = now;
              ch.recoveryAttempts++;
              int pin = RELAY_PINS[ch.channelNumber - 1];
              digitalWrite(pin, LOW);
              delay(300);
              digitalWrite(pin, HIGH);
              triggerBuzzer(3, 150);
            }
          }
          break;

        case STATE_SHORT_PRESS:
          ch.state = STATE_BOOT_GRACE;
          ch.stateStartTime = now;
          break;

        case STATE_BOOT_GRACE:
          if (hostResponded) {
            ch.state = STATE_ONLINE;
            ch.consecutiveFailures = 0;
            ch.recoveryAttempts = 0;
            triggerBuzzer(2, 200);
          } else {
            if (now - ch.stateStartTime >= 60000) {
              // Hard-hold is destructive (it can power OFF a machine that a
              // hub-initiated WOL just booted), so it gets an extra, more
              // conservative gate than the short-press escalation above.
              bool hubLockFresh = (ch.hubLockUntilMs != 0 && now < ch.hubLockUntilMs);
              if (ch.hwRecoveryDisabled || hubLockFresh || networkVerifyActive) {
                // Defer, don't cancel: stay in BOOT_GRACE and re-check next
                // cycle instead of escalating to a destructive hard-hold.
                ch.stateStartTime = now;
                break;
              }
              if (ch.recoveryAttempts < 2) {
                ch.state = STATE_HARD_HOLD;
                ch.stateStartTime = now;
                ch.recoveryAttempts++;
                int pin = RELAY_PINS[ch.channelNumber - 1];
                digitalWrite(pin, LOW);
                delay(8000);
                digitalWrite(pin, HIGH);
                delay(1500);
                digitalWrite(pin, LOW);
                delay(300);
                digitalWrite(pin, HIGH);
                triggerBuzzer(4, 150);
              } else {
                ch.state = STATE_CRITICAL;
                ch.stateStartTime = now;
              }
            }
          }
          break;

        case STATE_HARD_HOLD:
          ch.state = STATE_BOOT_GRACE;
          ch.stateStartTime = now;
          break;

        case STATE_CRITICAL:
          if (hostResponded) {
            ch.state = STATE_ONLINE;
            ch.consecutiveFailures = 0;
            ch.recoveryAttempts = 0;
          } else {
            if (now - ch.stateStartTime >= 300000) {
              ch.state = STATE_ONLINE;
              ch.consecutiveFailures = 0;
              ch.recoveryAttempts = 0;
            }
          }
          break;
      }
    }
  }
}

void syncWithHub() {
  unsigned long now = millis();
  if (now - lastPingTime < heartbeatIntervalMs && lastPingTime != 0) {
    return;
  }
  lastPingTime = now;

  if (WiFi.status() != WL_CONNECTED) {
    // Retry connection in background silently
    if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_CONNECTION_LOST || WiFi.status() == WL_DISCONNECTED) {
      WiFi.begin(wifiSSID, wifiPassword);
    }
    return;
  }

  sensors.requestTemperatures();
  float currentTemp = sensors.getTempCByIndex(0);
  if (currentTemp == DEVICE_DISCONNECTED_C) {
    currentTemp = 0.0f;
  }

  HTTPClient http;
  http.begin(hubURL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["mac_address"] = WiFi.macAddress();
  doc["ip_address"] = WiFi.localIP().toString();
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["max_channels"] = MAX_CHANNELS_LIMIT;
  doc["config_revision"] = localConfigRevision;
  doc["config_hash"] = localConfigHash;
  if (currentTemp > 0) {
    doc["temperature"] = currentTemp;
  }

  String requestBody;
  serializeJson(doc, requestBody);

  int httpCode = http.POST(requestBody);
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    // Sized generously for MAX_CHANNELS_LIMIT channels now that the response
    // carries per-channel hardware_recovery_disabled/hub_lock_* hints in
    // addition to the original fields.
    StaticJsonDocument<2048> respDoc;
    DeserializationError err = deserializeJson(respDoc, response);

    if (!err) {
      int remoteRevision = respDoc["config_revision"];
      String remoteHash = respDoc["config_hash"];

      if (remoteRevision > localConfigRevision && remoteHash != localConfigHash) {
        JsonArray channelsArray = respDoc["channels"];
        activeChannelCount = 0;

        for (JsonObject item : channelsArray) {
          if (activeChannelCount >= MAX_CHANNELS_LIMIT) break;

          MonitoredChannel& newCh = activeChannels[activeChannelCount];
          newCh.channelNumber = item["channel"];
          const char* host = item["host_ip"];
          strncpy(newCh.hostIP, host, sizeof(newCh.hostIP) - 1);
          newCh.maintenance = item["maintenance"];
          newCh.hwRecoveryDisabled = item["hardware_recovery_disabled"] | false;
          long lockSecondsRemaining = item["hub_lock_seconds_remaining"] | 0;
          newCh.hubLockUntilMs = (lockSecondsRemaining > 0)
            ? (millis() + (unsigned long)lockSecondsRemaining * 1000UL)
            : 0;
          newCh.state = STATE_ONLINE;
          newCh.consecutiveFailures = 0;
          newCh.lastProbeTime = 0;
          newCh.recoveryAttempts = 0;

          JsonArray ports = item["ports"];
          newCh.portCount = 0;
          for (int p : ports) {
            if (newCh.portCount < 3) {
              newCh.ports[newCh.portCount++] = p;
            }
          }
          activeChannelCount++;
        }

        preferences.begin("watchdog", false);
        preferences.putInt("revision", remoteRevision);
        preferences.putString("hash", remoteHash);
        preferences.end();

        localConfigRevision = remoteRevision;
        strncpy(localConfigHash, remoteHash.c_str(), sizeof(localConfigHash) - 1);
        triggerBuzzer(3, 100);
      } else {
        // Config revision unchanged, but volatile per-channel hints (hub
        // lock, maintenance, hardware-recovery-disabled) can legitimately
        // change between config revisions - refresh those on every ping
        // without rebuilding channels or resetting probe state/counters.
        JsonArray channelsArray = respDoc["channels"];
        for (JsonObject item : channelsArray) {
          int chanNum = item["channel"];
          for (int i = 0; i < activeChannelCount; i++) {
            if (activeChannels[i].channelNumber != chanNum) continue;
            activeChannels[i].maintenance = item["maintenance"];
            activeChannels[i].hwRecoveryDisabled = item["hardware_recovery_disabled"] | false;
            long lockSecondsRemaining = item["hub_lock_seconds_remaining"] | 0;
            activeChannels[i].hubLockUntilMs = (lockSecondsRemaining > 0)
              ? (millis() + (unsigned long)lockSecondsRemaining * 1000UL)
              : 0;
            break;
          }
        }
      }
    }
  }
  http.end();
}

void updateLCDDisplay() {
  if (!hasLCD) return;

  unsigned long now = millis();
  if (now - lastDisplayPageRotation < 4000) {
    return;
  }
  lastDisplayPageRotation = now;

  lcd.clear();

  if (networkVerifyActive) {
    lcd.setCursor(0, 0);
    lcd.print("NETWORK VERIFY");
    lcd.setCursor(0, 1);
    lcd.print("GATEWAY: OFFLINE");
    lcd.setCursor(0, 2);
    lcd.print("RECOVERY PAUSED");
    return;
  }

  if (activeChannelCount == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Recovery Watchdog");
    lcd.setCursor(0, 1);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP().toString());
    lcd.setCursor(0, 2);
    lcd.print("Status: Protected");
    lcd.setCursor(0, 3);
    lcd.print("Rev: ");
    lcd.print(localConfigRevision);
    return;
  }

  int itemsPerPage = 3;
  int startIdx = currentDisplayPage * itemsPerPage;

  lcd.setCursor(0, 0);
  lcd.print("Channels (Page ");
  lcd.print(currentDisplayPage + 1);
  lcd.print(")");

  for (int i = 0; i < itemsPerPage; i++) {
    int idx = startIdx + i;
    if (idx >= activeChannelCount) break;

    MonitoredChannel& ch = activeChannels[idx];
    lcd.setCursor(0, i + 1);
    lcd.print("CH");
    lcd.print(ch.channelNumber);
    lcd.print(" ");
    lcd.print(ch.hostIP);
    lcd.setCursor(15, i + 1);

    switch (ch.state) {
      case STATE_ONLINE: lcd.print("OK"); break;
      case STATE_VERIFYING_FAILURE: lcd.print("VERIFY"); break;
      case STATE_SHORT_PRESS:
      case STATE_HARD_HOLD: lcd.print("PRESS"); break;
      case STATE_BOOT_GRACE: lcd.print("BOOT"); break;
      case STATE_CRITICAL: lcd.print("CRIT"); break;
      default: lcd.print("WARN"); break;
    }
  }
  currentDisplayPage = (currentDisplayPage + 1) % ((activeChannelCount + itemsPerPage - 1) / itemsPerPage);
}
