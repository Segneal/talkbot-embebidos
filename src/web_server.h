#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "config.h"
#include "led_controller.h"
#include "audio_player.h"
#include "api_client.h"

class TalkbotWebServer {
public:
  void begin(LedController* leds, AudioPlayer* player, TalkbotState* statePtr, ApiClient* api = nullptr);
  void handleClient();

  // Config de agente (leída desde main.cpp)
  String getAgentName() { return _agentConfig.agentName; }
  String getVoiceId()   { return _agentConfig.voiceId; }
  String getBackendUrl() { return _backendUrl; }

private:
  WebServer _server{80};
  LedController* _leds = nullptr;
  AudioPlayer* _player = nullptr;
  ApiClient* _api = nullptr;
  TalkbotState* _statePtr = nullptr;
  unsigned long _startTime = 0;
  AgentConfig _agentConfig;
  Preferences _prefs;
  String _backendUrl;

  void _handleRoot();
  void _handleFile(const String& path, const String& contentType);
  void _handleStatus();
  void _handleSetVolume();
  void _handleSetLeds();
  void _handleGetConfig();
  void _handleSetConfig();
  void _handleReboot();
  void _handleNotFound();

  String _stateToString(TalkbotState state);
  uint8_t _pinFromColorName(const String& color);
};

#endif
