require('dotenv').config();
const nodemailer = require('nodemailer');
const db = require('../db');
const fs = require('fs');
const path = require('path');

// ─────────────────────────────────────────────
//  File Logger → backend/logs/email.log
// ─────────────────────────────────────────────
const LOG_DIR  = path.join(__dirname, '..', 'logs');
const LOG_FILE = path.join(LOG_DIR, 'email.log');

if (!fs.existsSync(LOG_DIR)) fs.mkdirSync(LOG_DIR, { recursive: true });

function fileLog(level, message) {
  const ts   = new Date().toLocaleString('id-ID', { timeZone: 'Asia/Jakarta' });
  const line = `[${ts}] [${level}] ${message}\n`;
  // Tulis ke file (append)
  fs.appendFile(LOG_FILE, line, (err) => { if (err) console.error('[Logger] Gagal tulis log:', err.message); });
  // Tetap tampil di console PM2
  if (level === 'ERROR') console.error(`[EmailNotif] ${message}`);
  else if (level === 'WARN')  console.warn(`[EmailNotif] ${message}`);
  else                        console.log(`[EmailNotif] ${message}`);
}

// ─────────────────────────────────────────────
//  Konfigurasi Transporter (SMTP)
//  Isi EMAIL_USER & EMAIL_PASS di file .env
// ─────────────────────────────────────────────
const transporter = nodemailer.createTransport({
  service: 'gmail',
  auth: {
    user: process.env.EMAIL_USER,
    pass: process.env.EMAIL_PASS, // gunakan App Password Gmail
  },
});

// ─────────────────────────────────────────────
//  State: simpan waktu terakhir email dikirim
//  agar tidak spam (interval 15 menit)
// ─────────────────────────────────────────────
const lastSentMap = {}; // { user_id: Date }
const INTERVAL_MS = 15 * 60 * 1000; // 15 menit

/**
 * Parse threshold string dari DB.
 * Format yang didukung:
 *   "7"           → { min: 7,   max: 7   }  (nilai tepat → ±0.5 toleransi)
 *   "6-8"         → { min: 6,   max: 8   }
 *   "6.5-7.5"     → { min: 6.5, max: 7.5 }
 */
function parseThreshold(thresholdStr) {
  if (!thresholdStr) return null;

  const str = String(thresholdStr).trim();

  // Format range: "min-max"
  const rangeMatch = str.match(/^([\d.]+)-([\d.]+)$/);
  if (rangeMatch) {
    return {
      min: parseFloat(rangeMatch[1]),
      max: parseFloat(rangeMatch[2]),
    };
  }

  // Format angka tunggal
  const single = parseFloat(str);
  if (!isNaN(single)) {
    return { min: single - 0.5, max: single + 0.5 };
  }

  return null;
}

/**
 * Buat konten email HTML yang informatif.
 */
function buildEmailHTML(userId, phValue, threshold, direction) {
  const now = new Date().toLocaleString('id-ID', { timeZone: 'Asia/Jakarta' });
  const statusColor = direction === 'rendah' ? '#e74c3c' : '#e67e22';
  const icon = direction === 'rendah' ? '⬇️' : '⬆️';
  const label = direction === 'rendah' ? 'DI BAWAH' : 'DI ATAS';

  return `
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8" />
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #f0f4f8; margin: 0; padding: 0; }
    .container { max-width: 560px; margin: 32px auto; background: #ffffff;
                 border-radius: 12px; overflow: hidden;
                 box-shadow: 0 4px 20px rgba(0,0,0,0.10); }
    .header { background: ${statusColor}; padding: 28px 32px; color: #fff; }
    .header h1 { margin: 0; font-size: 22px; font-weight: 700; }
    .header p  { margin: 6px 0 0; font-size: 14px; opacity: 0.90; }
    .body { padding: 28px 32px; }
    .ph-box { background: #f8f9fa; border-left: 4px solid ${statusColor};
              border-radius: 6px; padding: 16px 20px; margin: 16px 0; }
    .ph-box .label { font-size: 12px; color: #6c757d; text-transform: uppercase; letter-spacing: 1px; }
    .ph-box .value { font-size: 36px; font-weight: 800; color: ${statusColor}; line-height: 1.2; }
    .info-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 16px; }
    .info-item { background: #f8f9fa; border-radius: 6px; padding: 12px 16px; }
    .info-item .label { font-size: 11px; color: #6c757d; text-transform: uppercase; letter-spacing: 1px; }
    .info-item .val   { font-size: 16px; font-weight: 600; color: #343a40; margin-top: 4px; }
    .footer { padding: 16px 32px; background: #f8f9fa; font-size: 12px; color: #6c757d; text-align: center; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>${icon} Peringatan pH ${label} Ambang Batas</h1>
      <p>Sistem Hidroponik – Mode Manual</p>
    </div>
    <div class="body">
      <p>Nilai pH saat ini <strong>${label}</strong> ambang batas yang ditetapkan. Segera lakukan penyesuaian secara manual.</p>

      <div class="ph-box">
        <div class="label">Nilai pH Terukur</div>
        <div class="value">${phValue.toFixed(2)}</div>
      </div>

      <div class="info-grid">
        <div class="info-item">
          <div class="label">Ambang Min</div>
          <div class="val">${threshold.min.toFixed(2)}</div>
        </div>
        <div class="info-item">
          <div class="label">Ambang Max</div>
          <div class="val">${threshold.max.toFixed(2)}</div>
        </div>
        <div class="info-item">
          <div class="label">Device / User</div>
          <div class="val">${userId}</div>
        </div>
        <div class="info-item">
          <div class="label">Waktu Notifikasi</div>
          <div class="val">${now} WIB</div>
        </div>
      </div>

      <p style="margin-top:20px; font-size:13px; color:#6c757d;">
        ⏰ Email ini akan dikirim ulang setiap <strong>15 menit</strong> selama kondisi pH belum normal.
      </p>
    </div>
    <div class="footer">
      Sistem Monitoring Hidroponik &copy; ${new Date().getFullYear()}
    </div>
  </div>
</body>
</html>
  `.trim();
}

/**
 * Fungsi utama: cek pH vs threshold & kirim email jika perlu.
 *
 * @param {string} userId  - ID user/device
 * @param {number} phValue - Nilai pH saat ini
 */
async function checkAndNotify(userId, phValue) {
  try {
    // 1. Ambil mode, threshold, dan email_tujuan dari DB
    const [rows] = await db.query(
      `SELECT mode, threshold, email_tujuan FROM device_states WHERE user_id = ?`,
      [userId]
    );

    if (!rows.length) {
      fileLog('WARN', `User ${userId} tidak ditemukan di device_states.`);
      return;
    }

    const { mode, threshold: thresholdStr, email_tujuan } = rows[0];

    // 2. Hanya aktif di mode manual
    if (mode !== 'manual') {
      return; // Mode otomatis — tidak perlu log, terlalu sering
    }

    // 3. Validasi email tujuan
    if (!email_tujuan || !email_tujuan.trim()) {
      fileLog('WARN', `email_tujuan kosong untuk user ${userId}. Skip notifikasi.`);
      return;
    }

    // 4. Parse threshold
    const threshold = parseThreshold(thresholdStr);
    if (!threshold) {
      fileLog('WARN', `Threshold tidak valid untuk user ${userId}: "${thresholdStr}"`);
      return;
    }

    // 5. Cek apakah pH di luar batas
    let direction = null;
    if (phValue < threshold.min) direction = 'rendah';
    else if (phValue > threshold.max) direction = 'tinggi';

    if (!direction) {
      // pH normal — reset timer
      if (lastSentMap[userId]) {
        delete lastSentMap[userId];
        fileLog('INFO', `pH user ${userId} kembali normal (${phValue.toFixed(2)}). Timer direset.`);
      }
      return;
    }

    // 6. Cek interval 15 menit
    const lastSent = lastSentMap[userId];
    const now = Date.now();
    if (lastSent && now - lastSent < INTERVAL_MS) {
      const sisaMenit = Math.ceil((INTERVAL_MS - (now - lastSent)) / 60000);
      fileLog('INFO', `Skip kirim email user ${userId}: interval belum habis, sisa ~${sisaMenit} menit. pH=${phValue.toFixed(2)} (${direction})`);
      return;
    }

    // 7. Kirim email
    const emailTo = email_tujuan.trim();
    const subject = `⚠️ pH ${direction === 'rendah' ? 'Terlalu Rendah' : 'Terlalu Tinggi'} – Hidroponik User ${userId}`;

    fileLog('INFO', `Mencoba kirim email ke ${emailTo} | pH=${phValue.toFixed(2)} | threshold=[${threshold.min}-${threshold.max}] | arah=${direction}`);

    await transporter.sendMail({
      from: `"Sistem Hidroponik" <${process.env.EMAIL_USER}>`,
      to: emailTo,
      subject,
      html: buildEmailHTML(userId, phValue, threshold, direction),
    });

    lastSentMap[userId] = now;
    fileLog('SUCCESS', `✅ Email TERKIRIM ke ${emailTo} | pH=${phValue.toFixed(2)} | threshold=[${threshold.min}-${threshold.max}] | arah=${direction}`);

  } catch (err) {
    fileLog('ERROR', `❌ Gagal checkAndNotify user ${userId}: ${err.message}`);
  }
}

module.exports = { checkAndNotify };
