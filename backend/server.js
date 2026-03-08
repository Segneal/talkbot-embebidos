/**
 * Talkbot Backend - Express Server
 * Pipeline: Audio WAV → Amazon Transcribe (STT) → Claude Bedrock (LLM) → Amazon Polly (TTS) → Audio WAV
 */

require('dotenv').config();
const express = require('express');
const fs = require('fs');
const path = require('path');
const { transcribe } = require('./stt_service');
const { generateResponse, clearHistory } = require('./llm_service');
const { synthesize } = require('./tts_service');
const { log } = require('./logger');

const app = express();
const PORT = process.env.PORT || 8000;

// CORS para que talkbot.local pueda pedir audio
app.use((req, res, next) => {
  res.set('Access-Control-Allow-Origin', '*');
  res.set('Access-Control-Allow-Headers', 'X-API-Key, Content-Type');
  if (req.method === 'OPTIONS') return res.sendStatus(200);
  next();
});

// Log todas las requests entrantes
app.use((req, res, next) => {
  log('HTTP', `${req.method} ${req.url} from ${req.ip} | content-type: ${req.headers['content-type'] || 'none'} | content-length: ${req.headers['content-length'] || '0'}`);
  const start = Date.now();
  res.on('finish', () => {
    log('HTTP', `${req.method} ${req.url} → ${res.statusCode} (${Date.now() - start}ms)`);
  });
  next();
});

// API Key auth
const API_KEY = process.env.API_KEY;
app.use('/api', (req, res, next) => {
  if (!API_KEY) return next();
  // Endpoints públicos (para el speaker web)
  const publicPaths = ['/response-count', '/last-audio'];
  if (publicPaths.includes(req.path)) return next();
  const key = req.headers['x-api-key'];
  if (key !== API_KEY) {
    log('Auth', `Rechazado desde ${req.ip}`);
    return res.status(401).json({ error: 'Unauthorized' });
  }
  next();
});

// Recibir body raw (audio)
app.use('/api/chat', express.raw({ type: 'audio/wav', limit: '1mb' }));
app.use(express.json());

app.get('/api/health', (req, res) => {
  res.json({ status: 'ok' });
});

app.post('/api/chat', async (req, res) => {
  const audioBuffer = req.body;

  if (!audioBuffer || audioBuffer.length < 44) {
    return res.status(400).json({ error: 'Audio demasiado corto' });
  }

  // Leer config de agente desde headers del ESP32
  const voiceId = req.headers['x-voice'] || 'Lupe';
  const agentName = req.headers['x-agent'] || 'lupe';

  log('Chat', `--- Nueva petición: ${audioBuffer.length} bytes | agente=${agentName} voz=${voiceId} ---`);

  try {
    const t0 = Date.now();

    // 1. Speech-to-Text (Amazon Transcribe)
    const userText = await transcribe(audioBuffer);
    const t1 = Date.now();
    log('STT', `(${t1 - t0}ms) "${userText}"`);

    if (!userText || !userText.trim()) {
      log('STT', 'No se detectó habla');
      return res.status(400).json({ error: 'No se detectó habla en el audio' });
    }

    // 2. LLM (Claude via Bedrock)
    const responseText = await generateResponse(userText, agentName);
    const t2 = Date.now();
    log('LLM', `(${t2 - t1}ms) "${responseText}"`);

    // 3. Text-to-Speech (Amazon Polly)
    const responseAudio = await synthesize(responseText, voiceId);
    const t3 = Date.now();
    log('TTS', `(${t3 - t2}ms) ${responseAudio.length} bytes`);
    log('Chat', `Total: ${t3 - t0}ms | STT:${t1-t0} LLM:${t2-t1} TTS:${t3-t2}`);

    // Guardar WAV y notificar al speaker web
    const debugFile = path.join(__dirname, 'last_tts.wav');
    fs.writeFileSync(debugFile, responseAudio);
    lastUserText = userText;
    lastBotText = responseText;
    responseCount++;
    log('Debug', `TTS guardado (response #${responseCount})`);

    // Devolver audio WAV al ESP32
    res.set('Content-Type', 'audio/wav');
    res.set('X-User-Text', encodeURIComponent(userText));
    res.set('X-Bot-Text', encodeURIComponent(responseText));
    res.send(responseAudio);

  } catch (err) {
    log('Error', err.message);
    res.status(500).json({ error: err.message });
  }
});

// Contador de respuestas y textos para la UI speaker
let responseCount = 0;
let lastUserText = '';
let lastBotText = '';

// Página speaker con mismo estilo que talkbot.local
app.get('/speaker', (req, res) => {
  res.set('Content-Type', 'text/html');
  res.send(`<!DOCTYPE html>
<html lang="es"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Talkbot Speaker</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  background: #1a1a2e; color: #eee; min-height: 100vh; padding: 16px;
}
.container { max-width: 420px; margin: 0 auto; }
h1 { text-align: center; font-size: 1.8em; margin-bottom: 16px; color: #e94560; }
h2 { font-size: 1em; margin-bottom: 12px; color: #aaa; text-transform: uppercase; letter-spacing: 1px; }
.card { background: #16213e; border-radius: 12px; padding: 16px; margin-bottom: 12px; }
.status-indicator { display: flex; align-items: center; gap: 10px; font-size: 1.2em; }
.dot { width: 14px; height: 14px; border-radius: 50%; background: #555; flex-shrink: 0; }
.dot.waiting { background: #555; }
.dot.playing { background: #e94560; animation: pulse 0.8s infinite; }
@keyframes pulse { 0%,100% { opacity:1; } 50% { opacity:0.4; } }
.chat-log { max-height: 300px; overflow-y: auto; font-size: 0.95em; }
.chat-msg { padding: 8px 0; border-bottom: 1px solid #0f3460; }
.chat-msg:last-child { border-bottom: none; }
.chat-label { font-size: 0.75em; color: #888; text-transform: uppercase; margin-bottom: 2px; }
.chat-text { line-height: 1.4; }
.chat-user .chat-text { color: #4caf50; }
.chat-bot .chat-text { color: #eee; }
.hint { color: #666; font-size: 0.85em; text-align: center; margin-top: 8px; }
</style>
</head><body>
<div class="container">
  <h1>Talkbot</h1>
  <div class="card">
    <h2>Estado</h2>
    <div class="status-indicator">
      <span class="dot waiting" id="dot"></span>
      <span id="status">Esperando respuesta...</span>
    </div>
  </div>
  <div class="card">
    <h2>Conversaci&oacute;n</h2>
    <div class="chat-log" id="chat-log"></div>
  </div>
  <p class="hint">Habl&aacute; al ESP32 y la respuesta se reproduce ac&aacute;</p>
</div>
<script>
let lastCount = 0;
let firstLoad = true;

function addMsg(role, text) {
  const log = document.getElementById('chat-log');
  const msg = document.createElement('div');
  msg.className = 'chat-msg ' + (role === 'user' ? 'chat-user' : 'chat-bot');
  const label = document.createElement('div');
  label.className = 'chat-label';
  label.textContent = role === 'user' ? 'Tu' : 'Talkbot';
  const content = document.createElement('div');
  content.className = 'chat-text';
  content.textContent = text;
  msg.appendChild(label);
  msg.appendChild(content);
  log.appendChild(msg);
  log.scrollTop = log.scrollHeight;
}

async function poll() {
  try {
    const r = await fetch('/api/response-count');
    const data = await r.json();
    if (data.count > lastCount) {
      if (firstLoad) { lastCount = data.count; firstLoad = false; return; }
      lastCount = data.count;

      if (data.lastUser) addMsg('user', data.lastUser);
      if (data.lastBot) addMsg('bot', data.lastBot);

      document.getElementById('dot').className = 'dot playing';
      document.getElementById('status').textContent = 'Reproduciendo...';
      const audio = new Audio('/api/last-audio?' + Date.now());
      audio.onended = function() {
        document.getElementById('dot').className = 'dot waiting';
        document.getElementById('status').textContent = 'Esperando respuesta...';
      };
      audio.onerror = function() {
        document.getElementById('dot').className = 'dot waiting';
        document.getElementById('status').textContent = 'Esperando respuesta...';
      };
      audio.play().catch(function(){});
    }
  } catch(e) {}
  setTimeout(poll, 500);
}
poll();
</script>
</body></html>`);
});

// Servir último audio TTS
app.get('/api/last-audio', (req, res) => {
  const file = path.join(__dirname, 'last_tts.wav');
  if (fs.existsSync(file)) {
    res.set('Content-Type', 'audio/wav');
    res.send(fs.readFileSync(file));
  } else {
    res.status(404).json({ error: 'No hay audio todavía' });
  }
});

// Contador de respuestas
app.get('/api/response-count', (req, res) => {
  res.json({ count: responseCount, lastUser: lastUserText, lastBot: lastBotText });
});

app.post('/api/clear', (req, res) => {
  clearHistory();
  log('Chat', 'Historial limpiado');
  res.json({ status: 'cleared' });
});

app.listen(PORT, '0.0.0.0', () => {
  log('Server', `Talkbot Backend corriendo en http://0.0.0.0:${PORT}`);
});
