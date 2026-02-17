const fs = require('fs');
const path = require('path');

const logsDir = path.join(__dirname, 'logs');
if (!fs.existsSync(logsDir)) fs.mkdirSync(logsDir);

function getLogFile() {
  const date = new Date().toISOString().slice(0, 10); // YYYY-MM-DD
  return path.join(logsDir, `${date}.log`);
}

function log(tag, message) {
  const time = new Date().toISOString().slice(11, 23); // HH:MM:SS.mmm
  const line = `[${time}] [${tag}] ${message}`;
  console.log(line);
  fs.appendFileSync(getLogFile(), line + '\n');
}

module.exports = { log };
