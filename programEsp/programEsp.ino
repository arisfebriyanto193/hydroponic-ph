#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define RELAY1_PIN     2
#define RELAY2_PIN     4
#define PH_SENSOR_PIN  34
#define CALIB_BTN_PIN  0

#define PH_SEND_INTERVAL     1500
#define RELAY_MIN_ON_TIME    3000
#define PH_DEAD_BAND         0.15f
#define CALIB_BTN_HOLD_MS    3000
#define LCD_PAGE_INTERVAL    3000

char cfgWsHost[80]   = "server-iot-qbyte.qbyte.web.id";
char cfgWsPath[40]   = "/ws";
char cfgApiBase[120] = "https://hydroponik.qbyte.web.id";
char cfgUserId[20]   = "9911";
char cfgWifiSsid[32] = "";
char cfgWifiPass[64] = "";

WebSocketsClient  webSocket;
Preferences       prefs;
WebServer         server(80);

bool   wsConnected      = false;
bool   relay1State      = false;
bool   relay2State      = false;
String currentMode      = "manual";
float  threshold        = 6.5f;
bool   thresholdLoaded  = false;
bool   isFirstConnect   = true;
bool   isAPMode         = false;

unsigned long relay1OnAt = 0;
unsigned long relay2OnAt = 0;

int               phBuffer[10], phTemp;
unsigned long int phAvgVal;
float             calibration_value = 22.84f;
float             phSlope           = -5.70f;

bool              calibMode      = false;
unsigned long     btnPressStart  = 0;
bool              btnWasPressed  = false;

int               lcdPage        = 0;
unsigned long     lastLcdChange  = 0;

void publishRelayState(int relayId, bool state);
void setRelay1(bool state, bool publish = true);
void setRelay2(bool state, bool publish = true);
float readVoltageRaw();
float readPH();

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="id"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dashboard Hydroponic</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#e0e0e0;margin:0;padding:16px;min-height:100vh}
.c{max-width:480px;margin:0 auto}
h1{text-align:center;font-size:1.5rem;background:linear-gradient(90deg,#00d2ff,#3a7bd5);-webkit-background-clip:text;-webkit-text-fill-color:transparent;margin-bottom:20px}
.card{background:rgba(255,255,255,.07);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.12);border-radius:12px;padding:16px;margin-bottom:12px;text-align:center}
.v{font-size:3rem;font-weight:bold;color:#00d2ff;margin:10px 0}
.btn{width:100%;padding:12px;border:none;border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer;margin-bottom:10px;color:#fff;transition:0.2s}
.btn.on{background:linear-gradient(135deg,#11998e,#38ef7d)}
.btn.off{background:rgba(255,255,255,0.1);border:1px solid rgba(255,65,108,0.5);}
.btn.cfg{background:linear-gradient(135deg,#00d2ff,#3a7bd5)}
.lbl{font-size:.8rem;color:#a0a0c0;text-transform:uppercase}
.row{display:flex;gap:10px}
.row>div{flex:1}
</style></head><body><div class="c">
<h1>💧 Hydroponic Dashboard</h1>
<div class="card"><div class="lbl">pH Saat Ini</div><div class="v" id="phv">—</div>
<div class="lbl">Threshold: <span id="thv">—</span> | Mode: <span id="mdv">—</span></div></div>
<div class="card"><div class="lbl">Kontrol Relay Manual</div>
<div class="row">
<div><button id="r1" class="btn off" onclick="t(1)">Asam OFF</button></div>
<div><button id="r2" class="btn off" onclick="t(2)">Basa OFF</button></div>
</div>
<button id="mdbg" class="btn cfg" style="margin-top:10px" onclick="tm()">Ubah Mode</button>
</div>
<div class="card"><a href="/config"><button class="btn cfg">⚙️ Konfigurasi Sistem</button></a>
<a href="/calib"><button class="btn cfg" style="background:linear-gradient(135deg,#ff416c,#ff4b2b)">🧪 Kalibrasi pH</button></a></div>
</div>
<script>
function fetchS(){fetch('/api/status').then(r=>r.json()).then(d=>{
document.getElementById('phv').innerText=d.ph.toFixed(2);
document.getElementById('thv').innerText=d.threshold.toFixed(2);
document.getElementById('mdv').innerText=d.mode;
const rb1=document.getElementById('r1');const rb2=document.getElementById('r2');
rb1.className='btn '+(d.r1?'on':'off');rb1.innerText='Asam '+(d.r1?'ON':'OFF');
rb2.className='btn '+(d.r2?'on':'off');rb2.innerText='Basa '+(d.r2?'ON':'OFF');
const mdb=document.getElementById('mdbg');
mdb.innerText=d.mode==='manual'?'Ubah Mode ke Otomatis':'Ubah Mode ke Manual';
}).catch(()=>{});}
function t(id){fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'target=relay'+id}).then(()=>fetchS());}
function tm(){fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'target=mode'}).then(()=>fetchS());}
setInterval(fetchS,2000);fetchS();
</script></body></html>
)rawliteral";

const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="id"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Konfigurasi Sistem</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#e0e0e0;margin:0;padding:16px}
.c{max-width:480px;margin:0 auto}
h1{text-align:center;font-size:1.5rem;color:#00d2ff;margin-bottom:20px}
.card{background:rgba(255,255,255,.07);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.12);border-radius:12px;padding:16px;margin-bottom:12px}
label{display:block;font-size:.85rem;color:#a0a0c0;margin-bottom:4px}
input{width:100%;box-sizing:border-box;padding:10px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.15);border-radius:6px;color:#fff;margin-bottom:12px}
.btn{width:100%;padding:12px;background:linear-gradient(135deg,#11998e,#38ef7d);border:none;border-radius:8px;color:#fff;font-weight:bold;cursor:pointer}
.btn-b{background:linear-gradient(135deg,#00d2ff,#3a7bd5);margin-top:10px;text-align:center;display:block;text-decoration:none;box-sizing:border-box;}
</style>
</head><body><div class="c">
<h1>⚙️ Konfigurasi Sistem</h1>
<form action="/api/saveConfig" method="POST">
<div class="card">
<label>WiFi SSID</label><input type="text" name="ssid" id="ssid">
<label>WiFi Password</label><input type="password" name="pass" id="pass">
</div>
<div class="card">
<label>API Base URL</label><input type="text" name="api" id="api">
<label>User ID</label><input type="text" name="user" id="user">
<label>WebSocket Host</label><input type="text" name="wsh" id="wsh">
<label>WebSocket Path</label><input type="text" name="wsp" id="wsp">
</div>
<button type="submit" class="btn">💾 Simpan & Restart</button>
</form>
<a href="/" class="btn btn-b">🔙 Kembali</a>
</div>
<script>
fetch('/api/getConfig').then(r=>r.json()).then(d=>{
document.getElementById('ssid').value=d.ssid||'';
document.getElementById('pass').value=d.pass||'';
document.getElementById('api').value=d.api||'';
document.getElementById('user').value=d.user||'';
document.getElementById('wsh').value=d.wsh||'';
document.getElementById('wsp').value=d.wsp||'';
});
</script></body></html>
)rawliteral";

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
.btn{display:block;width:100%;padding:13px;border:none;border-radius:10px;font-size:.95rem;font-weight:600;cursor:pointer;transition:all .2s;margin-bottom:10px;text-align:center;text-decoration:none}
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
<a href="/" class="btn bb" style="margin-bottom:10px">🔙 Kembali ke Dashboard</a>
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


void saveConfig() {
  prefs.begin("hydro-cfg", false);
  prefs.putString("ws_host",  cfgWsHost);
  prefs.putString("ws_path",  cfgWsPath);
  prefs.putString("api_base", cfgApiBase);
  prefs.putString("user_id",  cfgUserId);
  prefs.putString("wifi_ssid",cfgWifiSsid);
  prefs.putString("wifi_pass",cfgWifiPass);
  prefs.end();
  Serial.println("[NVS] Config saved.");
}

void loadConfig() {
  prefs.begin("hydro-cfg", true);
  String h = prefs.getString("ws_host",  cfgWsHost);
  String p = prefs.getString("ws_path",  cfgWsPath);
  String a = prefs.getString("api_base", cfgApiBase);
  String u = prefs.getString("user_id",  cfgUserId);
  String ws= prefs.getString("wifi_ssid",cfgWifiSsid);
  String wp= prefs.getString("wifi_pass",cfgWifiPass);
  prefs.end();
  strncpy(cfgWsHost,   h.c_str(), sizeof(cfgWsHost));
  strncpy(cfgWsPath,   p.c_str(), sizeof(cfgWsPath));
  strncpy(cfgApiBase,  a.c_str(), sizeof(cfgApiBase));
  strncpy(cfgUserId,   u.c_str(), sizeof(cfgUserId));
  strncpy(cfgWifiSsid, ws.c_str(),sizeof(cfgWifiSsid));
  strncpy(cfgWifiPass, wp.c_str(),sizeof(cfgWifiPass));
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

float readVoltageRaw() {
  int buf[10], tmp; unsigned long acc = 0;
  for (int i = 0; i < 10; i++) { buf[i] = analogRead(PH_SENSOR_PIN); delay(30); }
  for (int i = 0; i < 9; i++)
    for (int j = i+1; j < 10; j++)
      if (buf[i] > buf[j]) { tmp=buf[i]; buf[i]=buf[j]; buf[j]=tmp; }
  for (int i = 2; i < 8; i++) acc += buf[i];
  return (float)acc * 3.3f / 4095.0f / 6.0f;
}

float readPH() {
  float volt = readVoltageRaw();
  return constrain(phSlope * volt + calibration_value, 0.0f, 14.0f);
}

// ======================= Calibration Endpoints ======================
void hCalibRoot() { server.send_P(200, "text/html", CALIB_HTML); }
void hCalibRead() {
  float volt = readVoltageRaw();
  float ph   = constrain(phSlope * volt + calibration_value, 0.0f, 14.0f);
  String j = "{\"ph\":" + String(ph,4) + ",\"volt\":" + String(volt,4)
           + ",\"slope\":" + String(phSlope,4) + ",\"offset\":" + String(calibration_value,4) + "}";
  server.send(200, "application/json", j);
}
void hCalib2pt() {
  if (!server.hasArg("v4") || !server.hasArg("v7")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  float v4 = server.arg("v4").toFloat();
  float v7 = server.arg("v7").toFloat();
  if (fabsf(v7 - v4) < 0.01f) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Selisih voltase terlalu kecil\"}"); return;
  }
  phSlope           = (7.0f - 4.0f) / (v7 - v4);
  calibration_value = 7.0f - phSlope * v7;
  saveCalibration();
  String j = "{\"ok\":true,\"slope\":" + String(phSlope,4) + ",\"offset\":" + String(calibration_value,4) + "}";
  server.send(200, "application/json", j);
}
void hCalib1pt() {
  if (!server.hasArg("ref") || !server.hasArg("volt")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  calibration_value = server.arg("ref").toFloat() - phSlope * server.arg("volt").toFloat();
  saveCalibration();
  server.send(200, "application/json", "{\"ok\":true,\"offset\":" + String(calibration_value,4) + "}");
}
void hCalibExit() { server.send(200, "text/plain", "OK"); delay(1500); ESP.restart(); }

// ========================= Server Setup =============================
void setupServerRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  
  server.on("/config", HTTP_GET, []() {
    server.send_P(200, "text/html", CONFIG_HTML);
  });
  
  server.on("/api/status", HTTP_GET, []() {
    float ph = readPH();
    String j = "{\"ph\":" + String(ph,2) + ",\"threshold\":" + String(threshold,2) + 
               ",\"mode\":\"" + currentMode + "\",\"r1\":" + (relay1State?"true":"false") + 
               ",\"r2\":" + (relay2State?"true":"false") + "}";
    server.send(200, "application/json", j);
  });
  
  server.on("/api/getConfig", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["ssid"] = cfgWifiSsid;
    doc["pass"] = cfgWifiPass;
    doc["api"]  = cfgApiBase;
    doc["user"] = cfgUserId;
    doc["wsh"]  = cfgWsHost;
    doc["wsp"]  = cfgWsPath;
    String j; serializeJson(doc, j);
    server.send(200, "application/json", j);
  });
  
  server.on("/api/control", HTTP_POST, []() {
    if (server.hasArg("target")) {
      String tgt = server.arg("target");
      if (tgt == "relay1" && currentMode == "manual") { setRelay1(!relay1State); }
      else if (tgt == "relay2" && currentMode == "manual") { setRelay2(!relay2State); }
      else if (tgt == "mode") { 
        currentMode = (currentMode == "manual") ? "otomatis" : "manual"; 
        if (currentMode == "manual") {
          if (relay1State) setRelay1(false);
          if (relay2State) setRelay2(false);
        }
      }
    }
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/api/saveConfig", HTTP_POST, []() {
    if(server.hasArg("ssid")) strncpy(cfgWifiSsid, server.arg("ssid").c_str(), sizeof(cfgWifiSsid));
    if(server.hasArg("pass")) strncpy(cfgWifiPass, server.arg("pass").c_str(), sizeof(cfgWifiPass));
    if(server.hasArg("api"))  strncpy(cfgApiBase, server.arg("api").c_str(), sizeof(cfgApiBase));
    if(server.hasArg("user")) strncpy(cfgUserId, server.arg("user").c_str(), sizeof(cfgUserId));
    if(server.hasArg("wsh"))  strncpy(cfgWsHost, server.arg("wsh").c_str(), sizeof(cfgWsHost));
    if(server.hasArg("wsp"))  strncpy(cfgWsPath, server.arg("wsp").c_str(), sizeof(cfgWsPath));
    saveConfig();
    server.send(200, "text/html", "<body><h2 style='text-align:center;'>Tersimpan! Restarting...</h2></body>");
    delay(1000);
    ESP.restart();
  });
  
  // Calibration
  server.on("/calib", HTTP_GET, hCalibRoot);
  server.on("/read", HTTP_GET, hCalibRead);
  server.on("/calib2pt", HTTP_GET, hCalib2pt);
  server.on("/calib1pt", HTTP_GET, hCalib1pt);
  server.on("/exit", HTTP_GET, hCalibExit);
}

void startAPMode() {
  WiFi.disconnect(true); delay(500);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Hydroponic_AP", "12345678");
  isAPMode = true;
  Serial.print("[WiFi] Started AP. IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupWiFi() {
  if (strlen(cfgWifiSsid) == 0) {
    Serial.println("[WiFi] No SSID saved. Starting AP mode.");
    startAPMode();
    return;
  }
  Serial.print("[WiFi] Connecting to ");
  Serial.println(cfgWifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgWifiSsid, cfgWifiPass);

  unsigned long startT = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startT < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
    isAPMode = false;
  } else {
    Serial.println("[WiFi] Connection failed. Starting AP mode.");
    startAPMode();
  }
}

// ======================= Relay Control ==============================
void publishRelayState(int relayId, bool state) {
  if (!wsConnected || isAPMode) return;
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
  digitalWrite(RELAY1_PIN, state ? 0 : 1);
  if (state) relay1OnAt = millis();
  Serial.printf("[Relay1/Asam] %s\n", state ? "ON" : "OFF");
  if (publish) publishRelayState(1, state);
}

void setRelay2(bool state, bool publish) {
  relay2State = state;
  digitalWrite(RELAY2_PIN, state ? 0 : 1);
  if (state) relay2OnAt = millis();
  Serial.printf("[Relay2/Basa] %s\n", state ? "ON" : "OFF");
  if (publish) publishRelayState(2, state);
}

void autoModeControl(float ph) {
  if (currentMode != "otomatis") return;
  unsigned long now = millis();

  if (ph > threshold + PH_DEAD_BAND) {
    if (!relay1State) setRelay1(true);
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) setRelay2(false);
  }
  else if (ph < threshold - PH_DEAD_BAND) {
    if (!relay2State) setRelay2(true);
    if (relay1State && (now - relay1OnAt > RELAY_MIN_ON_TIME)) setRelay1(false);
  }
  else {
    if (relay1State && (now - relay1OnAt > RELAY_MIN_ON_TIME)) setRelay1(false);
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) setRelay2(false);
  }
}

// ======================== API FETCHING ==============================
void fetchThresholdFromAPI() {
  if (isAPMode || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(cfgApiBase) + "/api/tr?user=" + cfgUserId;
  Serial.println("[API] Fetching threshold: " + url);
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, body) && doc["success"].as<bool>()) {
      float val = doc["data"]["threshold"].as<String>().toFloat();
      if (val > 0.0f) {
        threshold = val;
        thresholdLoaded = true;
        saveThreshold(threshold);
        Serial.printf("[API] Threshold OK: %.2f\n", threshold);
      }
    }
  }
  http.end();
}

void fetchModeFromAPI() {
  if (isAPMode || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(cfgApiBase) + "/api/mode?user=" + cfgUserId;
  Serial.println("[API] Fetching mode: " + url);
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, body) && doc["success"].as<bool>()) {
      String modeStr = doc["data"]["mode"].as<String>();
      if (modeStr == "otomatis" || modeStr == "manual") {
        currentMode = modeStr;
        Serial.println("[API] Mode loaded: " + currentMode);
      }
    }
  }
  http.end();
}

// ========================= WebSocket ================================
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

  if (topic == topicRelay1) {
    if (currentMode == "manual") {
      bool newState = parseBool(payload);
      if (newState != relay1State) setRelay1(newState, false);
    }
  }
  else if (topic == topicRelay2) {
    if (currentMode == "manual") {
      bool newState = parseBool(payload);
      if (newState != relay2State) setRelay2(newState, false);
    }
  }
  else if (topic == topicMode) {
    String newMode = payload.as<String>();
    if (newMode == "otomatis" || newMode == "manual") {
      currentMode = newMode;
      Serial.println("[Mode] Changed to: " + currentMode);
      if (currentMode == "manual") {
        if (relay1State) setRelay1(false);
        if (relay2State) setRelay2(false);
      }
    }
  }
  else if (topic == topicThresh) {
    float newVal = payload.is<float>() || payload.is<int>() ? payload.as<float>() : payload.as<String>().toFloat();
    if (newVal > 0.0f) {
      threshold = newVal;
      saveThreshold(threshold);
      Serial.printf("[Threshold] Updated via WS: %.2f\n", threshold);
    }
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      wsConnected = true;
      Serial.println("[WS] ✅ Connected");
      const char* topics[] = { "data/relay1/user/", "data/relay2/user/", "data/mode/user/", "data/treshold/user/" };
      for (auto t : topics) {
        StaticJsonDocument<160> doc;
        doc["action"] = "subscribe";
        doc["topic"]  = String(t) + cfgUserId;
        String msg; serializeJson(doc, msg);
        webSocket.sendTXT(msg);
      }
      if (isFirstConnect) {
        isFirstConnect = false;
        relay1State = false; relay2State = false;
        digitalWrite(RELAY1_PIN, 1); digitalWrite(RELAY2_PIN, 1);
        publishRelayState(1, false); publishRelayState(2, false);
      }
      break;
    }
    case WStype_TEXT: {
      String msgStr = String((char*)payload);
      StaticJsonDocument<512> doc;
      if (!deserializeJson(doc, msgStr)) {
        String topic = doc["topic"].as<String>();
        if (topic.length() > 0) handleWsMessage(topic, doc["payload"]);
      }
      break;
    }
    case WStype_DISCONNECTED:
      wsConnected = false; Serial.println("[WS] ⚠ Disconnected"); break;
    case WStype_ERROR:
      Serial.println("[WS] ❌ Error"); break;
    default: break;
  }
}

void updateLCD(float ph) {
  lcd.clear();
  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("pH:"); lcd.print(ph, 2);
    lcd.print(" T:"); lcd.print(threshold, 1);
    lcd.setCursor(0, 1);
    lcd.print(currentMode == "otomatis" ? "Auto" : "Mnl ");
    lcd.print(" R1:"); lcd.print(relay1State ? "1" : "0");
    lcd.print(" R2:"); lcd.print(relay2State ? "1" : "0");
  } else if (lcdPage == 1) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi: ");
    if (isAPMode) lcd.print("AP Mode");
    else if (WiFi.status() == WL_CONNECTED) lcd.print("Connected");
    else lcd.print("Disconnected");
    lcd.setCursor(0, 1);
    lcd.print("IP:");
    lcd.print(isAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  } else if (lcdPage == 2) {
    lcd.setCursor(0, 0);
    lcd.print("WS: "); lcd.print(wsConnected ? "Connected" : "Wait...");
    lcd.setCursor(0, 1);
    lcd.print("API: "); lcd.print((!isAPMode && WiFi.status()==WL_CONNECTED) ? "Ready" : "No Net");
  }
}

// ======================== SETUP =====================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n═══ Hydroponic pH Controller (Offline Mode Ready) ═══");

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" Hydroponic pH  ");
  lcd.setCursor(0, 1);
  lcd.print("   Controller   ");

  pinMode(RELAY1_PIN,   OUTPUT);
  pinMode(RELAY2_PIN,   OUTPUT);
  pinMode(CALIB_BTN_PIN, INPUT_PULLUP);
  digitalWrite(RELAY1_PIN, 1);
  digitalWrite(RELAY2_PIN, 1);

  loadCalibration();
  loadConfig();

  float stored = loadThreshold();
  if (stored > 0.0f) {
    threshold = stored;
    thresholdLoaded = true;
    Serial.printf("[NVS] Threshold loaded: %.2f\n", threshold);
  }

  // Setup WiFi or AP based on config & availability
  setupWiFi();

  // Initialize Web Server routes and start server
  setupServerRoutes();
  server.begin();
  Serial.println("[Server] WebServer started on port 80.");

  if (digitalRead(CALIB_BTN_PIN) == LOW) {
    Serial.println("[CALIB] D0 ditekan saat boot -> Mode Kalibrasi Dedikasi");
    calibMode = true;
    lcd.clear(); lcd.print(" Mode Kalibrasi ");
  }

  if (!isAPMode) {
    fetchThresholdFromAPI();
    if (!thresholdLoaded) { threshold = 6.5f; Serial.printf("[Threshold] Default: %.2f\n", threshold); }
    fetchModeFromAPI();
    Serial.println("[Mode] Initial mode: " + currentMode);

    webSocket.beginSSL(cfgWsHost, 443, cfgWsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    webSocket.enableHeartbeat(15000, 3000, 2);
  }
  
  publishRelayState(1, relay1State);
  publishRelayState(2, relay2State);
  Serial.println("═══ Setup selesai ═══\n");
}

// ======================== LOOP ======================================
unsigned long lastPhSend    = 0;
unsigned long lastWifiCheck = 0;

void loop() {
  server.handleClient(); // Always serve Web UI

  if (calibMode) {
    return; // Dedicated calib mode loops here
  }

  if (!isAPMode) {
    webSocket.loop();
  }

  unsigned long now = millis();

  // Check calibration long press
  if (digitalRead(CALIB_BTN_PIN) == LOW) {
    if (!btnWasPressed) { btnWasPressed = true; btnPressStart = now; }
    else if (now - btnPressStart >= CALIB_BTN_HOLD_MS) {
      Serial.println("[CALIB] D0 ditekan 3 detik -> Masuk mode kalibrasi dedikasi");
      calibMode = true;
      lcd.clear(); lcd.print(" Mode Kalibrasi ");
      return;
    }
  } else {
    btnWasPressed = false;
  }

  // WiFi Reconnect Logic
  if (!isAPMode && now - lastWifiCheck >= 30000) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Lost connection. Reconnecting...");
      WiFi.reconnect();
    }
  }

  // LCD Paging Logic
  if (now - lastLcdChange >= LCD_PAGE_INTERVAL) {
    lastLcdChange = now;
    lcdPage = (lcdPage + 1) % 3;
    updateLCD(readPH()); // Update immediately on page change
  }

  // Main task: Read pH, Print to LCD/Serial, Publish to WS, Auto Control
  if (now - lastPhSend >= PH_SEND_INTERVAL) {
    lastPhSend = now;
    float ph = readPH();
    
    // Update LCD
    updateLCD(ph);

    // Auto Control
    if (currentMode == "otomatis") autoModeControl(ph);

    // Serial & WS Publish
    Serial.printf("[pH] %.2f | Mode: %s | Threshold: %.2f | R1:%s R2:%s\n",
                  ph, currentMode.c_str(), threshold,
                  relay1State ? "ON" : "OFF", relay2State ? "ON" : "OFF");

    if (wsConnected && !isAPMode) {
      StaticJsonDocument<200> doc;
      doc["action"]             = "publish";
      doc["topic"]              = String("data/ph/user/") + cfgUserId;
      doc["payload"]["sensor1"] = round(ph * 100.0f) / 100.0f;
      String msg; serializeJson(doc, msg);
      webSocket.sendTXT(msg);
    }
  }
}
