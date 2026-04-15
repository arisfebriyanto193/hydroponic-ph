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
float  phDeadband       = 0.5f;
bool   thresholdLoaded  = false;
bool   isFirstConnect   = true;
bool   isAPMode         = false;

unsigned long relay1OnAt = 0;
unsigned long relay2OnAt = 0;

int               phBuffer[10], phTemp;
unsigned long int phAvgVal;
float             calib_ph_mid     = 6.86f;
float             calib_volt_mid   = 2.50f;
float             calib_slope_acid = -4.25f;
float             calib_slope_base = -4.25f;

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
input{width:100%;box-sizing:border-box;padding:10px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.15);border-radius:6px;color:#fff;}
input:focus{outline:none;border-color:#00d2ff}
</style></head><body><div class="c">
<h1>💧 Hydroponic Dashboard</h1>
<div class="card"><div class="lbl">pH Saat Ini</div><div class="v" id="phv">—</div>
<div class="lbl">Threshold: <span id="thv">—</span> ± <span id="dbv">—</span> | Mode: <span id="mdv">—</span></div></div>
<div class="card">
<div class="lbl">Atur Threshold & Toleransi (±)</div>
<div class="row" style="margin-top:10px; align-items:center;">
<div><input type="number" id="inTh" step="0.1" placeholder="pH"></div>
<div><input type="number" id="inDb" step="0.01" placeholder="Toleransi ±"></div>
<div style="flex:0.5;"><button class="btn cfg" style="margin:0;" onclick="svTh()">Set</button></div>
</div>
</div>
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
document.getElementById('dbv').innerText=d.deadband.toFixed(2);
if(document.activeElement !== document.getElementById('inTh')) document.getElementById('inTh').value=d.threshold.toFixed(2);
if(document.activeElement !== document.getElementById('inDb')) document.getElementById('inDb').value=d.deadband.toFixed(2);
document.getElementById('mdv').innerText=d.mode;
const rb1=document.getElementById('r1');const rb2=document.getElementById('r2');
rb1.className='btn '+(d.r1?'on':'off');rb1.innerText='Asam '+(d.r1?'ON':'OFF');
rb2.className='btn '+(d.r2?'on':'off');rb2.innerText='Basa '+(d.r2?'ON':'OFF');
const mdb=document.getElementById('mdbg');
mdb.innerText=d.mode==='manual'?'Ubah Mode ke Otomatis':'Ubah Mode ke Manual';
}).catch(()=>{});}
function t(id){fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'target=relay'+id}).then(()=>fetchS());}
function tm(){fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'target=mode'}).then(()=>fetchS());}
function svTh(){
  const th=document.getElementById('inTh').value, db=document.getElementById('inDb').value;
  if(th&&db) fetch('/api/setTreshold',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'thresh='+th+'&dband='+db}).then(()=>fetchS());
}
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
    <div class="vi"><div class="lb">Val Netral</div><div class="vl" id="cpm">—</div></div>
    <div class="vi"><div class="lb">Slp Asam</div><div class="vl" id="csa">—</div></div>
    <div class="vi"><div class="lb">Slp Basa</div><div class="vl" id="csb">—</div></div>
  </div>
</div>
<div class="card"><h2>📐 Kalibrasi Presisi (2 atau 3 Titik)</h2>
  <div class="step">Set 2 atau 3 cairan buffer. Isi form, celupkan, lalu ambil. (Bilas setiap mengganti cairan, biarkan form ke-3 abaikan jika hanya 2 titik).</div>
  
  <div class="ig" style="margin-bottom:8px;"><label>Titik 1 (misal pH Asam 4.01)</label>
    <div style="display:flex;gap:10px;align-items:center;">
      <input type="number" id="p1" step="0.01" value="4.01" style="flex:1;margin:0;">
      <button class="btn bg" style="margin:0;flex:1.5;padding:10px;" onclick="sp(1)">📥 Ambil</button>
    </div>
    <div class="smpl" id="s1" style="margin-top:6px">Terbaca → <span id="v1d">—</span> V ✅</div>
  </div>

  <div class="ig" style="margin-bottom:8px;"><label>Titik 2 (misal pH Netral 6.86)</label>
    <div style="display:flex;gap:10px;align-items:center;">
      <input type="number" id="p2" step="0.01" value="6.86" style="flex:1;margin:0;">
      <button class="btn bg" style="margin:0;flex:1.5;padding:10px;" onclick="sp(2)">📥 Ambil</button>
    </div>
    <div class="smpl" id="s2" style="margin-top:6px">Terbaca → <span id="v2d">—</span> V ✅</div>
  </div>

  <div class="ig" style="margin-bottom:8px;"><label>Titik 3 (misal pH Basa 9.18) - Opsi</label>
    <div style="display:flex;gap:10px;align-items:center;">
      <input type="number" id="p3" step="0.01" value="9.18" style="flex:1;margin:0;">
      <button class="btn bg" style="margin:0;flex:1.5;padding:10px;" onclick="sp(3)">📥 Ambil</button>
    </div>
    <div class="smpl" id="s3" style="margin-top:6px">Terbaca → <span id="v3d">—</span> V ✅</div>
  </div>

  <button class="btn bb" onclick="svM()" id="bm" style="display:none;margin-top:12px">✅ Hitung &amp; Simpan Kalibrasi Multi-Titik</button>
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
let v=[null,null,null,null], lv=0;
function poll(){fetch('/read').then(r=>r.json()).then(d=>{
  document.getElementById('pv').textContent=d.ph.toFixed(2);
  document.getElementById('vv').textContent=d.volt.toFixed(4)+' V';
  if(document.getElementById('cpm')) document.getElementById('cpm').textContent=d.pmid.toFixed(2);
  if(document.getElementById('csa')) document.getElementById('csa').textContent=d.sac.toFixed(3);
  if(document.getElementById('csb')) document.getElementById('csb').textContent=d.sba.toFixed(3);
  lv=d.volt;}).catch(()=>{});}
function sp(i){
  v[i]=lv;
  document.getElementById('v'+i+'d').textContent=v[i].toFixed(4);
  document.getElementById('s'+i).style.display='block';
  let count = 0;
  for(let j=1; j<=3; j++) if(v[j]!==null) count++;
  if(count >= 2) document.getElementById('bm').style.display='block';
}
function ss(m,ok){const e=document.getElementById('sb');e.textContent=m;e.className='st '+(ok?'ok':'err');e.style.display='block';setTimeout(()=>e.style.display='none',5000);}
function svM(){
  let pts = [];
  for(let i=1; i<=3; i++){
    if(v[i]!==null){
      let p = parseFloat(document.getElementById('p'+i).value);
      if(!isNaN(p)) pts.push({p:p, v:v[i]});
    }
  }
  if(pts.length < 2) { ss('Minimal 2 titik sampel diperlukan!',false); return; }
  let q="?";
  for(let i=0; i<pts.length; i++){
    q += "p"+(i+1)+"="+pts[i].p+"&v"+(i+1)+"="+pts[i].v;
    if(i<pts.length-1) q+="&";
  }
  let endpoint = pts.length === 3 ? '/calib3pt' : '/calib2pt';
  fetch(endpoint+q).then(r=>r.json()).then(d=>{
    if(d.ok) ss('✅ Kalibrasi ' + pts.length + ' titik berhasil disimpan!', true);
    else ss('❌ '+d.msg,false);
  }).catch(()=>ss('❌ Koneksi gagal',false));
}
function c1p(){
  const r=parseFloat(document.getElementById('rp').value);
  if(isNaN(r)||r<0||r>14){ss('Masukkan nilai pH 0–14',false);return;}
  fetch('/calib1pt?ref='+r+'&volt='+lv).then(r=>r.json()).then(d=>{
    if(d.ok)ss('✅ Offset 1-Titik disimpan',true);
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

void saveDeadband(float val) {
  prefs.begin("hydro-cfg", false);
  prefs.putFloat("deadband", val);
  prefs.end();
  Serial.printf("[NVS] Deadband saved: %.2f\n", val);
}

float loadDeadband() {
  prefs.begin("hydro-cfg", true);
  float val = prefs.getFloat("deadband", -1.0f);
  prefs.end();
  return val;
}

void saveCalibration() {
  prefs.begin("hydro-cfg", false);
  prefs.putFloat("c_ph_m",   calib_ph_mid);
  prefs.putFloat("c_v_m",    calib_volt_mid);
  prefs.putFloat("c_s_aca",  calib_slope_acid);
  prefs.putFloat("c_s_bas",  calib_slope_base);
  prefs.end();
  Serial.println("[NVS] Calibration 3pt saved.");
}

void loadCalibration() {
  prefs.begin("hydro-cfg", true);
  calib_ph_mid     = prefs.getFloat("c_ph_m", 6.86f);
  calib_volt_mid   = prefs.getFloat("c_v_m",  2.50f);
  
  float old_slope  = prefs.getFloat("ph_slope", -4.25f);
  calib_slope_acid = prefs.getFloat("c_s_aca", old_slope);
  calib_slope_base = prefs.getFloat("c_s_bas", old_slope);
  
  // Backward compatibility with raw 2-point
  if (prefs.getFloat("c_ph_m", -1.0f) == -1.0f) {
     float old_offset = prefs.getFloat("ph_offset", 17.70f);
     calib_volt_mid = (6.86f - old_offset) / old_slope; 
     calib_ph_mid = 6.86f;
  }
  prefs.end();
  Serial.printf("[NVS] Calib: Vm=%.2f Pm=%.2f Sa=%.2f Sb=%.2f\n", calib_volt_mid, calib_ph_mid, calib_slope_acid, calib_slope_base);
}

float emaVoltage = -1.0f;

float readVoltageRaw() {
  // Mengambil 40 sampel untuk kestabilan ekstra (filter noise ADC ESP32)
  int buf[40], tmp; unsigned long acc = 0;
  for (int i = 0; i < 40; i++) { buf[i] = analogRead(PH_SENSOR_PIN); delay(10); } // Total 400ms buffer
  for (int i = 0; i < 39; i++)
    for (int j = i+1; j < 40; j++)
      if (buf[i] > buf[j]) { tmp=buf[i]; buf[i]=buf[j]; buf[j]=tmp; }
  // Ambil rata-rata dari 20 data tengah (buang 10 terendah dan 10 tertinggi)
  for (int i = 10; i < 30; i++) acc += buf[i];
  
  float rawVolt = (float)acc * 3.3f / 4095.0f / 20.0f;
  
  // Implementasi Exponential Moving Average (EMA) untuk smoothing drastis
  if (emaVoltage < 0.0f) {
    emaVoltage = rawVolt; // Inisialisasi pertama kali
  } else {
    // 15% nilai sensor asli sekarang, 85% mengambil riwayat (meredam drastis lompatan noise akibat frekuensi listrik / WiFi ESP32)
    emaVoltage = (0.15f * rawVolt) + (0.85f * emaVoltage);
  }
  
  return emaVoltage;
}

float readPH() {
  float volt = readVoltageRaw();
  float ph;
  if (volt >= calib_volt_mid) {
    ph = calib_ph_mid + calib_slope_acid * (volt - calib_volt_mid);
  } else {
    ph = calib_ph_mid + calib_slope_base * (volt - calib_volt_mid);
  }
  return constrain(ph, 0.0f, 14.0f);
}

// ======================= Calibration Endpoints ======================
void hCalibRoot() { server.send_P(200, "text/html", CALIB_HTML); }
void hCalibRead() {
  float volt = readVoltageRaw();
  float ph   = readPH();
  String j = "{\"ph\":" + String(ph,4) + ",\"volt\":" + String(volt,4)
           + ",\"sac\":" + String(calib_slope_acid,4) + ",\"sba\":" + String(calib_slope_base,4)
           + ",\"pmid\":" + String(calib_ph_mid,4) + "}";
  server.send(200, "application/json", j);
}
void hCalib2pt() {
  if (!server.hasArg("v1") || !server.hasArg("p1") || !server.hasArg("v2") || !server.hasArg("p2")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  float p[2] = {server.arg("p1").toFloat(), server.arg("p2").toFloat()};
  float v[2] = {server.arg("v1").toFloat(), server.arg("v2").toFloat()};
  if(p[0] > p[1]) {
    float tmpp = p[0]; p[0] = p[1]; p[1] = tmpp;
    float tmpv = v[0]; v[0] = v[1]; v[1] = tmpv;
  }
  if (fabsf(v[1] - v[0]) < 0.01f || fabsf(p[1] - p[0]) < 0.01f) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Selisih terlalu kecil\"}"); return;
  }
  float slope = (p[1] - p[0]) / (v[1] - v[0]);
  calib_ph_mid     = p[1];
  calib_volt_mid   = v[1];
  calib_slope_acid = slope;
  calib_slope_base = slope;
  saveCalibration();
  server.send(200, "application/json", "{\"ok\":true}");
}
void hCalib3pt() {
  if (!server.hasArg("v1") || !server.hasArg("p1") || !server.hasArg("v2") || !server.hasArg("p2") || !server.hasArg("v3") || !server.hasArg("p3")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  float p[3] = { server.arg("p1").toFloat(), server.arg("p2").toFloat(), server.arg("p3").toFloat() };
  float v[3] = { server.arg("v1").toFloat(), server.arg("v2").toFloat(), server.arg("v3").toFloat() };
  
  for(int i=0; i<2; i++){
    for(int j=i+1; j<3; j++){
      if(p[i] > p[j]){
        float tmpp = p[i]; p[i] = p[j]; p[j] = tmpp;
        float tmpv = v[i]; v[i] = v[j]; v[j] = tmpv;
      }
    }
  }
  
  if (fabsf(v[1]-v[0]) < 0.01f || fabsf(v[2]-v[1]) < 0.01f || fabsf(p[1]-p[0]) < 0.01f || fabsf(p[2]-p[1]) < 0.01f) {
     server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Titik terlalu dekat\"}"); return;
  }
  
  calib_slope_acid = (p[1] - p[0]) / (v[1] - v[0]);
  calib_slope_base = (p[2] - p[1]) / (v[2] - v[1]);
  calib_ph_mid     = p[1];
  calib_volt_mid   = v[1];
  saveCalibration();
  server.send(200, "application/json", "{\"ok\":true}");
}
void hCalib1pt() {
  if (!server.hasArg("ref") || !server.hasArg("volt")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Parameter kurang\"}"); return;
  }
  calib_ph_mid   = server.arg("ref").toFloat();
  calib_volt_mid = server.arg("volt").toFloat();
  saveCalibration();
  server.send(200, "application/json", "{\"ok\":true}");
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
               ",\"deadband\":" + String(phDeadband,2) +
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
  
  server.on("/api/setTreshold", HTTP_POST, []() {
    if (server.hasArg("thresh") && server.hasArg("dband")) {
      float t = server.arg("thresh").toFloat();
      float d = server.arg("dband").toFloat();
      if (t > 0.0f) {
        threshold = t;
        saveThreshold(threshold);
      }
      if (d > 0.0f) {
        phDeadband = d;
        saveDeadband(phDeadband);
      }
      if (wsConnected && !isAPMode) {
        StaticJsonDocument<160> doc;
        doc["action"]  = "publish";
        doc["topic"]   = String("data/treshold/user/") + cfgUserId;
        doc["payload"] = threshold;
        String msg; serializeJson(doc, msg);
        webSocket.sendTXT(msg);
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
  server.on("/calib3pt", HTTP_GET, hCalib3pt);
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

  if (ph > threshold + phDeadband) {
    if (!relay1State) setRelay1(true);
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) setRelay2(false);
  }
  else if (ph < threshold - phDeadband) {
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

  float dbStored = loadDeadband();
  if (dbStored > 0.0f) {
    phDeadband = dbStored;
    Serial.printf("[NVS] Deadband loaded: %.2f\n", phDeadband);
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
