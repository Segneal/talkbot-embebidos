/**
 * Amazon Transcribe Streaming - Speech to Text
 * Usa la API de streaming (mucho más rápido que batch)
 */

const { TranscribeStreamingClient, StartStreamTranscriptionCommand } = require('@aws-sdk/client-transcribe-streaming');
const { log } = require('./logger');

const region = process.env.AWS_REGION || 'us-east-1';
const client = new TranscribeStreamingClient({ region });

async function transcribe(audioBuffer) {
  // Saltar WAV header (44 bytes) para obtener PCM crudo
  const pcmData = audioBuffer.slice(44);

  // Crear async iterator de chunks de audio
  const audioStream = async function* () {
    const chunkSize = 4096;
    for (let i = 0; i < pcmData.length; i += chunkSize) {
      yield { AudioEvent: { AudioChunk: pcmData.slice(i, i + chunkSize) } };
    }
  };

  const command = new StartStreamTranscriptionCommand({
    LanguageCode: 'es-ES',
    MediaEncoding: 'pcm',
    MediaSampleRateHertz: 16000,
    AudioStream: audioStream(),
  });

  const response = await client.send(command);

  let transcript = '';
  for await (const event of response.TranscriptResultStream) {
    if (event.TranscriptEvent) {
      const results = event.TranscriptEvent.Transcript.Results;
      for (const result of results) {
        if (!result.IsPartial) {
          transcript += result.Alternatives[0].Transcript + ' ';
        }
      }
    }
  }

  return transcript.trim();
}

module.exports = { transcribe };
