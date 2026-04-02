const express = require('express');
const cors = require('cors');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());

// API Routes
app.use('/api', require('./routes/api'));

app.listen(PORT, () => {
  console.log(`[Server] API running on port ${PORT}`);
});

// Import the WebSocket client to run it alongside Express server
require('./wsClient');

// Import the Cron/Cleanup job
require('./cleanup');
