#include "web_server.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

void TalkbotWebServer::begin(DisplayController* display, AudioPlayer* player, TalkbotState* statePtr, ApiClient* api) {
  _display = display;
  _player = player;
  _statePtr = statePtr;
  _api = api;
  _startTime = millis();

  // Inicializar LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("Error montando LittleFS");
    return;
  }
  Serial.println("LittleFS montado correctamente");

  // Cargar backend URL desde NVS (o usar default)
  _prefs.begin("talkbot", false);
  _backendUrl = _prefs.getString("backendUrl", BACKEND_URL);
  Serial.printf("Backend URL: %s\n", _backendUrl.c_str());

  // Rutas
  _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
  _server.on("/style.css", HTTP_GET, [this]() { _handleFile("/style.css", "text/css"); });
  _server.on("/app.js", HTTP_GET, [this]() { _handleFile("/app.js", "application/javascript"); });
  _server.on("/api/status", HTTP_GET, [this]() { _handleStatus(); });
  _server.on("/api/volume", HTTP_POST, [this]() { _handleSetVolume(); });
  _server.on("/api/config", HTTP_GET, [this]() { _handleGetConfig(); });
  _server.on("/api/config", HTTP_POST, [this]() { _handleSetConfig(); });
  _server.on("/api/reboot", HTTP_POST, [this]() { _handleReboot(); });
  _server.on("/api/wifi-reset", HTTP_POST, [this]() { _handleWifiReset(); });
  _server.onNotFound([this]() { _handleNotFound(); });

  _server.begin();

  if (MDNS.begin("talkbot")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: talkbot.local");
  }
  Serial.println("Web server iniciado en puerto 80");
}

void TalkbotWebServer::handleClient() {
  _server.handleClient();
}

void TalkbotWebServer::_handleRoot() {
  _handleFile("/index.html", "text/html");
}

void TalkbotWebServer::_handleFile(const String& path, const String& contentType) {
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    _server.streamFile(file, contentType);
    file.close();
  } else {
    _server.send(404, "text/plain", "Archivo no encontrado: " + path);
  }
}

void TalkbotWebServer::_handleStatus() {
  JsonDocument doc;
  doc["state"] = _stateToString(*_statePtr);
  doc["volume"] = _player->getVolume();
  doc["uptime"] = (millis() - _startTime) / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  doc["displayScreen"] = _display->getScreen();
  doc["peakLevel"] = _display->getPeakLevel();
  doc["conversations"] = _display->getConversationCount();
  doc["avgLatency"] = _display->getAvgLatency();
  doc["lastQuestion"] = _display->getLastQuestion();
  doc["lastAnswer"] = _display->getLastAnswer();

  String response;
  serializeJson(doc, response);
  _server.send(200, "application/json", response);
}

void TalkbotWebServer::_handleSetVolume() {
  if (!_server.hasArg("plain")) {
    _server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, _server.arg("plain"));
  if (err) {
    _server.send(400, "application/json", "{\"error\":\"JSON inválido\"}");
    return;
  }

  if (!doc["volume"].isNull()) {
    uint8_t vol = doc["volume"].as<uint8_t>();
    _player->setVolume(vol);
    _display->setVolume(vol);
    Serial.printf("Volumen ajustado a: %d\n", vol);
    _server.send(200, "application/json", "{\"ok\":true,\"volume\":" + String(vol) + "}");
  } else {
    _server.send(400, "application/json", "{\"error\":\"Falta campo volume\"}");
  }
}

void TalkbotWebServer::_handleGetConfig() {
  JsonDocument doc;
  doc["agent"] = _agentConfig.agentName;
  doc["voice"] = _agentConfig.voiceId;
  doc["volume"] = _player->getVolume();
  doc["backendUrl"] = _backendUrl;

  String response;
  serializeJson(doc, response);
  _server.send(200, "application/json", response);
}

void TalkbotWebServer::_handleSetConfig() {
  if (!_server.hasArg("plain")) {
    _server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, _server.arg("plain"));
  if (err) {
    _server.send(400, "application/json", "{\"error\":\"JSON inválido\"}");
    return;
  }

  if (!doc["agent"].isNull()) {
    String agent = doc["agent"].as<String>();
    if (agent == "lupe") {
      _agentConfig.agentName = "lupe";
      _agentConfig.voiceId = "Lupe";
    } else if (agent == "pedro") {
      _agentConfig.agentName = "pedro";
      _agentConfig.voiceId = "Pedro";
    } else if (agent == "mia") {
      _agentConfig.agentName = "mia";
      _agentConfig.voiceId = "Mia";
    } else {
      _server.send(400, "application/json", "{\"error\":\"Agente no válido\"}");
      return;
    }
    Serial.printf("Agente cambiado a: %s (voz: %s)\n", _agentConfig.agentName.c_str(), _agentConfig.voiceId.c_str());
  }

  if (!doc["volume"].isNull()) {
    uint8_t vol = doc["volume"].as<uint8_t>();
    _player->setVolume(vol);
    _display->setVolume(vol);
    Serial.printf("Volumen ajustado a: %d\n", vol);
  }

  if (!doc["backendUrl"].isNull()) {
    _backendUrl = doc["backendUrl"].as<String>();
    _prefs.putString("backendUrl", _backendUrl);
    if (_api) _api->begin(_backendUrl.c_str());
    Serial.printf("Backend URL cambiado a: %s (aplicado)\n", _backendUrl.c_str());
  }

  JsonDocument resp;
  resp["ok"] = true;
  resp["agent"] = _agentConfig.agentName;
  resp["voice"] = _agentConfig.voiceId;
  resp["volume"] = _player->getVolume();
  resp["backendUrl"] = _backendUrl;

  String response;
  serializeJson(resp, response);
  _server.send(200, "application/json", response);
}

void TalkbotWebServer::_handleReboot() {
  _server.send(200, "application/json", "{\"ok\":true,\"message\":\"Reiniciando...\"}");
  delay(500);
  ESP.restart();
}

void TalkbotWebServer::_handleWifiReset() {
  _server.send(200, "application/json", "{\"ok\":true,\"message\":\"Reseteando WiFi...\"}");
  delay(500);
  WiFiManager wm;
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

void TalkbotWebServer::_handleNotFound() {
  _server.send(404, "text/plain", "Not Found");
}

String TalkbotWebServer::_stateToString(TalkbotState state) {
  switch (state) {
    case STATE_IDLE:       return "idle";
    case STATE_LISTENING:  return "listening";
    case STATE_PROCESSING: return "processing";
    case STATE_SPEAKING:   return "speaking";
    case STATE_ERROR:      return "error";
    default:               return "unknown";
  }
}
