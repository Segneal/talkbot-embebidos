#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

class AudioRecorder {
public:
  bool begin();
  void startRecording();
  void stopRecording();
  bool isRecording() { return _recording; }

  // Acceso al buffer grabado
  uint8_t* getBuffer()    { return _wavBuffer; }
  size_t   getBufferSize(){ return _wavSize; }

  void freeBuffer();

private:
  volatile bool _recording = false;
  uint8_t* _wavBuffer = nullptr;
  size_t _pcmSamples = 0;
  size_t _wavSize = 0;

  void _buildWavHeader(uint8_t* header, size_t dataSize);
  bool _initI2sAdc();
  void _deinitI2s();
};

#endif
