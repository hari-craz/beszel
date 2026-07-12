#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

// --- Configuration Defaults ---
#define FIRMWARE_VERSION "1.0.0"
#define DEFAULT_WIFI_SSID "Your_WiFi_SSID"
#define DEFAULT_WIFI_PASS "Your_WiFi_Password"
#define DEFAULT_HUB_URL "http://192.168.1.5:8090/api/beszel/recovery/ping"

// Hardware Pin Layouts (Configured for standard ESP32 boards)
#define ONE_WIRE_BUS 4       // DS18B20 temperature sensor data pin
#define BUZZER_PIN 12        // Audible piezo buzzer output pin
#define MAX_CHANNELS_LIMIT 6 // Maximum hardware relay outputs supported

// Hardware output pins for relays CH1 - CH6
const int RELAY_PINS[MAX_CHANNELS_LIMIT] = {13, 14, 25, 26, 27, 32};

// I2C LCD Configurations (PCF8574 backpacks)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Temperature sensors
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Persistent flash storage
Preferences preferences;
WebServer server(80);

// --- State Machine Types & Structs ---
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
  ChannelState state;
  int consecutiveFailures;
  unsigned long lastProbeTime;
  unsigned long stateStartTime;
  int recoveryAttempts;
};

// Global telemetry and cache memory
MonitoredChannel activeChannels[MAX_CHANNELS_LIMIT];
int activeChannelCount = 0;
int localConfigRevision = 0;
char localConfigHash[65] = "";
char hubURL[128] = DEFAULT_HUB_URL;
unsigned long lastPingTime = 0;
unsigned long lastDisplayPageRotation = 0;
int currentDisplayPage = 0;

// Non-blocking buzzer scheduling variables
unsigned long buzzerPatternStartTime = 0;
int buzzerPatternBeeps = 0;
int buzzerPatternGap = 0;
bool buzzerIsBeeping = false;
bool buzzerState = false;

// --- Helper Declarations ---
void triggerBuzzer(int beepCount, int durationMs);
void updateLCDDisplay();
void processProberStateMachines();
void syncWithHub();

// --- REST Web Server Request Handlers ---
void handleRelayTrigger() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  int targetChannel = doc["channel"];
  int durationMs = doc["pulse_duration_ms"];

  if (targetChannel < 1 || targetChannel > MAX_CHANNELS_LIMIT) {
    server.send(400, "application/json", "{\"error\":\"Invalid channel range\"}");
    return;
  }

  int pin = RELAY_PINS[targetChannel - 1];
  digitalWrite(pin, HIGH); // Activate relay pulse
  delay(durationMs);       // Bounded block for short physical triggers
  digitalWrite(pin, LOW);  // Return to fail-safe state

  triggerBuzzer(1, 250);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// --- Arduino Core Initialization ---
void setup() {
  Serial.begin(115200);

  // Initialize hardware relays to safe default output states
  for (int i = 0; i < MAX_CHANNELS_LIMIT; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize display
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ESP Watchdog Booting");

  sensors.begin();

  // Load persistent configurations from non-volatile storage
  preferences.begin("watchdog", false);
  localConfigRevision = preferences.getInt("revision", 0);
  String savedHash = preferences.getString("hash", "");
  strncpy(localConfigHash, savedHash.c_str(), sizeof(localConfigHash) - 1);
  String savedHub = preferences.getString("hub_url", DEFAULT_HUB_URL);
  strncpy(hubURL, savedHub.c_str(), sizeof(hubURL) - 1);
  preferences.end();

  // Connect to Local Wi-Fi
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  lcd.setCursor(0, 1);
  lcd.print("Connecting Wi-Fi...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Wi-Fi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    triggerBuzzer(2, 100);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Fail: Local Mode");
  }

  // Register endpoints
  server.on("/api/relay/trigger", HTTP_POST, handleRelayTrigger);
  server.begin();
  delay(1500);
}

// --- Main Operational Task Loop ---
void loop() {
  server.handleClient();
  processProberStateMachines();
  syncWithHub();
  updateLCDDisplay();

  // Keep buzzer scheduler running asynchronously
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

// Non-blocking buzzer scheduling trigger
void triggerBuzzer(int beepCount, int durationMs) {
  buzzerPatternBeeps = beepCount;
  buzzerPatternGap = durationMs;
  buzzerPatternStartTime = millis();
  buzzerState = false;
}

// Dial target TCP port to verify system health
bool probeTCPPort(const char* ip, int port) {
  WiFiClient client;
  client.setTimeout(1500); // 1.5 seconds maximum timeout
  if (client.connect(ip, port)) {
    client.stop();
    return true;
  }
  return false;
}

// --- Independent Watchdog State Machine Loop ---
void processProberStateMachines() {
  unsigned long now = millis();

  for (int i = 0; i < activeChannelCount; i++) {
    MonitoredChannel& ch = activeChannels[i];

    if (ch.maintenance) {
      ch.state = STATE_ONLINE;
      continue;
    }

    // Determine probing schedule based on current state (5s normal, 2s fast verify)
    unsigned long interval = (ch.state == STATE_VERIFYING_FAILURE) ? 2000 : 5000;

    if (now - ch.lastProbeTime >= interval) {
      ch.lastProbeTime = now;
      bool hostResponded = false;

      // Scan all configured TCP ports
      for (int p = 0; p < ch.portCount; p++) {
        if (probeTCPPort(ch.hostIP, ch.ports[p])) {
          hostResponded = true;
          break;
        }
      }

      // Execute State Transitions
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
            if (ch.consecutiveFailures >= 3) { // 3 consecutive failures
              ch.state = STATE_SHORT_PRESS;
              ch.stateStartTime = now;
              ch.recoveryAttempts++;
              // Activate short relay pulse (simulate motherboard power button click)
              int pin = RELAY_PINS[ch.channelNumber - 1];
              digitalWrite(pin, HIGH);
              delay(300); // Bounded short pulse duration
              digitalWrite(pin, LOW);
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
            // Wait for boot grace timeout (e.g. 60 seconds)
            if (now - ch.stateStartTime >= 60000) {
              if (ch.recoveryAttempts < 2) {
                ch.state = STATE_HARD_HOLD;
                ch.stateStartTime = now;
                ch.recoveryAttempts++;
                // Hold power button for 8 seconds to force power off
                int pin = RELAY_PINS[ch.channelNumber - 1];
                digitalWrite(pin, HIGH);
                delay(8000); // Force power down
                digitalWrite(pin, LOW);
                delay(1500); // Let voltages stabilize
                // Tap power button to turn back on
                digitalWrite(pin, HIGH);
                delay(300);
                digitalWrite(pin, LOW);
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
            // Enter cooldown cycle for 5 minutes before retrying
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

// --- Bidirectional PocketBase Hub Config Synchronization ---
void syncWithHub() {
  unsigned long now = millis();
  if (now - lastPingTime < 30000 && lastPingTime != 0) {
    return;
  }
  lastPingTime = now;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // Request temperature readings from DS18B20 sensor
  sensors.requestTemperatures();
  float currentTemp = sensors.getTempCByIndex(0);
  if (currentTemp == DEVICE_DISCONNECTED_C) {
    currentTemp = 0.0f; // Invalid temperature default
  }

  HTTPClient http;
  http.begin(hubURL);
  http.addHeader("Content-Type", "application/json");

  // Format telemetry payload
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
    StaticJsonDocument<1024> respDoc;
    DeserializationError err = deserializeJson(respDoc, response);

    if (!err) {
      int remoteRevision = respDoc["config_revision"];
      String remoteHash = respDoc["config_hash"];

      // If configuration mismatch is detected, update local channel mappings
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
          newCh.state = STATE_ONLINE;
          newCh.consecutiveFailures = 0;
          newCh.lastProbeTime = 0;
          newCh.recoveryAttempts = 0;

          // Parse ports
          JsonArray ports = item["ports"];
          newCh.portCount = 0;
          for (int p : ports) {
            if (newCh.portCount < 3) {
              newCh.ports[newCh.portCount++] = p;
            }
          }
          activeChannelCount++;
        }

        // Save new configs into persistent NVS flash memory
        preferences.begin("watchdog", false);
        preferences.putInt("revision", remoteRevision);
        preferences.putString("hash", remoteHash);
        preferences.end();

        localConfigRevision = remoteRevision;
        strncpy(localConfigHash, remoteHash.c_str(), sizeof(localConfigHash) - 1);
        triggerBuzzer(3, 100);
      }
    }
  }
  http.end();
}

// --- Dynamic Rotating I2C LCD Screen Views ---
void updateLCDDisplay() {
  unsigned long now = millis();
  if (now - lastDisplayPageRotation < 4000) {
    return;
  }
  lastDisplayPageRotation = now;

  lcd.clear();

  // If no channels configured, show system status screen
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

  // Display summary of monitored channels
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
      case STATE_ONLINE:
        lcd.print("OK");
        break;
      case STATE_VERIFYING_FAILURE:
        lcd.print("VERIFY");
        break;
      case STATE_SHORT_PRESS:
      case STATE_HARD_HOLD:
        lcd.print("PRESS");
        break;
      case STATE_BOOT_GRACE:
        lcd.print("BOOT");
        break;
      case STATE_CRITICAL:
        lcd.print("CRIT");
        break;
      default:
        lcd.print("WARN");
        break;
    }
  }

  // Rotate display page
  currentDisplayPage = (currentDisplayPage + 1) % ((activeChannelCount + itemsPerPage - 1) / itemsPerPage);
}
