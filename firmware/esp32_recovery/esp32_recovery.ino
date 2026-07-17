#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
// Raw ICMP socket support for icmpPing() - host reachability is checked by
// pinging the IP directly instead of probing TCP ports.
#include <lwip/sockets.h>
#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp.h>

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
  // failureThreshold/bootGraceSeconds are per-channel timing profiles
  // (Beszel or this device's own local portal can edit them) - previously
  // hardcoded to 3 and 60s regardless of what was configured.
  int failureThreshold;
  int bootGraceSeconds;
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
// unreachable. gatewayIP starts auto-detected from DHCP but can be
// overridden by a local or Beszel-driven settings change.
char gatewayIP[16] = "";
char gatewayName[64] = "";
bool networkVerifyActive = false;
int gatewayConsecutiveOk = 0;
unsigned long lastGatewayProbeTime = 0;

// Local settings portal state - module-level settings editable either from
// this device's own web page or from Beszel, kept in sync via syncWithHub().
bool temperatureMonitoringDisabled = false;
float tempThresholdWarningLocal = 50.0;
float tempThresholdCriticalLocal = 60.0;
bool buzzerDisabled = false;
bool buzzerMuted = false;

// Bidirectional config sync: set when this device's own local web portal
// just applied a settings edit, so the next ping reports it as a
// local_change. Cleared once the hub's response confirms the new revision
// was accepted, or leaves it set (so the change keeps being reported) if
// the hub instead flags a conflict - see syncWithHub().
bool pendingLocalChange = false;
int localChangeBaseRevision = 0;

// Connectivity state surfaced on the local dashboard.
bool lastPingSuccess = false;

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
void markLocalChange();

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

// Normal-mode local dashboard/settings portal. Static markup - all values
// are populated client-side from GET /api/state, and edits are saved via
// POST /api/settings/save and POST /api/channel/save. This is the "ESP
// local web portal" from the spec: settings changed here flow back to
// Beszel via syncWithHub()'s local_change reporting, just as Beszel-side
// changes flow down via the regular ping response.
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Beszel Recovery Module</title>
<style>
body { font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif; background:#121214; color:#e4e4e7; margin:0; padding:20px; }
.card { background:#18181b; border:1px solid #27272a; padding:20px; border-radius:8px; max-width:600px; margin:0 auto 16px; }
h2 { margin-top:0; color:#3b82f6; font-size:18px; }
h3 { color:#a1a1aa; font-size:12px; text-transform:uppercase; margin:0 0 12px; letter-spacing:0.05em; }
.row { display:flex; justify-content:space-between; align-items:center; padding:5px 0; font-size:13px; border-bottom:1px solid #27272a; }
.row span:first-child { color:#a1a1aa; }
.badge { padding:2px 8px; border-radius:4px; font-size:11px; font-weight:600; margin-left:6px; }
.ok { background:#14532d; color:#4ade80; }
.warn { background:#713f12; color:#facc15; }
.crit { background:#7f1d1d; color:#f87171; }
.field { margin-bottom:12px; }
label { display:block; font-size:11px; font-weight:600; text-transform:uppercase; color:#a1a1aa; margin-bottom:4px; }
label.inline { display:flex; align-items:center; font-weight:600; }
input[type=text], input[type=number] { width:100%; padding:8px; background:#27272a; border:1px solid #3f3f46; border-radius:6px; color:#fff; box-sizing:border-box; }
input[type=checkbox] { margin-right:8px; width:auto; }
button { background:#3b82f6; color:#fff; padding:8px 16px; border:none; border-radius:6px; font-weight:600; cursor:pointer; font-size:13px; }
button:hover { background:#2563eb; }
.chan { border:1px solid #27272a; border-radius:6px; padding:12px; margin-bottom:10px; }
.chan-head { display:flex; justify-content:space-between; align-items:center; margin-bottom:10px; }
.grid2 { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
</style>
</head>
<body>
<div class='card'>
  <h2 id='moduleTitle'>Recovery Module</h2>
  <div id='statusRows'></div>
</div>
<div class='card'>
  <h3>Module Settings</h3>
  <div class='field'><label>Module Name</label><input type='text' id='f_name'></div>
  <div class='grid2'>
    <div class='field'><label>Gateway IP</label><input type='text' id='f_gwip' placeholder='blank = auto-detect'></div>
    <div class='field'><label>Gateway Name</label><input type='text' id='f_gwname'></div>
  </div>
  <div class='field'><label>Ping Interval (seconds)</label><input type='number' id='f_ping' min='5'></div>
  <div class='field'><label class='inline'><input type='checkbox' id='f_tempdis'> Disable Temperature Monitoring</label></div>
  <div class='grid2'>
    <div class='field'><label>Warn Threshold (&deg;C)</label><input type='number' id='f_tempwarn'></div>
    <div class='field'><label>Critical Threshold (&deg;C)</label><input type='number' id='f_tempcrit'></div>
  </div>
  <div class='field'><label class='inline'><input type='checkbox' id='f_buzzdis'> Disable Buzzer</label></div>
  <div class='field'><label class='inline'><input type='checkbox' id='f_buzzmute'> Mute Buzzer Temporarily</label></div>
  <button onclick='saveSettings()'>Save Module Settings</button>
</div>
<div class='card'>
  <h3>Channels</h3>
  <div id='channelsList'></div>
</div>
<script>
function badge(text, cls) { return "<span class='badge " + cls + "'>" + text + "</span>"; }
function esc(v) { return (v === undefined || v === null) ? '' : String(v).replace(/'/g, '&#39;'); }
function loadState() {
  fetch('/api/state').then(function(r){ return r.json(); }).then(function(data) {
    document.getElementById('moduleTitle').textContent = data.name || 'Recovery Module';
    var rows = '';
    rows += "<div class='row'><span>Module ID</span><span>" + esc(data.module_id) + "</span></div>";
    rows += "<div class='row'><span>Firmware</span><span>" + esc(data.firmware_version) + "</span></div>";
    rows += "<div class='row'><span>IP Address</span><span>" + esc(data.ip) + "</span></div>";
    rows += "<div class='row'><span>Uptime</span><span>" + data.uptime_seconds + "s</span></div>";
    rows += "<div class='row'><span>Beszel Connection</span><span>" + (data.beszel_connected ? badge('CONNECTED','ok') : badge('UNREACHABLE','warn')) + "</span></div>";
    rows += "<div class='row'><span>Gateway</span><span>" + esc(data.gateway_ip || 'N/A') + " " + (data.gateway_online ? badge('ONLINE','ok') : badge('OFFLINE','crit')) + "</span></div>";
    rows += "<div class='row'><span>Temperature</span><span>" + (data.temperature_monitoring_disabled ? badge('DISABLED','warn') : (Number(data.temperature).toFixed(1) + '&deg;C')) + "</span></div>";
    rows += "<div class='row'><span>Config Revision</span><span>" + data.config_revision + (data.sync_pending ? badge('SYNC PENDING','warn') : badge('SYNCED','ok')) + "</span></div>";
    document.getElementById('statusRows').innerHTML = rows;

    document.getElementById('f_name').value = data.name || '';
    document.getElementById('f_gwip').value = data.gateway_ip || '';
    document.getElementById('f_gwname').value = data.gateway_name || '';
    document.getElementById('f_ping').value = data.ping_interval_seconds || 30;
    document.getElementById('f_tempdis').checked = !!data.temperature_monitoring_disabled;
    document.getElementById('f_tempwarn').value = data.temp_threshold_warning || 50;
    document.getElementById('f_tempcrit').value = data.temp_threshold_critical || 60;
    document.getElementById('f_buzzdis').checked = !!data.buzzer_disabled;
    document.getElementById('f_buzzmute').checked = !!data.buzzer_muted;

    var chHtml = '';
    (data.channels || []).forEach(function(ch) {
      var cls = ch.state_label === 'OK' ? 'ok' : (ch.state_label === 'CRIT' ? 'crit' : 'warn');
      chHtml += "<div class='chan'>";
      chHtml += "<div class='chan-head'><b>Channel " + ch.channel + "</b>" + badge(ch.state_label, cls) + "</div>";
      chHtml += "<div class='field'><label>Host IP (pinged directly)</label><input type='text' id='ch_host_" + ch.channel + "' value='" + esc(ch.host_ip) + "'></div>";
      chHtml += "<div class='field'><label>Failure Threshold</label><input type='number' id='ch_thresh_" + ch.channel + "' value='" + ch.failure_threshold + "'></div>";
      chHtml += "<div class='field'><label>Boot Grace (seconds)</label><input type='number' id='ch_grace_" + ch.channel + "' value='" + ch.boot_grace_seconds + "'></div>";
      chHtml += "<div class='field'><label class='inline'><input type='checkbox' id='ch_maint_" + ch.channel + "' " + (ch.maintenance ? 'checked' : '') + "> Maintenance Mode</label></div>";
      chHtml += "<div class='field'><label class='inline'><input type='checkbox' id='ch_hwdis_" + ch.channel + "' " + (ch.hardware_recovery_disabled ? 'checked' : '') + "> Disable Autonomous Recovery</label></div>";
      chHtml += "<button onclick='saveChannel(" + ch.channel + ")'>Save Channel " + ch.channel + "</button>";
      chHtml += "</div>";
    });
    document.getElementById('channelsList').innerHTML = chHtml || "<p style='color:#a1a1aa;font-size:13px;'>No channels configured yet.</p>";
  });
}
function saveSettings() {
  var body = {
    name: document.getElementById('f_name').value,
    gateway_ip: document.getElementById('f_gwip').value,
    gateway_name: document.getElementById('f_gwname').value,
    ping_interval_seconds: parseInt(document.getElementById('f_ping').value, 10) || 30,
    temperature_monitoring_disabled: document.getElementById('f_tempdis').checked,
    temp_threshold_warning: parseFloat(document.getElementById('f_tempwarn').value) || 50,
    temp_threshold_critical: parseFloat(document.getElementById('f_tempcrit').value) || 60,
    buzzer_disabled: document.getElementById('f_buzzdis').checked,
    buzzer_muted: document.getElementById('f_buzzmute').checked
  };
  fetch('/api/settings/save', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
    .then(function(){ loadState(); });
}
function saveChannel(chan) {
  var body = {
    channel: chan,
    host_ip: document.getElementById('ch_host_' + chan).value,
    failure_threshold: parseInt(document.getElementById('ch_thresh_' + chan).value, 10) || 3,
    boot_grace_seconds: parseInt(document.getElementById('ch_grace_' + chan).value, 10) || 60,
    maintenance: document.getElementById('ch_maint_' + chan).checked,
    hardware_recovery_disabled: document.getElementById('ch_hwdis_' + chan).checked
  };
  fetch('/api/channel/save', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
    .then(function(){ loadState(); });
}
loadState();
setInterval(loadState, 10000);
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

void handleDashboard() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

// channelStateLabel mirrors updateLCDDisplay()'s short labels, reused here
// for the dashboard's channel status badges.
const char* channelStateLabel(ChannelState state) {
  switch (state) {
    case STATE_ONLINE: return "OK";
    case STATE_VERIFYING_FAILURE: return "VERIFY";
    case STATE_SHORT_PRESS:
    case STATE_HARD_HOLD: return "PRESS";
    case STATE_BOOT_GRACE: return "BOOT";
    case STATE_CRITICAL: return "CRIT";
    default: return "WARN";
  }
}

// handleGetState handles GET /api/state, returning the module/channel state
// the dashboard page renders and pre-fills its settings forms from.
void handleGetState() {
  StaticJsonDocument<2048> doc;
  doc["module_id"] = WiFi.macAddress();
  doc["name"] = moduleName;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime_seconds"] = millis() / 1000;
  doc["beszel_connected"] = lastPingSuccess;
  doc["gateway_ip"] = gatewayIP;
  doc["gateway_name"] = gatewayName;
  doc["gateway_online"] = !networkVerifyActive;
  doc["temperature_monitoring_disabled"] = temperatureMonitoringDisabled;

  float temp = sensors.getTempCByIndex(0);
  doc["temperature"] = (temp == DEVICE_DISCONNECTED_C) ? 0.0 : temp;
  doc["temp_threshold_warning"] = tempThresholdWarningLocal;
  doc["temp_threshold_critical"] = tempThresholdCriticalLocal;
  doc["buzzer_disabled"] = buzzerDisabled;
  doc["buzzer_muted"] = buzzerMuted;
  doc["ping_interval_seconds"] = heartbeatIntervalMs / 1000;
  doc["config_revision"] = localConfigRevision;
  doc["sync_pending"] = pendingLocalChange;

  JsonArray channels = doc.createNestedArray("channels");
  for (int i = 0; i < activeChannelCount; i++) {
    MonitoredChannel& ch = activeChannels[i];
    JsonObject chObj = channels.createNestedObject();
    chObj["channel"] = ch.channelNumber;
    chObj["host_ip"] = ch.hostIP;
    chObj["failure_threshold"] = ch.failureThreshold;
    chObj["boot_grace_seconds"] = ch.bootGraceSeconds;
    chObj["maintenance"] = ch.maintenance;
    chObj["hardware_recovery_disabled"] = ch.hwRecoveryDisabled;
    chObj["state_label"] = channelStateLabel(ch.state);
    JsonArray ports = chObj.createNestedArray("ports");
    for (int p = 0; p < ch.portCount; p++) {
      ports.add(ch.ports[p]);
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// handleSaveSettings handles POST /api/settings/save - module-level settings
// edited from this device's own dashboard. Applies immediately and reports
// the change to the hub on the next ping via markLocalChange().
void handleSaveSettings() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
    return;
  }
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char* name = doc["name"] | "";
  if (name[0] != '\0') strncpy(moduleName, name, sizeof(moduleName) - 1);
  const char* gwip = doc["gateway_ip"] | "";
  strncpy(gatewayIP, gwip, sizeof(gatewayIP) - 1);
  const char* gwname = doc["gateway_name"] | "";
  strncpy(gatewayName, gwname, sizeof(gatewayName) - 1);

  long pingInterval = doc["ping_interval_seconds"] | 30;
  if (pingInterval < 5) pingInterval = 5;
  heartbeatIntervalMs = pingInterval * 1000UL;

  temperatureMonitoringDisabled = doc["temperature_monitoring_disabled"] | false;
  tempThresholdWarningLocal = doc["temp_threshold_warning"] | 50.0;
  tempThresholdCriticalLocal = doc["temp_threshold_critical"] | 60.0;
  buzzerDisabled = doc["buzzer_disabled"] | false;
  buzzerMuted = doc["buzzer_muted"] | false;

  markLocalChange();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// handleSaveChannel handles POST /api/channel/save - a single channel's
// settings edited from this device's own dashboard. Note: unlike module
// settings, per-channel edits are only kept in memory (not persisted to
// NVS) - a reboot before the next successful hub sync reverts to whatever
// channel config the hub last pushed. This is an accepted limitation of
// this first version rather than building structured array persistence on
// top of ESP32 Preferences' flat key-value store.
void handleSaveChannel() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
    return;
  }
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  int chanNum = doc["channel"] | 0;
  MonitoredChannel* target = nullptr;
  for (int i = 0; i < activeChannelCount; i++) {
    if (activeChannels[i].channelNumber == chanNum) {
      target = &activeChannels[i];
      break;
    }
  }
  if (target == nullptr) {
    server.send(404, "application/json", "{\"error\":\"Channel not found\"}");
    return;
  }

  const char* host = doc["host_ip"] | "";
  if (host[0] != '\0') strncpy(target->hostIP, host, sizeof(target->hostIP) - 1);
  target->failureThreshold = doc["failure_threshold"] | target->failureThreshold;
  target->bootGraceSeconds = doc["boot_grace_seconds"] | target->bootGraceSeconds;
  target->maintenance = doc["maintenance"] | target->maintenance;
  target->hwRecoveryDisabled = doc["hardware_recovery_disabled"] | target->hwRecoveryDisabled;

  if (doc.containsKey("probe_ports")) {
    JsonArray ports = doc["probe_ports"];
    target->portCount = 0;
    for (JsonVariant p : ports) {
      if (target->portCount < 3) {
        target->ports[target->portCount++] = p.as<int>();
      }
    }
  }

  markLocalChange();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
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
    // Local settings-portal state saved by markLocalChange(). A blank saved
    // gateway IP means "use DHCP auto-detection" (set later once connected).
    strncpy(gatewayIP, preferences.getString("gwip", "").c_str(), sizeof(gatewayIP) - 1);
    strncpy(gatewayName, preferences.getString("gwname", "").c_str(), sizeof(gatewayName) - 1);
    temperatureMonitoringDisabled = preferences.getBool("tempdis", false);
    tempThresholdWarningLocal = preferences.getFloat("tempwarn", 50.0);
    tempThresholdCriticalLocal = preferences.getFloat("tempcrit", 60.0);
    buzzerDisabled = preferences.getBool("buzzdis", false);
    buzzerMuted = preferences.getBool("buzzmute", false);
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
      // Only auto-detect from DHCP if no custom gateway override was saved
      // (via the local settings portal or a Beszel-pushed change).
      if (strlen(gatewayIP) == 0) {
        WiFi.gatewayIP().toString().toCharArray(gatewayIP, sizeof(gatewayIP));
      }
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

    server.on("/", HTTP_GET, handleDashboard);
    server.on("/api/state", HTTP_GET, handleGetState);
    server.on("/api/settings/save", HTTP_POST, handleSaveSettings);
    server.on("/api/channel/save", HTTP_POST, handleSaveChannel);
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
  // Silencing the buzzer (disabled or temporarily muted) never affects
  // relay/probe/recovery logic - only the audible pattern is skipped.
  if (buzzerDisabled || buzzerMuted) return;
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

// icmpPing sends a single ICMP echo request to ipStr over a raw socket and
// reports whether a matching reply arrived within timeoutMs. This is the
// primary reachability check (replacing TCP port probing) so a monitored
// host no longer needs any port open to be recognized as up - only the IP
// needs to answer a ping, matching how the hub itself checks reachability.
// Returns false both when the socket can't be opened and when no reply
// arrives in time; callers that want a fallback path treat either the same.
bool icmpPing(const char* ipStr, int timeoutMs) {
  uint32_t destAddr = inet_addr(ipStr);
  if (destAddr == INADDR_NONE) return false;

  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock < 0) return false;

  struct timeval tv;
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  uint16_t id = (uint16_t)random(1, 65535);
  static uint16_t pingSeq = 0;
  uint16_t seq = ++pingSeq;

  struct icmp_echo_hdr icmpHdr;
  memset(&icmpHdr, 0, sizeof(icmpHdr));
  icmpHdr.type = ICMP_ECHO;
  icmpHdr.code = 0;
  icmpHdr.chksum = 0;
  icmpHdr.id = htons(id);
  icmpHdr.seqno = htons(seq);
  icmpHdr.chksum = inet_chksum(&icmpHdr, sizeof(icmpHdr));

  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = destAddr;

  if (sendto(sock, &icmpHdr, sizeof(icmpHdr), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
    close(sock);
    return false;
  }

  bool replied = false;
  unsigned long deadline = millis() + (unsigned long)timeoutMs;
  uint8_t buf[128];
  while (!replied && (long)(deadline - millis()) > 0) {
    struct sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromLen);
    if (n <= 0) break; // SO_RCVTIMEO expired or read error - no reply
    if (from.sin_addr.s_addr != destAddr) continue;

    // Raw ICMP sockets deliver the IP header too; skip it using its IHL
    // (low nibble of the first byte, in 32-bit words) to reach the ICMP body.
    int ipHeaderLen = (buf[0] & 0x0F) * 4;
    if (n < ipHeaderLen + (int)sizeof(struct icmp_echo_hdr)) continue;
    struct icmp_echo_hdr* reply = (struct icmp_echo_hdr*)(buf + ipHeaderLen);
    if (reply->type == ICMP_ER && ntohs(reply->id) == id && ntohs(reply->seqno) == seq) {
      replied = true;
    }
  }
  close(sock);
  return replied;
}

// probeGateway checks whether the local network gateway is reachable via
// ICMP ping first, falling back to the previous TCP-connect approach
// (ports 53/80) only if the raw ICMP socket itself couldn't be opened. If no
// gateway IP is known yet, treat it as reachable rather than blocking
// escalation on missing configuration.
bool probeGateway() {
  if (strlen(gatewayIP) == 0) return true;
  if (icmpPing(gatewayIP, 1200)) return true;
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

// computeLocalConfigHash produces a simple, deterministic hash of the
// current local configuration, for display/debugging only. It does NOT need
// to match the hub's own hash algorithm - sync state between Beszel and this
// firmware is driven by comparing revision numbers, not by requiring
// byte-identical hashes across Go and this firmware.
String computeLocalConfigHash() {
  String canonical = String(moduleName) + "|" + String(gatewayIP) + "|" + String(gatewayName) + "|" +
    String(heartbeatIntervalMs) + "|" + String(temperatureMonitoringDisabled) + "|" +
    String(tempThresholdWarningLocal, 1) + "|" + String(tempThresholdCriticalLocal, 1) + "|" +
    String(buzzerDisabled) + "|" + String(buzzerMuted);
  for (int i = 0; i < activeChannelCount; i++) {
    MonitoredChannel& ch = activeChannels[i];
    canonical += "|ch" + String(ch.channelNumber) + ":" + String(ch.hostIP) + ":" +
      String(ch.maintenance) + ":" + String(ch.hwRecoveryDisabled) + ":" +
      String(ch.failureThreshold) + ":" + String(ch.bootGraceSeconds);
    for (int p = 0; p < ch.portCount; p++) {
      canonical += "," + String(ch.ports[p]);
    }
  }

  uint32_t hash = 2166136261UL; // FNV-1a 32-bit offset basis
  for (size_t i = 0; i < canonical.length(); i++) {
    hash ^= (uint8_t)canonical[i];
    hash *= 16777619UL; // FNV prime
  }
  char hex[9];
  snprintf(hex, sizeof(hex), "%08x", hash);
  return String(hex);
}

// markLocalChange bumps the local revision, recomputes the local hash, and
// flags the change to be reported to the hub on the next ping. Call this
// right after applying any settings-save from this device's own web portal.
void markLocalChange() {
  localChangeBaseRevision = localConfigRevision;
  localConfigRevision++;
  String newHash = computeLocalConfigHash();
  strncpy(localConfigHash, newHash.c_str(), sizeof(localConfigHash) - 1);
  pendingLocalChange = true;

  preferences.begin("watchdog", false);
  preferences.putInt("revision", localConfigRevision);
  preferences.putString("hash", localConfigHash);
  preferences.putString("gwip", gatewayIP);
  preferences.putString("gwname", gatewayName);
  preferences.putBool("tempdis", temperatureMonitoringDisabled);
  preferences.putFloat("tempwarn", tempThresholdWarningLocal);
  preferences.putFloat("tempcrit", tempThresholdCriticalLocal);
  preferences.putBool("buzzdis", buzzerDisabled);
  preferences.putBool("buzzmute", buzzerMuted);
  preferences.putString("name", moduleName);
  preferences.end();
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
      bool hostResponded = icmpPing(ch.hostIP, 1200);

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
            int threshold = (ch.failureThreshold > 0) ? ch.failureThreshold : 3;
            if (ch.consecutiveFailures >= threshold) {
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
            unsigned long bootGraceMs = (ch.bootGraceSeconds > 0) ? (unsigned long)ch.bootGraceSeconds * 1000UL : 60000UL;
            if (now - ch.stateStartTime >= bootGraceMs) {
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

  // Sized generously to fit a full local_change payload (module + all
  // channels) alongside the regular ping fields.
  StaticJsonDocument<2048> doc;
  doc["mac_address"] = WiFi.macAddress();
  doc["ip_address"] = WiFi.localIP().toString();
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["max_channels"] = MAX_CHANNELS_LIMIT;
  doc["config_revision"] = localConfigRevision;
  doc["config_hash"] = localConfigHash;
  if (!temperatureMonitoringDisabled && currentTemp > 0) {
    doc["temperature"] = currentTemp;
  }

  // Report a pending local edit (from this device's own web portal) so the
  // hub can accept it as the new desired state, or flag a conflict if its
  // own desired config has moved on since this edit was made.
  if (pendingLocalChange) {
    JsonObject localChange = doc.createNestedObject("local_change");
    localChange["base_revision"] = localChangeBaseRevision;
    JsonObject moduleObj = localChange.createNestedObject("module");
    moduleObj["name"] = moduleName;
    moduleObj["gateway_ip"] = gatewayIP;
    moduleObj["gateway_name"] = gatewayName;
    moduleObj["ping_interval_seconds"] = heartbeatIntervalMs / 1000;
    moduleObj["temperature_monitoring_disabled"] = temperatureMonitoringDisabled;
    moduleObj["temp_threshold_warning"] = tempThresholdWarningLocal;
    moduleObj["temp_threshold_critical"] = tempThresholdCriticalLocal;
    moduleObj["buzzer_disabled"] = buzzerDisabled;
    moduleObj["buzzer_muted"] = buzzerMuted;

    JsonArray channelsArr = localChange.createNestedArray("channels");
    for (int i = 0; i < activeChannelCount; i++) {
      MonitoredChannel& ch = activeChannels[i];
      JsonObject chObj = channelsArr.createNestedObject();
      chObj["channel"] = ch.channelNumber;
      chObj["host_ip"] = ch.hostIP;
      chObj["failure_threshold"] = ch.failureThreshold;
      chObj["boot_grace_seconds"] = ch.bootGraceSeconds;
      chObj["maintenance"] = ch.maintenance;
      chObj["hardware_recovery_disabled"] = ch.hwRecoveryDisabled;
      JsonArray portsArr = chObj.createNestedArray("probe_ports");
      for (int p = 0; p < ch.portCount; p++) {
        portsArr.add(ch.ports[p]);
      }
    }
  }

  String requestBody;
  serializeJson(doc, requestBody);

  int httpCode = http.POST(requestBody);
  lastPingSuccess = (httpCode == HTTP_CODE_OK);
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
      bool conflict = respDoc["pending_esp_change"] | false;

      // A pending local edit is resolved once the hub's desired revision
      // catches up to what we reported - whether because it accepted our
      // change (revision now equals what we sent) or because a conflict is
      // now being tracked hub-side instead (still shows as pending until an
      // admin resolves it, so keep resending until the flag clears).
      if (pendingLocalChange && !conflict && remoteRevision >= localConfigRevision) {
        pendingLocalChange = false;
      }

      // While we have our own unconfirmed local edit in flight, don't let a
      // stale/older hub-pushed value overwrite it - we'd just fight
      // ourselves. Once accepted (pendingLocalChange cleared above) or if
      // there was never a pending local edit, hub-pushed values apply
      // normally below.
      bool applyHubPush = !pendingLocalChange;

      if (applyHubPush) {
        const char* pushedName = respDoc["name"] | "";
        if (pushedName[0] != '\0') strncpy(moduleName, pushedName, sizeof(moduleName) - 1);
        const char* pushedGwIp = respDoc["gateway_ip"] | "";
        if (pushedGwIp[0] != '\0') strncpy(gatewayIP, pushedGwIp, sizeof(gatewayIP) - 1);
        const char* pushedGwName = respDoc["gateway_name"] | "";
        strncpy(gatewayName, pushedGwName, sizeof(gatewayName) - 1);
        temperatureMonitoringDisabled = respDoc["temperature_monitoring_disabled"] | false;
        tempThresholdWarningLocal = respDoc["temp_threshold_warning"] | 50.0;
        tempThresholdCriticalLocal = respDoc["temp_threshold_critical"] | 60.0;
        buzzerDisabled = respDoc["buzzer_disabled"] | false;
        buzzerMuted = respDoc["buzzer_muted"] | false;
        long pushedPingInterval = respDoc["ping_interval_seconds"] | 0;
        if (pushedPingInterval >= 5) {
          heartbeatIntervalMs = pushedPingInterval * 1000UL;
        }
      }

      // Revision number is the sole authoritative sync signal (see
      // computeLocalConfigHash's comment - the two sides don't share a hash
      // algorithm). Requiring a hash mismatch too could permanently block a
      // legitimate rebuild whenever both sides' hashes happen to already be
      // equal or empty, so only the revision is checked here.
      if (applyHubPush && remoteRevision != localConfigRevision) {
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
          newCh.failureThreshold = item["failure_threshold"] | 3;
          newCh.bootGraceSeconds = item["boot_grace_seconds"] | 60;
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
      } else if (applyHubPush) {
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
            activeChannels[i].failureThreshold = item["failure_threshold"] | 3;
            activeChannels[i].bootGraceSeconds = item["boot_grace_seconds"] | 60;
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

    lcd.print(channelStateLabel(ch.state));
  }
  currentDisplayPage = (currentDisplayPage + 1) % ((activeChannelCount + itemsPerPage - 1) / itemsPerPage);
}
