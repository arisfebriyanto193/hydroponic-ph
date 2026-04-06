const db = require('./db');

// Maksimum jumlah data yang disimpan per user (harus sama dengan di wsClient.js)
const MAX_RECORDS = 250;

// Cleanup interval: jalankan setiap 1 jam sebagai safety net
const CLEANUP_INTERVAL = 60 * 60 * 1000; // 1 Jam

async function enforceCircularBuffer() {
  try {
    console.log('[Cleanup] Menjalankan pengecekan circular buffer...');

    // Ambil semua user_id yang ada di ph_logs
    const [users] = await db.query('SELECT DISTINCT user_id FROM ph_logs');

    for (const { user_id } of users) {
      const [[{ total }]] = await db.query(
        `SELECT COUNT(*) AS total FROM ph_logs WHERE user_id = ?`,
        [user_id]
      );

      if (total > MAX_RECORDS) {
        const excess = total - MAX_RECORDS;
        // Hapus sejumlah 'excess' data terlama
        await db.query(
          `DELETE FROM ph_logs WHERE user_id = ? ORDER BY id ASC LIMIT ?`,
          [user_id, excess]
        );
        console.log(`[Cleanup] User ${user_id}: hapus ${excess} data terlama (sisa: ${MAX_RECORDS})`);
      } else {
        console.log(`[Cleanup] User ${user_id}: ${total}/${MAX_RECORDS} record, tidak perlu hapus`);
      }
    }
  } catch (error) {
    console.error('[Cleanup Error]', error);
  }
}

// Jalankan setiap 1 jam
setInterval(enforceCircularBuffer, CLEANUP_INTERVAL);

// Jalankan sekali saat server start (delay 5 detik)
setTimeout(enforceCircularBuffer, 5000);
