#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

class AudioPlayer {
public:
  bool begin();
  void end();
  bool play(uint8_t* wavData, size_t wavSize);
  bool isPlaying() { return _playing; }

  // Streaming: init/deinit I2S para uso externo (api_client)
  bool initI2sStream(uint32_t sampleRate);
  void deinitI2sStream();

  void setVolume(uint8_t vol);  // 0-100
  uint8_t getVolume() { return _volume; }

private:
  bool _playing = false;
  bool _initialized = false;
  uint8_t _volume = 100;  // Default 100%

  bool _initI2s(uint32_t sampleRate);
  void _deinitI2s();
  uint32_t _parseSampleRate(uint8_t* wavData);
};

#endif
