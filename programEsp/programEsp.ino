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
#include <WebServer.h>           // Built-in ESP32 — untuk web kalibrasi

// ─── Pin Configuration ────────────────────────────────────────────────────────
#define RELAY1_PIN     2    // Relay Asam (Acid) — menurunkan pH
#define RELAY2_PIN     27   // Relay Basa (Base) — menaikkan pH
#define PH_SENSOR_PIN  34   // pH sensor analog input (ADC1)
#define CALIB_BTN_PIN  0    // Tombol D0/BOOT — tekan saat boot untuk kalibrasi

// ─── Konstanta ────────────────────────────────────────────────────────────────
#define PH_SEND_INTERVAL     1500    // Kirim pH ke WS setiap 1.5 detik
#define RELAY_MIN_ON_TIME    3000    // Relay minimal menyala 3 detik (cooldown)
#define PH_DEAD_BAND         0.15f   // ±0.15 dari threshold = zona aman
#define CALIB_BTN_HOLD_MS    3000    // Tahan D0 3 detik saat running untuk kalibrasi

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

// ─── pH Sensor Variables ──────────────────────────────────────────────────────
int               phBuffer[10], phTemp;
unsigned long int phAvgVal;
float             calibration_value = 22.84f;  // offset kalibrasi (disimpan NVS)
float             phSlope           = -5.70f;  // slope kalibrasi  (disimpan NVS)

// ─── Calibration Mode ────────────────────────────────────────────────────────
WebServer         calibServer(80);
bool              calibMode      = false;
unsigned long     btnPressStart  = 0;
bool              btnWasPressed  = false;

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

void saveCalibration() {
  prefs.begin("hydro-cfg", false);
  prefs.putFloat("ph_slope",  phSlope);
  prefs.putFloat("ph_offset", calibration_value);
  prefs.end();
  Serial.printf("[NVS] Calibration saved: slope=%.4f offset=%.4f\n", phSlope, calibration_value);
}

void loadCalibration() {
  prefs.begin("hydro-cfg", true);
  phSlope           = prefs.getFloat("ph_slope",  -5.70f);
  calibration_value = prefs.getFloat("ph_offset", 22.84f);
  prefs.end();
  Serial.printf("[NVS] Calibration loaded: slope=%.4f offset=%.4f\n", phSlope, calibration_value);
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
 * Algoritma:
 *   1. Ambil 10 sampel ADC dengan jeda 30 ms
 *   2. Urutkan (bubble sort) untuk menyingkirkan outlier
 *   3. Rata-ratakan 6 data tengah (indeks 2–7) → lebih stabil
 *   4. Konversi ke voltase ESP32 (3.3 V / 4095 / 6 sampel)
 *   5. Hitung pH: ph = -5.70 * volt + calibration_value
 *
 * Kalibrasi:
 *   Sesuaikan 'calibration_value' (baris global di atas) dengan
 *   hasil pengukuran di buffer pH 4.0 dan pH 7.0 Anda.
 */
// Helper: baca voltase mentah (dipakai juga oleh calib server)
float readVoltageRaw() {
  int buf[10], tmp; unsigned long acc = 0;
  for (int i = 0; i < 10; i++) { buf[i] = analogRead(PH_SENSOR_PIN); delay(30); }
  for (int i = 0; i < 9; i++)
    for (int j = i+1; j < 10; j++)
      if (buf[i] > buf[j]) { tmp=buf[i]; buf[i]=buf[j]; buf[j]=tmp; }
  for (int i = 2; i < 8; i++) acc += buf[i];
  return (float)acc * 3.3f / 4095.0f / 6.0f;
}

/**
 * Baca pH: 10 sampel bubble-sort → rata-rata 6 tengah → ph = phSlope * volt + calibration_value
 */
float readPH() {
  float volt = readVoltageRaw();
  return constrain(phSlope * volt + calibration_value, 0.0f, 14.0f);
}

// ═══════════════════════════════════════════════════════════════════
//  CALIBRATION WEB SERVER — HTML (PROGMEM)
// ═══════════════════════════════════════════════════════════════════
const char CALIB_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="id"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Kalibrasi Sensor pH</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);min-height:100vh;color:#e0e0e0;padding:16px}
.c{max-width:480px;margin:0 auto}
h1{text-align:center;font-size:1.4rem;font-weight:700;background:linear-gradient(90deg,#00d2ff,#3a7bd5);-webkit-background-clip:text;-webkit-text-fill-color:transparent;margin-bottom:20px;padding-top:8px}
.card{background:rgba(255,255,255,.07);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.12);border-radius:16px;padding:18px;margin-bottom:14px}
.card h2{font-size:.78rem;color:#a0a0c0;margin-bottom:12px;text-transform:uppercase;letter-spacing:.08em}
.ph-big{text-align:center;font-size:3.2rem;font-weight:800;background:linear-gradient(90deg,#00d2ff,#3a7bd5);-webkit-background-clip:text;-webkit-text-fill-color:transparent;line-height:1}
.ph-lbl{text-align:center;color:#606080;font-size:.75rem;margin-top:2px}
.volt{text-align:center;font-size:1.1rem;color:#80d0ff;margin-top:8px}
.step{background:rgba(0,210,255,.08);border-left:3px solid #00d2ff;padding:10px 12px;border-radius:8px;margin-bottom:10px;font-size:.84rem;color:#c0d0e0}
.btn{display:block;width:100%;padding:13px;border:none;border-radius:10px;font-size:.95rem;font-weight:600;cursor:pointer;transition:all .2s;margin-bottom:10px}
.bb{background:linear-gradient(135deg,#00d2ff,#3a7bd5);color:#fff}
.bg{background:linear-gradient(135deg,#11998e,#38ef7d);color:#fff}
.br{background:linear-gradient(135deg,#ff416c,#ff4b2b);color:#fff}
.btn:hover{opacity:.85;transform:translateY(-1px)}
.smpl{background:rgba(56,239,125,.12);border:1px solid rgba(56,239,125,.4);border-radius:8px;padding:10px;text-align:center;margin-bottom:10px;display:none}
.smpl span{color:#38ef7d;font-weight:700}
.ig{margin-bottom:12px}
.ig label{display:block;font-size:.82rem;color:#a0a0b0;margin-bottom:4px}
.ig input{width:100%;padding:10px 14px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.15);border-radius:8px;color:#fff;font-size:1rem}
.ig input:focus{outline:none;border-color:#00d2ff}
.st{padding:12px;border-radius:8px;text-align:center;font-weight:600;margin-bottom:12px;display:none}
.st.ok{background:rgba(56,239,125,.2);color:#38ef7d}
.st.err{background:rgba(255,65,108,.2);color:#ff416c}
.vals{display:flex;gap:8px;margin-top:4px}
.vi{flex:1;background:rgba(255,255,255,.05);border-radius:8px;padding:10px;text-align:center}
.vi .lb{font-size:.7rem;color:#808090}.vi .vl{font-size:1rem;color:#80d0ff;font-weight:600}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#38ef7d;margin-right:6px;animation:bk 1.2s infinite}
@keyframes bk{0%,100%{opacity:1}50%{opacity:.3}}
</style></head><body><div class="c">
<h1>🧪 Kalibrasi Sensor pH</h1>
<div class="card"><h2><span class="dot"></span>Pembacaan Real-time</h2>
  <div class="ph-big" id="pv">—</div>
  <div class="ph-lbl">nilai pH saat ini</div>
  <div class="volt" id="vv">— V</div>
</div>
<div class="card"><h2>⚙️ Kalibrasi Aktif</h2>
  <div class="vals">
    <div class="vi"><div class="lb">Slope</div><div class="vl" id="cs">—</div></div>
    <div class="vi"><div class="lb">Offset</div><div class="vl" id="co">—</div></div>
  </div>
</div>
<div class="card"><h2>📐 Kalibrasi 2 Titik (Direkomendasikan)</h2>
  <div class="step">1️⃣ Celupkan sensor ke buffer <strong>pH 4.0</strong>, tunggu stabil ±30 detik, tekan tombol.</div>
  <button class="btn bb" onclick="sp4()">📥 Ambil Sampel pH 4.0</button>
  <div class="smpl" id="s4">Buffer pH 4.0 → <span id="v4d">—</span> V ✅</div>
  <div class="step">2️⃣ Celupkan sensor ke buffer <strong>pH 7.0</strong>, tunggu stabil, tekan tombol.</div>
  <button class="btn bb" onclick="sp7()">📥 Ambil Sampel pH 7.0</button>
  <div class="smpl" id="s7">Buffer pH 7.0 → <span id="v7d">—</span> V ✅</div>
  <button class="btn bg" onclick="sv2()" id="b2" style="display:none">✅ Hitung &amp; Simpan Kalibrasi 2 Titik</button>
</div>
<div class="card"><h2>🎯 Kalibrasi 1 Titik (Cepat)</h2>
  <div class="step">Celupkan sensor ke larutan buffer yang diketahui, tunggu stabil, isi nilai pH referensi lalu klik Kalibrasi.</div>
  <div class="ig"><label>Nilai pH Referensi (0–14)</label>
    <input type="number" id="rp" step="0.01" min="0" max="14" placeholder="contoh: 7.00"></div>
  <button class="btn bb" onclick="c1p()">🎯 Kalibrasi 1 Titik</button>
</div>
<div class="st" id="sb"></div>
<button class="btn br" onclick="ex()">🔄 Selesai &amp; Restart ESP32</button>
</div><script>
let v4=null,v7=null,lv=0;
function poll(){fetch('/read').then(r=>r.json()).then(d=>{
  document.getElementById('pv').textContent=d.ph.toFixed(2);
  document.getElementById('vv').textContent=d.volt.toFixed(4)+' V';
  document.getElementById('cs').textContent=d.slope.toFixed(4);
  document.getElementById('co').textContent=d.offset.toFixed(4);
  lv=d.volt;}).catch(()=>{});}
function sp4(){v4=lv;document.getElementById('v4d').textContent=v4.toFixed(4);document.getElementById('s4').style.display='block';if(v7!==null)document.getElementById('b2').style.display='block';}
function sp7(){v7=lv;document.getElementById('v7d').textContent=v7.toFixed(4);document.getElementById('s7').style.display='block';if(v4!==null)document.getElementById('b2').style.display='block';}
function ss(m,ok){const e=document.getElementById('sb');e.textContent=m;e.className='st '+(ok?'ok':'err');e.style.display='block';setTimeout(()=>e.style.display='none',5000);}
function sv2(){
  if(v4===null||v7===null){ss('Sampel belum lengkap!',false);return;}
  if(Math.abs(v7-v4)<0.01){ss('Selisih voltase terlalu kecil! Cek larutan buffer.',false);return;}
  fetch('/calib2pt?v4='+v4+'&v7='+v7).then(r=>r.json()).then(d=>{
    if(d.ok)ss('✅ Slope='+d.slope.toFixed(4)+' Offset='+d.offset.toFixed(4),true);
    else ss('❌ '+d.msg,false);}).catch(()=>ss('❌ Koneksi gagal',false));}
function c1p(){
  const r=parseFloat(document.getElementById('rp').value);
  if(isNaN(r)||r<0||r>14){ss('Masukkan nilai pH 0–14',false);return;}
  fetch('/calib1pt?ref='+r+'&volt='+lv).then(r=>r.json()).then(d=>{
    if(d.ok)ss('✅ Offset baru: '+d.offset.toFixed(4),true);
    else ss('❌ '+d.msg,false);}).catch(()=>ss('❌ Koneksi gagal',false));}
function ex(){if(confirm('Keluar dari mode kalibrasi dan restart ESP32?')){
  fetch('/exit').finally(()=>{document.body.innerHTML='<div style="background:linear-gradient(135deg,#0f0c29,#302b63);min-height:100vh;display:flex;align-items:center;justify-content:center;color:#38ef7d;font-size:1.2rem;font-family:sans-serif;">✅ Menyimpan &amp; Restart...</div>';});}}
setInterval(poll,1500);poll();
</script></body></html>
)rawliteral";

// ── Handlers ──────────────────────────────────────────────────────
void hCalibRoot() { calibServer.send_P(200, "text/html", CALIB_HTML); }

void hCalibRead() {
  float volt = readVoltageRaw();
  float ph   = constrain(phSlope * volt + calibration_value, 0.0f, 14.0f);
  String j = "{\"ph\":" + String(ph,4) + ",\"volt\":" + String(volt,4)
           + ",\"slope\":" + String(phSlope,4) + ",\"offset\":" + String(calibration_value,4) + "}";
  calibServer.send(200, "application/json", j);
}

void hCalib2pt() {
  if (!calibServer.hasArg("v4") || !calibServer.hasArg("v7")) {
    calibServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  float v4 = calibServer.arg("v4").toFloat();
  float v7 = calibServer.arg("v7").toFloat();
  if (fabsf(v7 - v4) < 0.01f) {
    calibServer.send(200, "application/json", "{\"ok\":false,\"msg\":\"Selisih voltase terlalu kecil\"}"); return;
  }
  phSlope           = (7.0f - 4.0f) / (v7 - v4);
  calibration_value = 7.0f - phSlope * v7;
  saveCalibration();
  String j = "{\"ok\":true,\"slope\":" + String(phSlope,4) + ",\"offset\":" + String(calibration_value,4) + "}";
  calibServer.send(200, "application/json", j);
}

void hCalib1pt() {
  if (!calibServer.hasArg("ref") || !calibServer.hasArg("volt")) {
    calibServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  calibration_value = calibServer.arg("ref").toFloat() - phSlope * calibServer.arg("volt").toFloat();
  saveCalibration();
  calibServer.send(200, "application/json", "{\"ok\":true,\"offset\":" + String(calibration_value,4) + "}");
}

void hCalibExit() { calibServer.send(200, "text/plain", "OK"); delay(1500); ESP.restart(); }

// ── startCalibrationMode ──────────────────────────────────────────
void startCalibrationMode() {
  calibMode = true;
  Serial.println("\n[CALIB] ═══ Masuk Mode Kalibrasi pH ═══");
  digitalWrite(RELAY1_PIN, LOW); digitalWrite(RELAY2_PIN, LOW);
  relay1State = false; relay2State = false;
  webSocket.disconnect();
  WiFi.disconnect(true); delay(500);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("HydroCalib", "calibrasi");
  Serial.println("[CALIB] AP: SSID=HydroCalib | Pass=calibrasi");
  Serial.print("[CALIB] Buka: http://"); Serial.println(WiFi.softAPIP());
  calibServer.on("/",        hCalibRoot);
  calibServer.on("/read",    hCalibRead);
  calibServer.on("/calib2pt",hCalib2pt);
  calibServer.on("/calib1pt",hCalib1pt);
  calibServer.on("/exit",    hCalibExit);
  calibServer.begin();
  Serial.println("[CALIB] Web server aktif.");
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
  Serial.println("\n\n\u2550\u2550\u2550 Hydroponic pH Controller \u2550\u2550\u2550");

  // ── GPIO ──────────────────────────────────────────────────────────
  pinMode(RELAY1_PIN,   OUTPUT);
  pinMode(RELAY2_PIN,   OUTPUT);
  pinMode(CALIB_BTN_PIN, INPUT_PULLUP);  // D0/BOOT button
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // ── Load kalibrasi dari NVS ──────────────────────────────────────────────
  loadCalibration();

  // ── Cek tombol D0 saat boot ────────────────────────────────────────────
  if (digitalRead(CALIB_BTN_PIN) == LOW) {
    Serial.println("[CALIB] D0 ditekan saat boot \u2192 mode kalibrasi");
    startCalibrationMode();
    return;   // skip normal setup, loop() akan handle calibServer
  }

  // ── Load config dari NVS ───────────────────────────────────────────────
  loadConfig();

  // ── Load threshold dari NVS (jika ada) ────────────────────────────────
  float stored = loadThreshold();
  if (stored > 0.0f) {
    threshold       = stored;
    thresholdLoaded = true;
    Serial.printf("[NVS] Threshold loaded: %.2f\n", threshold);
  } else {
    Serial.println("[NVS] No threshold saved yet.");
  }

  // ── WiFiManager ──────────────────────────────────────────────────────────
  setupWiFiManager();

  // ── Fetch threshold dari API (selalu coba saat boot) ─────────────────
  fetchThresholdFromAPI();
  if (!thresholdLoaded) { threshold = 6.5f; Serial.printf("[Threshold] Default: %.2f\n", threshold); }

  // ── Fetch mode dari API ───────────────────────────────────────────────
  fetchModeFromAPI();
  Serial.println("[Mode] Initial mode: " + currentMode);

  // ── WebSocket ───────────────────────────────────────────────────────────
  webSocket.beginSSL(cfgWsHost, 443, cfgWsPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(15000, 3000, 2);
  publishRelayState(1, false);
  publishRelayState(2, false);
  Serial.println("\u2550\u2550\u2550 Setup selesai \u2550\u2550\u2550\n");
}

// ═══════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════
unsigned long lastPhSend   = 0;
unsigned long lastWifiCheck= 0;

void loop() {
  // ── Mode Kalibrasi: handle web server, skip semua logik normal ──────
  if (calibMode) {
    calibServer.handleClient();
    return;
  }

  webSocket.loop();
  unsigned long now = millis();

  // ── Deteksi long-press D0 (3 detik) untuk masuk kalibrasi ──────────
  if (digitalRead(CALIB_BTN_PIN) == LOW) {
    if (!btnWasPressed) { btnWasPressed = true; btnPressStart = now; }
    else if (now - btnPressStart >= CALIB_BTN_HOLD_MS) {
      Serial.println("[CALIB] D0 ditekan 3 detik \u2192 masuk mode kalibrasi");
      startCalibrationMode();
      return;
    }
  } else {
    btnWasPressed = false;
  }

  // ── Cek koneksi WiFi setiap 30 detik ──────────────────────────────
  if (now - lastWifiCheck >= 30000) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Lost connection. Reconnecting...");
      WiFi.reconnect();
    }
  }

  // ── Baca & kirim pH setiap PH_SEND_INTERVAL ───────────────────────
  if (now - lastPhSend >= PH_SEND_INTERVAL) {
    lastPhSend = now;
    float ph = readPH();
    Serial.printf("[pH] %.2f | Mode: %s | Threshold: %.2f | R1:%s R2:%s\n",
      ph, currentMode.c_str(), threshold,
      relay1State ? "ON" : "OFF", relay2State ? "ON" : "OFF");

    if (wsConnected) {
      StaticJsonDocument<200> doc;
      doc["action"]             = "publish";
      doc["topic"]              = String("data/ph/user/") + cfgUserId;
      doc["payload"]["sensor1"] = round(ph * 100.0f) / 100.0f;
      String msg; serializeJson(doc, msg);
      webSocket.sendTXT(msg);
    }
    if (currentMode == "otomatis") autoModeControl(ph);
  }
}
