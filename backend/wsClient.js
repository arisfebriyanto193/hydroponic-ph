const WebSocket = require('ws');
const db = require('./db');
const cron = require('node-cron');
const { checkAndNotify } = require('./service/email_notif');


const WS_URL = 'wss://server-iot-qbyte.qbyte.web.id/ws';

let socket = null;
let reconnectInterval = 5000;

// Variabel untuk menyimpan data terbaru yang masuk
const USER_ID = '9911';
let latestPhData = null;
let latestTdsData = null;

// Maksimum jumlah data yang disimpan per user (circular buffer)



async function getMaxRecords(userId) {
  //ambil max records dari database
  try {
    const [rows] = await db.query(`SELECT max_records FROM settings WHERE user_id = ?`, [userId]);
    return rows.length > 0 ? rows[0].max_records || 250 : 250;
  } catch (err) {
    console.error('Error getting max records:', err);
    return 250;
  }
}

// Cron job berjalan setiap kelipatan 5 menit (contoh: 00:00, 00:05, 00:10, dst)
cron.schedule('*/5 * * * *', async () => {
  if (latestPhData !== null || latestTdsData !== null) {
    let phValue = 0;
    if (latestPhData !== null) {
      if (typeof latestPhData === 'object' && latestPhData !== null) {
        phValue = latestPhData.sensor1 || latestPhData.pH || 0;
      } else {
        phValue = parseFloat(latestPhData);
      }
    }

    let tdsValue = 0;
    if (latestTdsData !== null) {
      if (typeof latestTdsData === 'object' && latestTdsData !== null) {
        tdsValue = latestTdsData.sensor1 || latestTdsData.tds || 0;
      } else {
        tdsValue = parseFloat(latestTdsData);
      }
    }

    try {
      const MAX_RECORDS = await getMaxRecords(USER_ID);
      // Kita menghitung total records berdasarkan waktu atau id, tapi karena kita punya dua baris (ph, tds) per entry,
      // lebih baik cek berdasarkan COUNT(DISTINCT created_at) atau asumsikan 1 set data = 2 rows
      const [[{ total }]] = await db.query(
        `SELECT COUNT(*) AS total FROM ph_logs WHERE user_id = ? AND sensor_type = 'ph'`,
        [USER_ID]
      );

      // Jika sudah mencapai batas, hapus data terlama
      if (total >= MAX_RECORDS) {
        // Hapus berdasar created_at terlama agar sinkron
        const [[oldest]] = await db.query(
          `SELECT created_at FROM ph_logs WHERE user_id = ? ORDER BY created_at ASC LIMIT 1`,
          [USER_ID]
        );
        if (oldest) {
          await db.query(
            `DELETE FROM ph_logs WHERE user_id = ? AND created_at = ?`,
            [USER_ID, oldest.created_at]
          );
        }
      }

      const now = new Date(); // Pastikan timestamp sama untuk pH dan TDS

      if (!isNaN(phValue) && latestPhData !== null) {
        await db.query(
          `INSERT INTO ph_logs (user_id, sensor_type, value, created_at) VALUES (?, 'ph', ?, ?)`,
          [USER_ID, phValue, now]
        );
        // ── Cek threshold & kirim email notifikasi jika mode=manual ──
        await checkAndNotify(USER_ID, phValue);
      }

      if (!isNaN(tdsValue) && latestTdsData !== null) {
        await db.query(
          `INSERT INTO ph_logs (user_id, sensor_type, value, created_at) VALUES (?, 'tds', ?, ?)`,
          [USER_ID, tdsValue, now]
        );
      }

      console.log(`[DB Cron] Saved data at ${now.toLocaleTimeString('id-ID')}: pH=${phValue}, TDS=${tdsValue}`);

    } catch (error) {
      console.error('[DB Cron] Error saving data:', error);
    }

    // Reset
    latestPhData = null;
    latestTdsData = null;
  }
});

function connectWS() {
  console.log(`[WebSocket] Connecting to ${WS_URL}...`);
  socket = new WebSocket(WS_URL);

  socket.on('open', () => {
    console.log('[WebSocket] Connected');

    // Subscribe to topics
    const topicsToSubscribe = [
      `data/ph/user/${USER_ID}`,
      `data/tds/user/${USER_ID}`,
      `data/mode/user/${USER_ID}`,
      `data/treshold/user/${USER_ID}`,
      `data/relay1/user/${USER_ID}`,
      `data/relay2/user/${USER_ID}`
    ];

    topicsToSubscribe.forEach(topic => {
      socket.send(JSON.stringify({
        action: 'subscribe',
        topic: topic
      }));
    });
    console.log('[WebSocket] Subscribed to topics');
  });

  socket.on('message', async (data) => {
    try {
      const messages = data.toString().split('\n');
      for (const messageStr of messages) {
        if (!messageStr.trim()) continue;

        try {
          const msg = JSON.parse(messageStr);
          if (msg.action === 'publish' || msg.topic) {
            await handleMessage(msg.topic, msg.payload);
          }
        } catch (innerErr) {
          console.error('[WebSocket] Error parsing single message:', innerErr, messageStr);
        }
      }
    } catch (e) {
      console.error('[WebSocket] Error processing messages:', e, data.toString());
    }
  });

  socket.on('close', () => {
    console.log('[WebSocket] Disconnected. Reconnecting in 5s...');
    setTimeout(connectWS, reconnectInterval);
  });

  socket.on('error', (err) => {
    console.error('[WebSocket] Error:', err.message);
    socket.close(); // trigger onclose to reconnect
  });
}

async function handleMessage(topic, payload) {
  try {
    const topicParts = topic.split('/');

    // Check if topic is related to user 9911
    if (topic === `data/ph/user/${USER_ID}`) {
      latestPhData = payload;
    }
    else if (topic === `data/tds/user/${USER_ID}`) {
      latestTdsData = payload;
    }
    else if (topic === `data/mode/user/${USER_ID}`) {
      // payload ex: "otomatis" / "manual"
      const modeStr = (typeof payload === 'string') ? payload : String(payload?.mode || 'manual');
      await db.query(`UPDATE device_states SET mode = ? WHERE user_id = ?`, [modeStr, USER_ID]);
      console.log(`[DB] Updated mode to: ${modeStr}`);
    }
    else if (topic === `data/treshold/user/${USER_ID}`) {
      if (payload !== undefined && payload !== null && payload !== 'undefined') {
        const threshStr = (typeof payload === 'object') ? String(payload.threshold || Object.values(payload)[0] || '') : String(payload);
        if (threshStr && threshStr.trim() !== '') {
          await db.query(`UPDATE device_states SET threshold = ? WHERE user_id = ?`, [threshStr, USER_ID]);
          console.log(`[DB] Updated threshold to ${threshStr}`);
        }
      }
    }
    else if (topic === `data/relay1/user/${USER_ID}`) {
      const p = typeof payload === 'object' ? Object.values(payload)[0] : payload;
      const state = (p === true || p === 'true' || p === '1' || p === 1);
      await db.query(`UPDATE device_states SET relay1_status = ? WHERE user_id = ?`, [state, USER_ID]);
      console.log(`[DB] Updated relay1 status to ${state}`);
    }
    else if (topic === `data/relay2/user/${USER_ID}`) {
      const p = typeof payload === 'object' ? Object.values(payload)[0] : payload;
      const state = (p === true || p === 'true' || p === '1' || p === 1);
      await db.query(`UPDATE device_states SET relay2_status = ? WHERE user_id = ?`, [state, USER_ID]);
      console.log(`[DB] Updated relay2 status to ${state}`);
    }


  } catch (error) {
    console.error('[WebSocket DB Error]', error);
  }
}

connectWS();

module.exports = { socket };
