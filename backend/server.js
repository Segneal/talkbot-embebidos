/**
 * Talkbot Backend - Express Server
 * Pipeline: Audio WAV → Amazon Transcribe (STT) → Claude Bedrock (LLM) → Amazon Polly (TTS) → Audio WAV
 */

require('dotenv').config();
const express = require('express');
const { transcribe } = require('./stt_service');
const { generateResponse, clearHistory } = require('./llm_service');
const { synthesize } = require('./tts_service');
const { log } = require('./logger');

const app = express();
const PORT = process.env.PORT || 8000;

// Recibir body raw (audio)
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
  if (!API_KEY) return next(); // sin key configurada, acceso libre (dev local)
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

    // Devolver audio WAV al ESP32
    res.set('Content-Type', 'audio/wav');
    res.send(responseAudio);

  } catch (err) {
    log('Error', err.message);
    res.status(500).json({ error: err.message });
  }
});

app.post('/api/clear', (req, res) => {
  clearHistory();
  log('Chat', 'Historial limpiado');
  res.json({ status: 'cleared' });
});

app.listen(PORT, '0.0.0.0', () => {
  log('Server', `Talkbot Backend corriendo en http://0.0.0.0:${PORT}`);
});
