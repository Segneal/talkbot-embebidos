#include "audio_player.h"
#include <driver/dac.h>

// I2S DAC mode: salida analógica GPIO25 (DAC1) via DMA
// Jack 3.5mm → Noga NG-106 (parlante amplificado USB)

#define I2S_DAC_PORT I2S_NUM_0

static inline uint16_t toDac(int16_t sample) {
  return (uint16_t)((int32_t)sample + 32768);
}

bool AudioPlayer::_initI2s(uint32_t sampleRate) {
  if (_initialized) _deinitI2s();

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = I2S_DMA_BUF_COUNT;
  cfg.dma_buf_len = I2S_DMA_BUF_LEN;
  cfg.use_apll = true;

  esp_err_t err = i2s_driver_install(I2S_DAC_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[Player] Error I2S DAC: %d\n", err);
    return false;
  }

  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  _initialized = true;
  Serial.printf("[Player] DAC init: %dHz\n", sampleRate);
  return true;
}

void AudioPlayer::_deinitI2s() {
  if (_initialized) {
    i2s_zero_dma_buffer(I2S_DAC_PORT);
    i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
    i2s_driver_uninstall(I2S_DAC_PORT);
    _initialized = false;
    // Deshabilitar DAC para evitar ruido en el jack 3.5mm
    dac_output_disable(DAC_CHANNEL_1);
    dac_output_disable(DAC_CHANNEL_2);
  }
}

bool AudioPlayer::begin() {
  // Deshabilitar DAC para evitar ruido en el jack 3.5mm
  dac_output_disable(DAC_CHANNEL_1);
  dac_output_disable(DAC_CHANNEL_2);
  Serial.println("[Player] Audio player listo (DAC → GPIO25 → Jack 3.5mm)");
  return true;
}

void AudioPlayer::end() {
  _deinitI2s();
}

uint32_t AudioPlayer::_parseSampleRate(uint8_t* wavData) {
  return wavData[24] | (wavData[25] << 8) | (wavData[26] << 16) | (wavData[27] << 24);
}

bool AudioPlayer::play(uint8_t* wavData, size_t wavSize) {
  if (!wavData || wavSize <= 44) return false;
  if (wavData[0] != 'R' || wavData[1] != 'I') {
    Serial.println("[Player] Error: No es WAV");
    return false;
  }

  uint32_t sampleRate = _parseSampleRate(wavData);
  if (!_initI2s(sampleRate)) return false;

  _playing = true;
  int16_t* samples = (int16_t*)(wavData + 44);
  size_t numSamples = (wavSize - 44) / sizeof(int16_t);

  uint16_t buf[512];
  size_t pos = 0;
  while (pos < numSamples) {
    size_t chunk = min((size_t)256, numSamples - pos);
    for (size_t i = 0; i < chunk; i++) {
      int32_t s = (int32_t)samples[pos + i] * _volume / 100;
      uint16_t d = toDac((int16_t)constrain(s, -32768, 32767));
      buf[i * 2] = d;
      buf[i * 2 + 1] = d;
    }
    size_t bw;
    i2s_write(I2S_DAC_PORT, buf, chunk * 2 * sizeof(uint16_t), &bw, portMAX_DELAY);
    pos += chunk;
  }

  _playing = false;
  _deinitI2s();
  return true;
}

void AudioPlayer::setVolume(uint8_t vol) {
  _volume = constrain(vol, 0, 100);
  Serial.printf("[Player] Volumen: %d%%\n", _volume);
}

bool AudioPlayer::initI2sStream(uint32_t sampleRate) {
  if (!_initI2s(sampleRate)) return false;
  _playing = true;
  return true;
}

void AudioPlayer::deinitI2sStream() {
  _deinitI2s();
  _playing = false;
}

i2s_port_t AudioPlayer::getI2sPort() {
  return I2S_DAC_PORT;
}

void AudioPlayer::playTestTone(uint32_t freqHz, uint32_t durationMs) {
  Serial.printf("[Player] Tono: %dHz %dms vol=%d%%\n", freqHz, durationMs, _volume);
  uint32_t sr = 16000;  // Mismo rate que TTS del backend
  if (!_initI2s(sr)) return;

  uint32_t total = sr * durationMs / 1000;
  uint16_t buf[512];
  size_t pos = 0;

  while (pos < total) {
    size_t chunk = min((size_t)256, (size_t)(total - pos));
    for (size_t i = 0; i < chunk; i++) {
      float t = (float)(pos + i) / sr;
      int16_t raw = (int16_t)(sinf(2.0f * 3.14159f * freqHz * t) * 32000);
      int32_t s = (int32_t)raw * _volume / 100;
      uint16_t d = toDac((int16_t)constrain(s, -32768, 32767));
      buf[i * 2] = d;
      buf[i * 2 + 1] = d;
    }
    size_t bw;
    i2s_write(I2S_DAC_PORT, buf, chunk * 2 * sizeof(uint16_t), &bw, portMAX_DELAY);
    pos += chunk;
  }

  _deinitI2s();
  Serial.println("[Player] Tono OK");
}
