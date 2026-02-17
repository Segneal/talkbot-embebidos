#include "api_client.h"
#include <WiFi.h>

void ApiClient::begin(const char* backendUrl) {
  _baseUrl = String(backendUrl);
  Serial.printf("API Client configurado: %s\n", backendUrl);
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
    Serial.println("Backend health: OK");
    return true;
  }

  Serial.printf("Backend health check falló: %d\n", httpCode);
  return false;
}

bool ApiClient::sendAudioAndPlay(uint8_t* wavData, size_t wavSize, AudioPlayer& player,
                                 const String& voiceId, const String& agentName) {
  if (!wavData || wavSize == 0) {
    _lastError = "No hay audio para enviar";
    return false;
  }

  HTTPClient http;
  String url = _baseUrl + CHAT_ENDPOINT;

  Serial.printf("Enviando %d bytes a %s\n", wavSize, url.c_str());

  http.begin(url);
  http.setTimeout(CHAT_TIMEOUT);
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Voice", voiceId);
  http.addHeader("X-Agent", agentName);
  if (strlen(API_KEY) > 0) http.addHeader("X-API-Key", API_KEY);

  int httpCode = http.POST(wavData, wavSize);

  if (httpCode != 200) {
    _lastError = "HTTP error: " + String(httpCode);
    if (httpCode > 0) {
      _lastError += " - " + http.getString();
    }
    Serial.println(_lastError);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  Serial.printf("Respuesta: %d bytes (streaming)\n", contentLength);

  WiFiClient* stream = http.getStreamPtr();

  // Leer WAV header (44 bytes)
  uint8_t wavHeader[44];
  size_t headerRead = 0;
  while (headerRead < 44) {
    size_t available = stream->available();
    if (available > 0) {
      size_t toRead = min(available, (size_t)(44 - headerRead));
      size_t r = stream->readBytes(wavHeader + headerRead, toRead);
      headerRead += r;
    }
    yield();
  }

  // Parsear sample rate del header
  uint32_t sampleRate = wavHeader[24] | (wavHeader[25] << 8) | (wavHeader[26] << 16) | (wavHeader[27] << 24);
  Serial.printf("Streaming playback: %d Hz\n", sampleRate);

  // Iniciar I2S para reproducción
  if (!player.initI2sStream(sampleRate)) {
    _lastError = "Error iniciando I2S para streaming";
    http.end();
    return false;
  }

  // Streaming: leer mono de red, duplicar a stereo, escribir a I2S
  #define MONO_BUF_SIZE 1024
  uint8_t monoBuf[MONO_BUF_SIZE];
  int16_t stereoBuf[MONO_BUF_SIZE];  // 1024 samples stereo = 2048 bytes
  size_t totalRead = 0;
  size_t dataSize = (contentLength > 0) ? contentLength - 44 : 0;

  // Ganancia base x4, modulada por volumen (0-100)
  int32_t vol = player.getVolume();

  while (totalRead < dataSize || contentLength <= 0) {
    size_t available = stream->available();
    if (available > 0) {
      size_t toRead = min(available, (size_t)MONO_BUF_SIZE);
      if (contentLength > 0) {
        toRead = min(toRead, dataSize - totalRead);
      }
      size_t r = stream->readBytes(monoBuf, toRead);
      if (r > 0) {
        // Aplicar ganancia x4 * volumen y duplicar mono -> stereo (L=R)
        int16_t* monoSamples = (int16_t*)monoBuf;
        size_t numSamples = r / sizeof(int16_t);
        for (size_t i = 0; i < numSamples; i++) {
          int32_t s = (int32_t)monoSamples[i] * 4 * vol / 100;
          if (s > 32767) s = 32767;
          if (s < -32768) s = -32768;
          stereoBuf[i * 2] = (int16_t)s;      // Left
          stereoBuf[i * 2 + 1] = (int16_t)s;  // Right
        }

        size_t written;
        i2s_write(I2S_NUM_1, stereoBuf, numSamples * 4, &written, portMAX_DELAY);
        totalRead += r;
      }
    } else if (!stream->connected()) {
      break;
    }
    yield();
  }

  player.deinitI2sStream();
  http.end();

  Serial.printf("Streaming terminado: %d bytes reproducidos\n", totalRead);
  return true;
}
