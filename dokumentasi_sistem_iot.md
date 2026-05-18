# Dokumentasi Alur Sistem IoT Hidroponik

Sistem ini terdiri dari tiga komponen utama:
1. **Perangkat IoT (ESP32)**: Membaca sensor pH, mengontrol pompa (relay), dan berkomunikasi via WebSocket & API.
2. **Backend (Node.js/Express)**: Menjadi jembatan antara ESP32 dan Frontend, menangani koneksi WebSocket, menyimpan data ke database, dan mengirim notifikasi email.
3. **Frontend (Next.js/React)**: Antarmuka pengguna untuk memonitor nilai pH secara real-time, melihat riwayat, dan mengontrol mode serta pengaturan sistem.

Berikut adalah penjelasan detail bagaimana sistem saling berinteraksi beserta potongan kodenya dari file yang telah diberikan.

---

## 1. Bagaimana Data Dibaca dan Dikirim oleh ESP32
ESP32 membaca nilai tegangan analog dari sensor pH, mengubahnya menjadi nilai pH (menggunakan rumus regresi linear dari proses kalibrasi), lalu mengirimkan datanya ke server backend melalui **WebSocket** secara berkala (setiap 1.5 detik / `PH_SEND_INTERVAL`).

**Potongan Program ESP32 (`programEsp.ino`):**
```cpp
// Di dalam void loop() utama ESP32
if (now - lastPhSend >= PH_SEND_INTERVAL) {
  lastPhSend = now;
  float ph = readPH(); // Membaca nilai pH aktual

  // Jika terhubung ke jaringan dan mode Client (bukan Access Point)
  if (wsConnected && !isAPMode) {
    StaticJsonDocument<200> doc;
    doc["action"]             = "publish";
    doc["topic"]              = String("data/ph/user/") + cfgUserId;
    doc["payload"]["sensor1"] = round(ph * 100.0f) / 100.0f;
    String msg; 
    serializeJson(doc, msg);
    webSocket.sendTXT(msg); // Mengirim data pH dalam format JSON ke server melalui WebSocket
  }
}
```

---

## 2. Bagaimana Relay Dikontrol
Sistem mengendalikan 2 relay (Relay 1 untuk Asam, Relay 2 untuk Basa). Kontrol relay ini bisa dilakukan lewat 2 cara:
- **Dari Dashboard Web (Mode Manual)**: User menekan tombol di antarmuka Web yang akan mengirim perintah via WebSocket, kemudian ESP32 akan menyalakan relay.
- **Dari ESP32 (Mode Otomatis)**: ESP32 mengontrol relay berdasarkan nilai sensor tanpa menunggu perintah Web.

**Potongan Program ESP32 (`programEsp.ino`):**
```cpp
// Fungsi aktuator pengontrol tegangan ke Pin Relay
void setRelay1(bool state, bool publish) {
  relay1State = state;
  digitalWrite(RELAY1_PIN, state ? 0 : 1); // Relay Aktif LOW (0 = Nyala, 1 = Mati)
  if (publish) publishRelayState(1, state); // Memberikan feedback/laporan status ke Web
}

// Menerima perintah dari web via WebSocket (Hanya diproses saat mode manual)
void handleWsMessage(String& topic, JsonVariant payload) {
  if (topic == topicRelay1) {
    if (currentMode == "manual") {
      bool newState = parseBool(payload);
      if (newState != relay1State) setRelay1(newState, false);
    }
  }
}
```

---

## 3. Sistem Mode Otomatis & Threshold
Jika mode "otomatis" diaktifkan, ESP32 akan mengeksekusi logika tertutup (`autoModeControl`). Sistem secara konstan membandingkan nilai pH dengan target (`threshold`) dan nilai toleransi atas/bawah (`deadband`). Jika pH lebih tinggi dari batas wajar, pompa cairan Asam (Relay 1) menyala. Jika pH terlalu rendah, pompa cairan Basa (Relay 2) menyala.

**Potongan Program ESP32 (`programEsp.ino`):**
```cpp
void autoModeControl(float ph) {
  if (currentMode != "otomatis") return;
  unsigned long now = millis();

  if (ph > threshold + phDeadband) { // Kondisi: Terlalu Basa
    if (!relay1State) setRelay1(true); // Nyalakan pompa Asam
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) setRelay2(false);
  }
  else if (ph < threshold - phDeadband) { // Kondisi: Terlalu Asam
    if (!relay2State) setRelay2(true); // Nyalakan pompa Basa
    if (relay1State && (now - relay1OnAt > RELAY_MIN_ON_TIME)) setRelay1(false);
  }
  else { // Kondisi: Normal/Optimal (Sesuai Threshold)
    if (relay1State && (now - relay1OnAt > RELAY_MIN_ON_TIME)) setRelay1(false);
    if (relay2State && (now - relay2OnAt > RELAY_MIN_ON_TIME)) setRelay2(false);
  }
}
```

---

## 4. Penyimpanan Data dan Email oleh Backend
Backend dibangun menggunakan Node.js (Express). Backend berperan sebagai perantara dan otak penyimpanan.
Backend menerima data dari perangkat ESP32 via WebSocket. Data mentah (seperti pH) akan langsung dilempar/broadcast kembali ke user Web agar grafik Web bergerak secara Real-Time. Pada saat yang sama, Backend secara diam-diam menyimpannya ke Database (seperti MySQL/PostgreSQL) secara historis, dan mengecek apabila pH berada di titik bahaya untuk **mengirim Email**.

**Potongan Program Backend (`server.js`):**
```javascript
const express = require('express');
const cors = require('cors');
require('dotenv').config();

const app = express();
app.use(cors());
app.use(express.json());

// Tempat deklarasi route untuk Frontend melakukan HTTP Request (misal: mengambil data riwayat tabel)
app.use('/api', require('./routes/api'));

app.listen(PORT, () => {
  console.log(`[Server] API running on port ${PORT}`);
});

// Modul yang bertugas mengelola lalu-lintas data langsung dari ESP32, 
// memasukkan data log ke DB, serta menjalankan fungsi Email Notification.
require('./wsClient'); 

// Modul penjadwalan (CRON) untuk menghapus data lama (misal 30 hari ke atas) agar DB tidak penuh
require('./cleanup');
```

---

## 5. Frontend: Mengambil dan Menampilkan Data
Aplikasi Front-End (Web) menggunakan React (Next.js) dan dipoles dengan animasi/desain modern. Ada 2 cara Web mendapatkan data:
1. **Real-time (Live)**: Di-handle oleh file *hook* `useHydroponics`, yang me-listen WebSocket dari Backend, kemudian data ini ditampilkan di indikator Live dan Grafik (Chart.js).
2. **Historical (Riwayat)**: Data yang sudah disimpan di Database Backend dapat di-query dengan menggunakan `axios` HTTP GET.

**Potongan Program Frontend (`page.tsx`):**
```tsx
// 1. Hook custom yang menarik dan mengikat seluruh state IoT (pH, koneksi, status relay)
const h = useHydroponics(); 

// UI Menampilkan nilai pH secara Live dari hook tersebut
<div className="ph-box">
  <div className="ph-num">{h.ph !== null ? h.ph.toFixed(2) : '--'}</div>
  <div className="ph-unit">Potential of Hydrogen</div>
</div>

// 2. Fungsi Mengambil Riwayat Data untuk mengisi Tabel berdasarkan tanggal tertentu
const fetchHistoryByDate = async (date: string) => {
  setHistoryLoading(true);
  try {
    // Meminta data dari route `/api/ph` dari backend Node.js
    const res = await axios.get(`${h.API_URL}/api/ph?date=${date}&user=${h.USER_ID}`);
    if (res.data?.success) setHistoryData(res.data.data.reverse());
  } catch (e) {
    // Penanganan eror
  } finally {
    setHistoryLoading(false);
  }
};

// 3. UI Kontrol (Mengatur Email Notifikasi di mode Manual)
<div className="threshold-row">
  <input 
    type="email" 
    className="input-field" 
    value={h.emailTujuan} 
    onChange={(e) => h.setEmailTujuan(e.target.value)}
    placeholder="email@contoh.com" 
  />
  <button className="btn-primary" onClick={h.updateEmailTujuan}>Simpan Email</button>
</div>
```

---

### Rangkuman Alur Sistem (Flow) Keseluruhan
1. **Sensing & Pengiriman**: ESP32 secara konstan membaca voltase pH Sensor -> mengubahnya menjadi angka pH -> mengirim paket data JSON ke URL server Node.js melalui koneksi WebSocket.
2. **Auto vs Manual**: 
   - Jika **Otomatis**, ESP32 mengambil alih kontrol mutlak pada Relay 1 dan 2 berdasarkan nilai `threshold`.
   - Jika **Manual**, user menekan tombol Asam/Basa di Frontend -> Frontend kirim sinyal ke Backend -> Backend teruskan sinyal ke ESP32 -> ESP32 mengeksekusi pin relay.
3. **Database & API**: Backend Node.js menangkap aliran data pH dan menabungnya di Database secara berjangka. Frontend (Next.js) dapat sewaktu-waktu memanggil HTTP GET (via Axios) untuk melihat riwayat data tanggal-tanggal sebelumnya dan menampilkannya di tabel serta men-download rekap datanya.
4. **Email Alerting**: Saat nilai pH melanggar batas kritis yang diatur pada dashboard, backend akan mengambil inisiatif memanggil module email (`service/email_notif.js`) menggunakan NodeMailer dan mengirim *alert warning* secara otomatis ke email si pemilik.
