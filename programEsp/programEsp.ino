/**
 * ═══════════════════════════════════════════════════════════════════
 *  Hydroponic pH Controller — ESP32
 * ═══════════════════════════════════════════════════════════════════
 *  Library yang dibutuhkan (install via Arduino Library Manager):
 *   - WiFiManager by tzapu          (v2.0+)
 *   - WebSockets by Markus Sattler  (v2.4+)
 *   - ArduinoJson by Benoit Blanchon (v6+)
 *
 *  Fitur:
 *   - WiFiManager: config WiFi, API URL, WebSocket host via portal web
 *   - Threshold: fetch dari API saat boot, simpan di Preferences (NVS)
 *     → update selanjutnya via WebSocket (tidak perlu API lagi)
 *   - Mode Otomatis: relay dikontrol otomatis berdasarkan pH vs threshold
 *   - Mode Manual  : relay dikontrol dari aplikasi via WebSocket
 *   - Setiap perubahan relay di-publish ke WebSocket
 *   - Kirim data pH ke WebSocket setiap interval
 * ═══════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <WebSocketsClient.h>    // https://github.com/Links2004/arduinoWebSockets
#include <ArduinoJson.h>         // https://github.com/bblanchon/ArduinoJson
#include <Preferences.h>         // Built-in ESP32 NVS (Non-Volatile Storage)
#include <HTTPClient.h>          // Built-in

// ─── Pin Configuration ────────────────────────────────────────────────────────
#define RELAY1_PIN     2   // Relay Asam (Acid) — menurunkan pH
#define RELAY2_PIN     27   // Relay Basa (Base) — menaikkan pH
#define PH_SENSOR_PIN  34   // pH sensor analog input (ADC1)

// ─── Konstanta ────────────────────────────────────────────────────────────────
#define PH_SEND_INTERVAL     1500    // Kirim pH ke WS setiap 5 detik
#define RELAY_MIN_ON_TIME    3000    // Relay minimal menyala 3 detik (cooldown)
#define PH_DEAD_BAND         0.15f   // ±0.15 dari threshold = zona aman

// ─── Default Config (bisa diubah via WiFiManager portal) ─────────────────────
char cfgWsHost[80]   = "server-iot-qbyte.qbyte.web.id";
char cfgWsPath[40]   = "/ws";
char cfgApiBase[120] = "https://hydroponik.qbyte.web.id";
char cfgUserId[20]   = "9911";

// ─── State Global ─────────────────────────────────────────────────────────────
WebSocketsClient webSocket;
Preferences       prefs;

bool   wsConnected      = false;
bool   relay1State      = false;   // Asam
bool   relay2State      = false;   // Basa
String currentMode      = "manual";
float  threshold        = 6.5f;
bool   thresholdLoaded  = false;   // sudah ada nilai dari EEPROM/API
bool   isFirstConnect   = true;    // flag: koneksi WS pertama kali setelah boot

unsigned long relay1OnAt = 0;      // cooldown timestamp relay1
unsigned long relay2OnAt = 0;      // cooldown timestamp relay2

// ─────────────────────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────────────
void publishRelayState(int relayId, bool state);
void setRelay1(bool state, bool publish = true);
void setRelay2(bool state, bool publish = true);

// ═══════════════════════════════════════════════════════════════════
//  PREFERENCES (NVS) — simpan config ke flash
// ═══════════════════════════════════════════════════════════════════
void saveConfig() {
  prefs.begin("hydro-cfg", false);
  prefs.putString("ws_host",  cfgWsHost);
  prefs.putString("ws_path",  cfgWsPath);
  prefs.putString("api_base", cfgApiBase);
  prefs.putString("user_id",  cfgUserId);
  prefs.end();
  Serial.println("[NVS] Config saved.");
}

void loadConfig() {
  prefs.begin("hydro-cfg", true);
  String h = prefs.getString("ws_host",  cfgWsHost);
  String p = prefs.getString("ws_path",  cfgWsPath);
  String a = prefs.getString("api_base", cfgApiBase);
  String u = prefs.getString("user_id",  cfgUserId);
  prefs.end();
  strncpy(cfgWsHost,   h.c_str(), sizeof(cfgWsHost));
  strncpy(cfgWsPath,   p.c_str(), sizeof(cfgWsPath));
  strncpy(cfgApiBase,  a.c_str(), sizeof(cfgApiBase));
  strncpy(cfgUserId,   u.c_str(), sizeof(cfgUserId));
  Serial.println("[NVS] Config loaded.");
}

void saveThreshold(float val) {
  prefs.begin("hydro-cfg", false);
  prefs.putFloat("threshold", val);
  prefs.end();
  Serial.printf("[NVS] Threshold saved: %.2f\n", val);
}

float loadThreshold() {
  prefs.begin("hydro-cfg", true);
  float val = prefs.getFloat("threshold", -1.0f);
  prefs.end();
  return val;
}

// ═══════════════════════════════════════════════════════════════════
//  WiFiManager — setup WiFi + custom parameters
// ═══════════════════════════════════════════════════════════════════
bool needSaveConfig = false;

void saveConfigCallback() {
  needSaveConfig = true;
}

void setupWiFiManager() {
  WiFiManager wm;

  // Custom parameter untuk portal web
  WiFiManagerParameter paramWsHost("ws_host",  "WebSocket Host",    cfgWsHost,  80);
  WiFiManagerParameter paramWsPath("ws_path",  "WebSocket Path",    cfgWsPath,  40);
  WiFiManagerParameter paramApi   ("api_base", "API Base URL",      cfgApiBase, 120);
  WiFiManagerParameter paramUser  ("user_id",  "User ID",           cfgUserId,  20);

  wm.addParameter(&paramWsHost);
  wm.addParameter(&paramWsPath);
  wm.addParameter(&paramApi);
  wm.addParameter(&paramUser);

  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(180);  // portal timeout 3 menit
  wm.setAPClientCheck(true);       // auto restart jika tidak ada client

  Serial.println("[WiFi] Connecting...");
  // AP: "HydroESP32" password: "12345678"
  if (!wm.autoConnect("HydroESP32", "12345678")) {
    Serial.println("[WiFi] Failed to connect. Restarting...");
    delay(3000);
    ESP.restart();
  }

  if (needSaveConfig) {
    strncpy(cfgWsHost,  paramWsHost.getValue(),  sizeof(cfgWsHost));
    strncpy(cfgWsPath,  paramWsPath.getValue(),  sizeof(cfgWsPath));
    strncpy(cfgApiBase, paramApi.getValue(),     sizeof(cfgApiBase));
    strncpy(cfgUserId,  paramUser.getValue(),    sizeof(cfgUserId));
    saveConfig();
  }

  Serial.print("[WiFi] Connected! IP: ");
  Serial.println(WiFi.localIP());
}

// ═══════════════════════════════════════════════════════════════════
//  pH SENSOR
// ═══════════════════════════════════════════════════════════════════
/**
 * Baca nilai pH dari sensor analog.
 * ───────────────────────────────────────────────────────────────────
 * Kalibrasi:
 *   Ukur tegangan saat sensor di buffer pH 4.0 dan pH 7.0, lalu
 *   sesuaikan nilai 'phSlope' dan 'phOffset' agar cocok.
 *
 *   Rumus:  pH = phSlope * voltage + phOffset
 *
 * Default (sesuaikan!):
 *   pH 4.0 → ~2.23 V   → slope = (7-4)/(V7-V4)
 *   pH 7.0 → ~1.92 V
 */
float readPH() {
  // Rata-rata 10 sample untuk stabilitas
  int   samples = 10;
  long  sum     = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(PH_SENSOR_PIN);
    delay(10);
  }
  float raw     = sum / (float)samples;
  float voltage = raw * (3.3f / 4095.0f);   // ESP32 ADC 12-bit, VCC 3.3V

  // ── SESUAIKAN NILAI INI DENGAN KALIBRASI SENSOR ANDA ──
  const float phSlope  = -5.70f;   // slope kalibrasi
  const float phOffset = 21.34f;   // offset kalibrasi
  // ──────────────────────────────────────────────────────

  float ph = phSlope * voltage + phOffset;
  ph = constrain(ph, 0.0f, 14.0f);
  return ph;
}

// ═══════════════════════════════════════════════════════════════════
//  RELAY CONTROL
// ═══════════════════════════════════════════════════════════════════
void publishRelayState(int relayId, bool state) {
  if (!wsConnected) return;
  StaticJsonDocument<160> doc;
  doc["action"]  = "publish";
  doc["topic"]   = String("data/relay") + relayId + "/user/" + cfgUserId;
  doc["payload"] = state ? 1 : 0;
  String msg;
  serializeJson(doc, msg);
  webSocket.sendTXT(msg);
  Serial.printf("[WS] Published relay%d = %s\n", relayId, state ? "ON" : "OFF");
}

void setRelay1(bool state, bool publish) {
  relay1State = state;
  digitalWrite(RELAY1_PIN, state ? HIGH : LOW);
  if (state) relay1OnAt = millis();
  Serial.printf("[Relay1/Asam] %s\n", state ? "ON" : "OFF");
  if (publish) publishRelayState(1, state);
}

void setRelay2(bool state, bool publish) {
  relay2State = state;
  digitalWrite(RELAY2_PIN, state ? HIGH : LOW);
  if (state) relay2OnAt = millis();
  Serial.printf("[Relay2/Basa] %s\n", state ? "ON" : "OFF");
  if (publish) publishRelayState(2, state);
}

// ═══════════════════════════════════════════════════════════════════
//  AUTO MODE — kontrol relay berdasarkan pH vs threshold
// ═══════════════════════════════════════════════════════════════════
void autoModeControl(float ph) {
  if (currentMode != "otomatis") return;

  unsigned long now = millis();

  // pH terlalu TINGGI (basa) → nyalakan pompa Asam (relay1)
  if (ph > threshold + PH_DEAD_BAND) {
    if (!relay1State) {
      setRelay1(true);
    }
    // Matikan relay Basa jika sedang ON dan cooldown selesai
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) {
      setRelay2(false);
    }
  }
  // pH terlalu RENDAH (asam) → nyalakan pompa Basa (relay2)
  else if (ph < threshold - PH_DEAD_BAND) {
    if (!relay2State) {
      setRelay2(true);
    }
    // Matikan relay Asam jika sedang ON dan cooldown selesai
    if (relay1State && (now - relay1OnAt > RELAY_MIN_ON_TIME)) {
      setRelay1(false);
    }
  }
  // pH dalam zona aman → matikan semua (setelah cooldown)
  else {
    if (relay1State && (now - relay1OnAt > RELAY_MIN_ON_TIME)) setRelay1(false);
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) setRelay2(false);
  }
}

// ═══════════════════════════════════════════════════════════════════
//  API — fetch threshold dari backend saat pertama boot
// ═══════════════════════════════════════════════════════════════════
void fetchThresholdFromAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(cfgApiBase) + "/api/tr?user=" + cfgUserId;
  Serial.println("[API] Fetching threshold: " + url);

  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();

  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (!err && doc["success"].as<bool>()) {
      String threshStr = doc["data"]["threshold"].as<String>();
      float val = threshStr.toFloat();
      if (val > 0.0f) {
        threshold       = val;
        thresholdLoaded = true;
        saveThreshold(threshold);
        Serial.printf("[API] Threshold OK: %.2f\n", threshold);
      }
    } else {
      Serial.println("[API] JSON parse/success error.");
    }
  } else {
    Serial.printf("[API] HTTP error: %d\n", code);
  }

  http.end();
}

// ═══════════════════════════════════════════════════════════════════
//  API — fetch mode dari backend saat boot
// ═══════════════════════════════════════════════════════════════════
void fetchModeFromAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(cfgApiBase) + "/api/mode?user=" + cfgUserId;
  Serial.println("[API] Fetching mode: " + url);

  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();

  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (!err && doc["success"].as<bool>()) {
      String modeStr = doc["data"]["mode"].as<String>();
      if (modeStr == "otomatis" || modeStr == "manual") {
        currentMode = modeStr;
        Serial.println("[API] Mode loaded: " + currentMode);
      } else {
        Serial.println("[API] Mode value invalid, using default: manual");
      }
    } else {
      Serial.println("[API] Mode JSON parse/success error.");
    }
  } else {
    Serial.printf("[API] Mode HTTP error: %d\n", code);
  }

  http.end();
}

// ═══════════════════════════════════════════════════════════════════
//  WebSocket — message handler
// ═══════════════════════════════════════════════════════════════════
bool parseBool(JsonVariant v) {
  if (v.is<bool>())   return v.as<bool>();
  if (v.is<int>())    return v.as<int>() == 1;
  String s = v.as<String>();
  return (s == "1" || s == "true");
}

void handleWsMessage(String& topic, JsonVariant payload) {
  String topicRelay1  = String("data/relay1/user/")    + cfgUserId;
  String topicRelay2  = String("data/relay2/user/")    + cfgUserId;
  String topicMode    = String("data/mode/user/")      + cfgUserId;
  String topicThresh  = String("data/treshold/user/")  + cfgUserId;

  // ── Relay 1 (mode manual saja) ──────────────────────────────────
  if (topic == topicRelay1) {
    if (currentMode == "manual") {
      bool newState = parseBool(payload);
      if (newState != relay1State) {
        setRelay1(newState, false);  // jangan re-publish (app yg kirim)
      }
    }
    return;
  }

  // ── Relay 2 (mode manual saja) ──────────────────────────────────
  if (topic == topicRelay2) {
    if (currentMode == "manual") {
      bool newState = parseBool(payload);
      if (newState != relay2State) {
        setRelay2(newState, false);  // jangan re-publish
      }
    }
    return;
  }

  // ── Mode otomatis/manual ─────────────────────────────────────────
  if (topic == topicMode) {
    String newMode = payload.as<String>();
    if (newMode == "otomatis" || newMode == "manual") {
      currentMode = newMode;
      Serial.println("[Mode] Changed to: " + currentMode);
      // Saat beralih ke manual, matikan relay yang menyala via auto
      if (currentMode == "manual") {
        if (relay1State) setRelay1(false);
        if (relay2State) setRelay2(false);
      }
    }
    return;
  }

  // ── Update threshold via WebSocket ──────────────────────────────
  // (setelah boot, tidak perlu API lagi — update datang dari app)
  if (topic == topicThresh) {
    float newVal = 0.0f;
    if (payload.is<float>() || payload.is<int>()) {
      newVal = payload.as<float>();
    } else {
      newVal = payload.as<String>().toFloat();
    }
    if (newVal > 0.0f) {
      threshold = newVal;
      saveThreshold(threshold);
      Serial.printf("[Threshold] Updated via WS: %.2f\n", threshold);
    }
    return;
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {

    case WStype_CONNECTED: {
      wsConnected = true;
      Serial.println("[WS] ✅ Connected");

      // Subscribe ke semua topic yang diperlukan
      const char* topics[] = {
        "data/relay1/user/",
        "data/relay2/user/",
        "data/mode/user/",
        "data/treshold/user/",
      };
      for (auto t : topics) {
        StaticJsonDocument<160> doc;
        doc["action"] = "subscribe";
        doc["topic"]  = String(t) + cfgUserId;
        String msg;
        serializeJson(doc, msg);
        webSocket.sendTXT(msg);
        Serial.println("[WS] Subscribed: " + String(t) + cfgUserId);
      }

      // Saat pertama kali boot: pastikan relay dalam kondisi OFF
      // dan beritahu semua subscriber (app) bahwa relay mati
      if (isFirstConnect) {
        isFirstConnect = false;
        relay1State = false;
        relay2State = false;
        digitalWrite(RELAY1_PIN, LOW);
        digitalWrite(RELAY2_PIN, LOW);
        publishRelayState(1, false);
        publishRelayState(2, false);
        Serial.println("[WS] Boot: relay direset ke OFF dan dipublish.");
      }
      break;
    }

    case WStype_TEXT: {
      String msgStr = String((char*)payload);
      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, msgStr);
      if (err) {
        Serial.println("[WS] JSON error: " + String(err.c_str()));
        break;
      }
      String topic = doc["topic"].as<String>();
      if (topic.length() > 0) {
        handleWsMessage(topic, doc["payload"]);
      }
      break;
    }

    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("[WS] ⚠ Disconnected");
      break;

    case WStype_ERROR:
      Serial.println("[WS] ❌ Error");
      break;

    default:
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n═══ Hydroponic pH Controller ═══");

  // ── GPIO ──────────────────────────────────────────────────────────
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // ── Load config dari NVS ──────────────────────────────────────────
  loadConfig();

  // ── Load threshold dari NVS (jika ada) ───────────────────────────
  float stored = loadThreshold();
  if (stored > 0.0f) {
    threshold       = stored;
    thresholdLoaded = true;
    Serial.printf("[NVS] Threshold loaded: %.2f\n", threshold);
  } else {
    Serial.println("[NVS] No threshold saved yet.");
  }






  // ── WiFiManager ───────────────────────────────────────────────────
  setupWiFiManager();

  // ── Fetch threshold dari API (selalu coba saat boot) ─────────────
  // Jika API berhasil → update NVS, selanjutnya update via WS saja
  fetchThresholdFromAPI();

  if (!thresholdLoaded) {
    Serial.printf("[Threshold] Using default: %.2f\n", threshold);
    threshold = 6.5f;
  }

  // ── Fetch mode dari API (setiap boot, ambil mode terakhir dari DB) ─
  // Mode akan terus diupdate via WebSocket saat running
  fetchModeFromAPI();
  Serial.println("[Mode] Initial mode: " + currentMode);

  // ── WebSocket ─────────────────────────────────────────────────────
  webSocket.beginSSL(cfgWsHost, 443, cfgWsPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(15000, 3000, 2);  // ping setiap 15 detik
  publishRelayState(1, false);
  publishRelayState(2, false);
  Serial.println("═══ Setup selesai ═══\n");
}

// ═══════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════
unsigned long lastPhSend   = 0;
unsigned long lastWifiCheck= 0;

void loop() {
  webSocket.loop();

  unsigned long now = millis();

  // ── Cek koneksi WiFi setiap 30 detik ────────────────────────────
  if (now - lastWifiCheck >= 30000) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Lost connection. Reconnecting...");
      WiFi.reconnect();
    }
  }

  // ── Baca & kirim pH setiap PH_SEND_INTERVAL ─────────────────────
  if (now - lastPhSend >= PH_SEND_INTERVAL) {
    lastPhSend = now;

    float ph = readPH();
    Serial.printf("[pH] %.2f | Mode: %s | Threshold: %.2f | R1:%s R2:%s\n",
      ph, currentMode.c_str(), threshold,
      relay1State ? "ON" : "OFF",
      relay2State ? "ON" : "OFF"
    );

    // Publish pH ke WebSocket
    if (wsConnected) {
      StaticJsonDocument<200> doc;
      doc["action"]            = "publish";
      doc["topic"]             = String("data/ph/user/") + cfgUserId;
      doc["payload"]["sensor1"] = random(0,10);//round(ph * 100.0f) / 100.0f;  // 2 desimal
      String msg;
      serializeJson(doc, msg);
      webSocket.sendTXT(msg);
    }

    // Auto mode: kontrol relay berdasarkan pH
    if (currentMode == "otomatis") {
      autoModeControl(ph);
    }
  }
}
