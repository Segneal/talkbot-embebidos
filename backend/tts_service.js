/**
 * Amazon Polly - Text to Speech
 */

const { PollyClient, SynthesizeSpeechCommand } = require('@aws-sdk/client-polly');

const region = process.env.AWS_REGION || 'us-east-1';
const defaultVoiceId = process.env.POLLY_VOICE_ID || 'Lupe';

const client = new PollyClient({ region });

const VOICE_LANGUAGE = {
  Mia: 'es-MX',
};

async function synthesize(text, voiceId) {
  const voice = voiceId || defaultVoiceId;
  const languageCode = VOICE_LANGUAGE[voice] || 'es-US';
  console.log(`[TTS] Sintetizando (voz=${voice}, lang=${languageCode}): ${text.slice(0, 50)}...`);

  const response = await client.send(new SynthesizeSpeechCommand({
    Text: text,
    OutputFormat: 'pcm',
    SampleRate: '16000',
    VoiceId: voice,
    Engine: 'neural',
    LanguageCode: languageCode,
  }));

  // Leer stream a buffer
  const chunks = [];
  for await (const chunk of response.AudioStream) {
    chunks.push(chunk);
  }
  const pcmBuffer = Buffer.concat(chunks);

  // Normalizar audio (maximizar volumen sin distorsión)
  normalizePcm(pcmBuffer);

  // Construir WAV header + PCM data
  const wavBuffer = buildWav(pcmBuffer, 16000);

  console.log(`[TTS] Audio generado: ${wavBuffer.length} bytes`);
  return wavBuffer;
}

function normalizePcm(pcmBuffer) {
  // Encontrar el pico máximo
  let peak = 0;
  for (let i = 0; i < pcmBuffer.length - 1; i += 2) {
    const sample = Math.abs(pcmBuffer.readInt16LE(i));
    if (sample > peak) peak = sample;
  }

  if (peak === 0) return;

  // Escalar para que el pico llegue a ~95% del rango (30000 de 32767)
  const gain = 30000 / peak;
  if (gain <= 1.0) return; // Ya está fuerte

  for (let i = 0; i < pcmBuffer.length - 1; i += 2) {
    let sample = Math.round(pcmBuffer.readInt16LE(i) * gain);
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    pcmBuffer.writeInt16LE(sample, i);
  }

  console.log(`[TTS] Audio normalizado: pico ${peak} -> gain x${gain.toFixed(1)}`);
}

function buildWav(pcmData, sampleRate) {
  const header = Buffer.alloc(44);
  const dataSize = pcmData.length;
  const fileSize = 36 + dataSize;

  header.write('RIFF', 0);
  header.writeUInt32LE(fileSize, 4);
  header.write('WAVE', 8);
  header.write('fmt ', 12);
  header.writeUInt32LE(16, 16);       // fmt chunk size
  header.writeUInt16LE(1, 20);        // PCM format
  header.writeUInt16LE(1, 22);        // mono
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(sampleRate * 2, 28); // byte rate
  header.writeUInt16LE(2, 32);        // block align
  header.writeUInt16LE(16, 34);       // bits per sample
  header.write('data', 36);
  header.writeUInt32LE(dataSize, 40);

  return Buffer.concat([header, pcmData]);
}

module.exports = { synthesize };
