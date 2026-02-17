#include "audio_player.h"

bool AudioPlayer::_initI2s(uint32_t sampleRate) {
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_config.sample_rate = sampleRate;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = I2S_DMA_BUF_COUNT;
  i2s_config.dma_buf_len = I2S_DMA_BUF_LEN;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = true;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = I2S_BCLK_PIN;
  pin_config.ws_io_num = I2S_LRC_PIN;
  pin_config.data_out_num = I2S_DOUT_PIN;
  pin_config.data_in_num = I2S_PIN_NO_CHANGE;

  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Error instalando I2S TX driver: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_NUM_1, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Error configurando pines I2S TX: %d\n", err);
    i2s_driver_uninstall(I2S_NUM_1);
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_1);
  _initialized = true;
  return true;
}

void AudioPlayer::_deinitI2s() {
  if (_initialized) {
    i2s_zero_dma_buffer(I2S_NUM_1);
    i2s_driver_uninstall(I2S_NUM_1);
    _initialized = false;
  }
}

bool AudioPlayer::begin() {
  // No inicializar I2S aquí, se hace en play() con el sample rate correcto
  return true;
}

void AudioPlayer::end() {
  _deinitI2s();
}

uint32_t AudioPlayer::_parseSampleRate(uint8_t* wavData) {
  // Sample rate está en bytes 24-27 del header WAV
  return wavData[24] | (wavData[25] << 8) | (wavData[26] << 16) | (wavData[27] << 24);
}

bool AudioPlayer::play(uint8_t* wavData, size_t wavSize) {
  if (!wavData || wavSize <= 44) return false;

  // Verificar header WAV
  if (wavData[0] != 'R' || wavData[1] != 'I' || wavData[2] != 'F' || wavData[3] != 'F') {
    Serial.println("Error: No es un archivo WAV válido");
    return false;
  }

  uint32_t sampleRate = _parseSampleRate(wavData);
  Serial.printf("Reproduciendo WAV: %d Hz, %d bytes\n", sampleRate, wavSize);

  // Inicializar I2S con el sample rate del WAV
  if (!_initI2s(sampleRate)) return false;

  _playing = true;

  // Datos PCM empiezan después del header (byte 44)
  uint8_t* pcmData = wavData + 44;
  size_t pcmSize = wavSize - 44;

  // Aplicar volumen a las muestras
  int16_t* samples = (int16_t*)pcmData;
  size_t numSamples = pcmSize / sizeof(int16_t);

  for (size_t i = 0; i < numSamples; i++) {
    samples[i] = (int32_t)samples[i] * _volume / 100;
  }

  // Escribir al I2S en bloques
  size_t bytesWritten = 0;
  size_t offset = 0;
  size_t chunkSize = I2S_DMA_BUF_LEN * 2;  // bytes por chunk

  while (offset < pcmSize) {
    size_t toWrite = min(chunkSize, pcmSize - offset);
    esp_err_t err = i2s_write(I2S_NUM_1, pcmData + offset, toWrite, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK) {
      Serial.printf("Error escribiendo I2S: %d\n", err);
      break;
    }
    offset += bytesWritten;
    yield();
  }

  // Esperar a que termine de reproducir
  delay(100);
  _deinitI2s();
  _playing = false;

  Serial.println("Reproducción terminada");
  return true;
}

void AudioPlayer::setVolume(uint8_t vol) {
  _volume = constrain(vol, 0, 100);
}

bool AudioPlayer::initI2sStream(uint32_t sampleRate) {
  _playing = true;
  return _initI2s(sampleRate);
}

void AudioPlayer::deinitI2sStream() {
  delay(100);
  _deinitI2s();
  _playing = false;
}
