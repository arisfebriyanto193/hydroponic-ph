require('dotenv').config();
const mysql = require('mysql2/promise');

async function migrate() {
  try {
    // Connect without database first to create it if it doesn't exist
    const connection = await mysql.createConnection({
      host: process.env.DB_HOST || 'localhost',
      user: process.env.DB_USER || 'root',
      password: process.env.DB_PASSWORD || '',
    });

    const dbName = process.env.DB_NAME || 'hydroponik_db';
    await connection.query(`CREATE DATABASE IF NOT EXISTS \`${dbName}\``);
    console.log(`Database ${dbName} created or exists.`);
    
    await connection.query(`USE \`${dbName}\``);

    // Create device_states table
    await connection.query(`
      CREATE TABLE IF NOT EXISTS device_states (
        user_id VARCHAR(50) PRIMARY KEY,
        mode VARCHAR(20) DEFAULT 'manual',
        threshold VARCHAR(255) DEFAULT NULL,
        relay1_status BOOLEAN DEFAULT false,
        relay2_status BOOLEAN DEFAULT false,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
      )
    `);
    console.log('Table device_states created.');

    // Create ph_logs table
    await connection.query(`
      CREATE TABLE IF NOT EXISTS ph_logs (
        id INT AUTO_INCREMENT PRIMARY KEY,
        user_id VARCHAR(50),
        sensor_type VARCHAR(50) DEFAULT 'ph',
        value FLOAT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        INDEX(user_id, created_at)
      )
    `);
    console.log('Table ph_logs created.');

    // Create settings table
    await connection.query(`
      CREATE TABLE IF NOT EXISTS settings (
        id INT AUTO_INCREMENT PRIMARY KEY,
        user_id VARCHAR(50),
        retention_days INT DEFAULT 2,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        UNIQUE(user_id)
      )
    `);
    console.log('Table settings created.');

    // Insert dummy initial data for user 9911
    await connection.query(`
      INSERT IGNORE INTO device_states (user_id, mode) VALUES ('9911', 'manual')
    `);
    await connection.query(`
      INSERT IGNORE INTO settings (user_id, retention_days) VALUES ('9911', 2)
    `);
    console.log('Initial data inserted.');

    await connection.end();
    console.log('Migration completed successfully.');
  } catch (error) {
    console.error('Migration failed:', error);
  }
}

migrate();
