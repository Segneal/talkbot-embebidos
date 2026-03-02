#include "audio_recorder.h"
#include <driver/dac.h>

#define I2S_ADC_UNIT    ADC_UNIT_1
#define I2S_ADC_CHANNEL ADC1_CHANNEL_6  // GPIO 34
#define WAV_HEADER_SIZE 44

bool AudioRecorder::_initI2sAdc() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  i2s_config.sample_rate = 44100;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 8;
  i2s_config.dma_buf_len = 1024;
  i2s_config.use_apll = false;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Error instalando I2S driver: %d\n", err);
    return false;
  }

  err = i2s_set_adc_mode(I2S_ADC_UNIT, I2S_ADC_CHANNEL);
  if (err != ESP_OK) {
    Serial.printf("Error configurando ADC mode: %d\n", err);
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }

  // Deshabilitar DAC para que no reproduzca la entrada del mic por GPIO25
  i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);

  i2s_adc_enable(I2S_NUM_0);
  return true;
}

void AudioRecorder::_deinitI2s() {
  i2s_adc_disable(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
}

bool AudioRecorder::begin() {
  Serial.printf("Audio recorder listo (max %d bytes)\n", AUDIO_BUFFER_SIZE);
  Serial.printf("Heap libre: %d bytes\n", ESP.getFreeHeap());
  return true;
}

void AudioRecorder::startRecording() {
  if (_recording) return;

  _pcmSamples = 0;
  _wavSize = 0;

  // Liberar buffer anterior si existe
  if (_wavBuffer) {
    free(_wavBuffer);
    _wavBuffer = nullptr;
  }

  // Un solo buffer: 44 bytes header + PCM data
  size_t totalSize = WAV_HEADER_SIZE + AUDIO_BUFFER_SIZE;
  _wavBuffer = (uint8_t*)malloc(totalSize);
  if (!_wavBuffer) {
    Serial.printf("Error: No se pudo asignar buffer (%d bytes, heap: %d)\n",
                   totalSize, ESP.getFreeHeap());
    return;
  }

  // PCM samples van después del header
  int16_t* pcmDest = (int16_t*)(_wavBuffer + WAV_HEADER_SIZE);
  size_t maxSamples = AUDIO_BUFFER_SIZE / sizeof(int16_t);

  if (!_initI2sAdc()) {
    Serial.println("Error iniciando I2S ADC");
    free(_wavBuffer);
    _wavBuffer = nullptr;
    return;
  }

  _recording = true;
  _lastPeakLevel = 0.0f;
  Serial.println("Grabación iniciada");

  int16_t readBuf[256];
  int sampleCounter = 0;
  int16_t chunkPeak = 0;

  while (_recording && _pcmSamples < maxSamples) {
    size_t bytesRead = 0;
    chunkPeak = 0;

    esp_err_t err = i2s_read(I2S_NUM_0, readBuf, sizeof(readBuf), &bytesRead, 100 / portTICK_PERIOD_MS);
    if (err == ESP_OK && bytesRead > 0) {
      size_t samplesRead = bytesRead / sizeof(int16_t);
      for (size_t i = 0; i < samplesRead && _pcmSamples < maxSamples; i++) {
        sampleCounter++;
        if (sampleCounter >= 3) {
          sampleCounter = 0;
          int16_t sample = readBuf[i];
          sample = (sample & 0x0FFF) - 2048;
          sample <<= 4;
          pcmDest[_pcmSamples++] = sample;
          // Track peak level for VU meter
          int16_t absSample = abs(sample);
          if (absSample > chunkPeak) chunkPeak = absSample;
        }
      }
      // Update peak level (normalize to 0.0-1.0, 32767 = max int16)
      _lastPeakLevel = (float)chunkPeak / 32767.0f;
    }
    vTaskDelay(1);
  }

  _deinitI2s();

  // Escribir WAV header al inicio del buffer
  size_t dataSize = _pcmSamples * sizeof(int16_t);
  _wavSize = WAV_HEADER_SIZE + dataSize;
  _buildWavHeader(_wavBuffer, dataSize);

  Serial.printf("Grabación: %d muestras (%.1fs), WAV: %d bytes, heap: %d\n",
                _pcmSamples, (float)_pcmSamples / SAMPLE_RATE, _wavSize, ESP.getFreeHeap());
}

void AudioRecorder::stopRecording() {
  _recording = false;
}

void AudioRecorder::freeBuffer() {
  if (_wavBuffer) {
    free(_wavBuffer);
    _wavBuffer = nullptr;
    _wavSize = 0;
  }
}

void AudioRecorder::_buildWavHeader(uint8_t* header, size_t dataSize) {
  uint32_t fileSize = 36 + dataSize;

  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  header[4] = fileSize & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1;  header[21] = 0;
  header[22] = NUM_CHANNELS; header[23] = 0;

  uint32_t sr = SAMPLE_RATE;
  header[24] = sr & 0xFF;
  header[25] = (sr >> 8) & 0xFF;
  header[26] = (sr >> 16) & 0xFF;
  header[27] = (sr >> 24) & 0xFF;

  uint32_t byteRate = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
  header[28] = byteRate & 0xFF;
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF;
  header[31] = (byteRate >> 24) & 0xFF;

  uint16_t blockAlign = NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
  header[32] = blockAlign & 0xFF;
  header[33] = (blockAlign >> 8) & 0xFF;
  header[34] = BITS_PER_SAMPLE; header[35] = 0;

  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}
