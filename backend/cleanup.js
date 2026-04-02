const db = require('./db');
const cron = require('node-cron');

// Cleanup job berjalan setiap 1 jam sekali, 
// atau bisa pakai setInterval kalau tidak mau tambahkan node-cron
// Di sini saya pakai setInterval per 1 jam agar tanpa dependency ekstra.

const CLEANUP_INTERVAL = 60 * 60 * 1000; // 1 Jam

setInterval(async () => {
  try {
    console.log('[Cleanup] Menjalankan job penghapusan data lama...');
    
    // Ambil data users dan retention_days (berapa lama disisakan) dari table settings
    const [settings] = await db.query('SELECT user_id, retention_days FROM settings');
    
    for (const setting of settings) {
      const userId = setting.user_id;
      const days = setting.retention_days || 2; 

      const [result] = await db.query(
        `DELETE FROM ph_logs 
         WHERE user_id = ? 
         AND created_at < NOW() - INTERVAL ? DAY`,
        [userId, days]
      );
      
      if (result.affectedRows > 0) {
         console.log(`[Cleanup] Menghapus ${result.affectedRows} baris usang untuk user ${userId}`);
      }
    }
  } catch (error) {
    console.error('[Cleanup Error]', error);
  }
}, CLEANUP_INTERVAL);

// Jalankan saat start langsung 1 kali
setTimeout(async () => {
  try {
    console.log('[Cleanup] Init check');
    const [settings] = await db.query('SELECT user_id, retention_days FROM settings');
    for (const setting of settings) {
      await db.query(
        `DELETE FROM ph_logs WHERE user_id = ? AND created_at < NOW() - INTERVAL ? DAY`,
        [setting.user_id, setting.retention_days || 2]
      );
    }
  } catch(e) {}
}, 5000);
