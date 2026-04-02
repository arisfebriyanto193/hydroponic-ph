const express = require('express');
const router = express.Router();
const db = require('../db');

// GET /api/ph?days=1
// Mengambil data pH untuk x hari ke belakang
// Ambil treshold dari db
router.get('/tr', async (req, res) => {
  try {
    const userId = req.query.user || '9911';
    const [rows] = await db.query('SELECT treshold FROM device_states WHERE user_id = ?', [userId]);
    
    if (rows.length > 0) {
      res.json({ success: true, data: rows[0] });
    } else {
      res.status(404).json({ success: false, message: 'Treshold not found' });
    }
  } catch (error) {
    console.error('Error fetching treshold:', error);
    res.status(500).json({ success: false, message: 'Server Error' });
  }
});

//ambil status relay 1/2
router.get('/relay', async (req, res) => {
  try {
    const userId = req.query.user || '9911';
    const [rows] = await db.query('SELECT relay1_status, relay2_status, mode FROM device_states WHERE user_id = ?', [userId]);
    
    if (rows.length > 0) {
      res.json({ success: true, data: rows[0] });
    } else {
      res.status(404).json({ success: false, message: 'Relay not found' });
    }
  } catch (error) {
    console.error('Error fetching relay:', error);
    res.status(500).json({ success: false, message: 'Server Error' });
  }
});


router.get('/ph', async (req, res) => {
  try {
    const days = parseInt(req.query.days) || 1;
    const userId = req.query.user || '9911';

    // Using INTERVAL in MySQL to filter logic
    const [rows] = await db.query(
      `SELECT * FROM ph_logs 
       WHERE user_id = ? 
       AND created_at >= NOW() - INTERVAL ? DAY 
       ORDER BY created_at DESC`,
      [userId, days]
    );

    res.json({
      success: true,
      data: rows,
      days_requested: days
    });
  } catch (error) {
    console.error('Error fetching pH data:', error);
    res.status(500).json({ success: false, message: 'Server Error' });
  }
});

// GET /api/settings
router.get('/settings', async (req, res) => {
  try {
    const userId = req.query.user || '9911';
    const [rows] = await db.query('SELECT retention_days FROM settings WHERE user_id = ?', [userId]);
    
    if (rows.length > 0) {
      res.json({ success: true, data: rows[0] });
    } else {
      res.status(404).json({ success: false, message: 'Settings not found' });
    }
  } catch (error) {
    console.error('Error fetching settings:', error);
    res.status(500).json({ success: false, message: 'Server Error' });
  }
});

// POST /api/settings
router.post('/settings', async (req, res) => {
  try {
    const userId = req.body.user || '9911';
    const retention_days = parseInt(req.body.retention_days);

    if (isNaN(retention_days)) {
      return res.status(400).json({ success: false, message: 'Invalid retention_days' });
    }

    await db.query(
      `INSERT INTO settings (user_id, retention_days) VALUES (?, ?) 
       ON DUPLICATE KEY UPDATE retention_days = ?`,
      [userId, retention_days, retention_days]
    );

    res.json({ success: true, message: 'Settings updated' });
  } catch (error) {
    console.error('Error updating settings:', error);
    res.status(500).json({ success: false, message: 'Server Error' });
  }
});

// GET /api/download/ph
// Download history ph dalam bentuk CSV
router.get('/download/ph', async (req, res) => {
  try {
    const days = parseInt(req.query.days) || 7;
    const userId = req.query.user || '9911';

    const [rows] = await db.query(
      `SELECT created_at, value FROM ph_logs 
       WHERE user_id = ? 
       AND created_at >= NOW() - INTERVAL ? DAY 
       ORDER BY created_at DESC`,
      [userId, days]
    );

    let csvContent = "Waktu,pH\n";
    rows.forEach(row => {
      csvContent += `${new Date(row.created_at).toLocaleString('id-ID')},${row.value}\n`;
    });

    res.header('Content-Type', 'text/csv');
    res.attachment(`ph_history_${userId}_${days}days.csv`);
    return res.send(csvContent);
  } catch (error) {
    console.error('Error downloading pH data:', error);
    res.status(500).send('Server Error');
  }
});

module.exports = router;
