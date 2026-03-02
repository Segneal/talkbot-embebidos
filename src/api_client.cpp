#include "api_client.h"
#include <WiFi.h>
#include <driver/i2s.h>

static inline uint16_t toDac(int16_t sample) {
  return (uint16_t)((int32_t)sample + 32768);
}

void ApiClient::begin(const char* backendUrl) {
  _baseUrl = String(backendUrl);
  Serial.printf("[API] Client: %s\n", backendUrl);
}

bool ApiClient::checkHealth() {
  HTTPClient http;
  String url = _baseUrl + "/api/health";
  http.begin(url);
  http.setTimeout(5000);
  if (strlen(API_KEY) > 0) http.addHeader("X-API-Key", API_KEY);
  int httpCode = http.GET();
  http.end();
  if (httpCode == 200) {
    Serial.println("[API] Backend OK");
    return true;
  }
  Serial.printf("[API] Health fail: %d\n", httpCode);
  return false;
}

bool ApiClient::sendAudioAndPlay(uint8_t* wavData, size_t wavSize, AudioPlayer& player,
                                 const String& voiceId, const String& agentName,
                                 PostSentCallback onSent) {
  if (!wavData || wavSize == 0) {
    _lastError = "No hay audio para enviar";
    return false;
  }

  HTTPClient http;
  String url = _baseUrl + CHAT_ENDPOINT;
  Serial.printf("[API] Enviando %d bytes\n", wavSize);

  http.begin(url);
  http.useHTTP10(true);
  http.setTimeout(CHAT_TIMEOUT);
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Voice", voiceId);
  http.addHeader("X-Agent", agentName);
  if (strlen(API_KEY) > 0) http.addHeader("X-API-Key", API_KEY);

  int httpCode = http.POST(wavData, wavSize);
  if (onSent) onSent();

  if (httpCode != 200) {
    _lastError = "HTTP error: " + String(httpCode);
    if (httpCode > 0) _lastError += " - " + http.getString();
    Serial.printf("[API] %s\n", _lastError.c_str());
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  Serial.printf("[API] Respuesta: %d bytes\n", contentLength);
  WiFiClient* stream = http.getStreamPtr();

  // Leer WAV header
  uint8_t wavHeader[44];
  size_t headerRead = 0;
  unsigned long headerTimeout = millis();
  while (headerRead < 44) {
    if (millis() - headerTimeout > 5000) {
      _lastError = "Timeout WAV header";
      http.end();
      return false;
    }
    size_t avail = stream->available();
    if (avail > 0) {
      size_t r = stream->readBytes(wavHeader + headerRead, min(avail, (size_t)(44 - headerRead)));
      headerRead += r;
      headerTimeout = millis();
    }
    delay(1);
  }

  // === DEBUG: Dump WAV header ===
  uint16_t audioFormat = wavHeader[20] | (wavHeader[21] << 8);
  uint16_t numChannels = wavHeader[22] | (wavHeader[23] << 8);
  uint32_t sampleRate = wavHeader[24] | (wavHeader[25] << 8) | (wavHeader[26] << 16) | (wavHeader[27] << 24);
  uint32_t byteRate = wavHeader[28] | (wavHeader[29] << 8) | (wavHeader[30] << 16) | (wavHeader[31] << 24);
  uint16_t blockAlign = wavHeader[32] | (wavHeader[33] << 8);
  uint16_t bitsPerSample = wavHeader[34] | (wavHeader[35] << 8);
  uint32_t dataChunkSize = wavHeader[40] | (wavHeader[41] << 8) | (wavHeader[42] << 16) | (wavHeader[43] << 24);

  Serial.printf("[WAV] RIFF: %c%c%c%c\n", wavHeader[0], wavHeader[1], wavHeader[2], wavHeader[3]);
  Serial.printf("[WAV] fmt:  %c%c%c%c\n", wavHeader[8], wavHeader[9], wavHeader[10], wavHeader[11]);
  Serial.printf("[WAV] audioFormat=%d channels=%d sampleRate=%d\n", audioFormat, numChannels, sampleRate);
  Serial.printf("[WAV] byteRate=%d blockAlign=%d bitsPerSample=%d\n", byteRate, blockAlign, bitsPerSample);
  Serial.printf("[WAV] dataChunkSize=%d contentLength=%d\n", dataChunkSize, contentLength);

  if (!player.initI2sStream(sampleRate)) {
    _lastError = "Error I2S DAC stream";
    http.end();
    return false;
  }

  int32_t vol = player.getVolume();
  size_t totalRead = 0;
  size_t dataSize = (contentLength > 0) ? contentLength - 44 : 0;
  Serial.printf("[API] vol=%d dataSize=%d\n", vol, dataSize);

  i2s_port_t i2sPort = player.getI2sPort();
  uint8_t netBuf[512];
  uint16_t dacBuf[512];
  unsigned long lastDataTime = millis();
  unsigned long playStart = millis();
  bool finished = false;
  int chunkCount = 0;
  uint8_t leftover = 0;
  bool hasLeftover = false;

  while (!finished) {
    if (contentLength > 0 && totalRead >= dataSize) { finished = true; break; }

    size_t avail = stream->available();
    if (avail > 0) {
      lastDataTime = millis();
      size_t toRead = min(avail, (size_t)512);
      if (contentLength > 0) toRead = min(toRead, dataSize - totalRead);

      size_t offset = 0;
      if (hasLeftover) {
        netBuf[0] = leftover;
        hasLeftover = false;
        offset = 1;
        if (toRead > 511) toRead = 511;
      }

      size_t r = stream->readBytes(netBuf + offset, toRead);
      size_t total = r + offset;
      totalRead += r;

      if (total & 1) {
        leftover = netBuf[total - 1];
        hasLeftover = true;
        total--;
      }

      if (total >= 2) {
        int16_t* samples = (int16_t*)netBuf;
        size_t numSamples = total / sizeof(int16_t);

        // Debug primeros 3 chunks
        if (chunkCount < 3) {
          Serial.printf("[CHK%d] read=%d total=%d samples=%d first5: %d %d %d %d %d\n",
            chunkCount, r, total, numSamples,
            numSamples > 0 ? samples[0] : 0,
            numSamples > 1 ? samples[1] : 0,
            numSamples > 2 ? samples[2] : 0,
            numSamples > 3 ? samples[3] : 0,
            numSamples > 4 ? samples[4] : 0);
        }

        for (size_t i = 0; i < numSamples; i++) {
          int32_t s = (int32_t)samples[i] * vol / 100;
          uint16_t d = toDac((int16_t)constrain(s, -32768, 32767));
          dacBuf[i * 2] = d;
          dacBuf[i * 2 + 1] = d;
        }
        size_t bw;
        i2s_write(i2sPort, dacBuf, numSamples * 2 * sizeof(uint16_t), &bw, portMAX_DELAY);
        chunkCount++;
      }
    } else if (!stream->connected() && !stream->available()) {
      finished = true;
    } else {
      delay(1);
      if (millis() - lastDataTime > 10000) { Serial.println("[API] Timeout"); finished = true; }
    }
  }

  uint32_t playDuration = millis() - playStart;
  float expectedDuration = (float)dataSize / (sampleRate * numChannels * bitsPerSample / 8);
  Serial.printf("[API] Chunks=%d totalRead=%d duration=%dms expected=%.0fms\n",
    chunkCount, totalRead, playDuration, expectedDuration * 1000);

  player.deinitI2sStream();
  http.end();
  return totalRead > 0;
}
