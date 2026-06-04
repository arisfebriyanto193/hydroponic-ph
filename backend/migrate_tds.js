require('dotenv').config();
const mysql = require('mysql2/promise');

async function migrateTds() {
  try {
    const connection = await mysql.createConnection({
      host: process.env.DB_HOST || 'localhost',
      user: process.env.DB_USER || 'root',
      password: process.env.DB_PASSWORD || '',
      database: process.env.DB_NAME || 'hydroponik_db',
    });

    console.log('Terhubung ke database. Menjalankan migrasi TDS...');

    // Cek apakah kolom sensor_type sudah ada untuk menghindari error
    const [columns] = await connection.query(`SHOW COLUMNS FROM ph_logs LIKE 'sensor_type'`);
    
    if (columns.length === 0) {
      await connection.query(`
        ALTER TABLE ph_logs
        ADD COLUMN sensor_type VARCHAR(50) DEFAULT 'ph' AFTER user_id
      `);
      console.log('✅ Berhasil menambahkan kolom sensor_type ke tabel ph_logs.');
    } else {
      console.log('ℹ️ Kolom sensor_type sudah ada di tabel ph_logs.');
    }

    // Pastikan INDEX sudah optimal untuk query yang baru
    // Kita gunakan TRY CATCH karena MySQL tidak memiliki syntax CREATE INDEX IF NOT EXISTS 
    try {
      await connection.query(`CREATE INDEX idx_user_sensor_time ON ph_logs (user_id, sensor_type, created_at)`);
      console.log('✅ Berhasil menambahkan index untuk mempercepat query.');
    } catch (e) {
      // Abaikan jika index sudah ada
      if (e.code === 'ER_DUP_KEYNAME') {
         console.log('ℹ️ Index sudah ada.');
      }
    }

    await connection.end();
    console.log('Migrasi selesai!');
  } catch (error) {
    console.error('❌ Migrasi gagal:', error);
  }
}

migrateTds();
