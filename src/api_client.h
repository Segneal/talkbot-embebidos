#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include "config.h"
#include "audio_player.h"

class ApiClient {
public:
  void begin(const char* backendUrl);

  // Envía audio WAV al backend y reproduce la respuesta en streaming
  bool sendAudioAndPlay(uint8_t* wavData, size_t wavSize, AudioPlayer& player,
                        const String& voiceId = "Lupe", const String& agentName = "lupe");

  // Health check
  bool checkHealth();

  String getLastError() { return _lastError; }

private:
  String _baseUrl;
  String _lastError;
};

#endif
