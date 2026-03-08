/*
 * Talkbot - ESP32 Voice Assistant
 * Componentes habilitados via feature flags en config.h
 */

#include <Arduino.h>
#include "config.h"

#ifdef ENABLE_WIFI
#include <WiFi.h>
#include <WiFiManager.h>
#endif

#ifdef ENABLE_DISPLAY
#include "display_controller.h"
DisplayController display;
#endif

#ifdef ENABLE_AUDIO
#include "audio_recorder.h"
#include "audio_player.h"
AudioRecorder recorder;
AudioPlayer player;
#endif

#ifdef ENABLE_API
#include "api_client.h"
ApiClient apiClient;
#endif

#ifdef ENABLE_WEBSERVER
#include "web_server.h"
TalkbotWebServer webServer;
#endif

// Estado global
TalkbotState currentState = STATE_IDLE;

#if defined(ENABLE_AUDIO) && defined(ENABLE_DISPLAY)
void onPeakUpdate(float level) {
  display.setPeakLevel(level);
  display.update();
}
#endif

void setState(TalkbotState newState) {
  currentState = newState;
#ifdef ENABLE_DISPLAY
  display.setState(newState);
#endif
  Serial.printf("[Main] Estado: %d\n", newState);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== TALKBOT ===");

  // --- WiFi ---
#ifdef ENABLE_WIFI
  Serial.println("[WiFi] Iniciando WiFiManager...");
  #ifdef ENABLE_DISPLAY
  setState(STATE_WIFI_CONFIG);
  #endif
  WiFiManager wm;
  wm.setConfigPortalTimeout(WM_TIMEOUT);
  if (!wm.autoConnect(WM_AP_NAME, WM_AP_PASS)) {
    Serial.println("[WiFi] Fallo conexion, reiniciando...");
    ESP.restart();
  }
  Serial.printf("[WiFi] Conectado: %s\n", WiFi.localIP().toString().c_str());
#endif

  // --- Display ---
#ifdef ENABLE_DISPLAY
  display.begin();
  setState(STATE_IDLE);
  Serial.println("[Display] OK");
#endif

  // --- Audio ---
#ifdef ENABLE_AUDIO
  recorder.begin();
  player.begin();
  Serial.println("[Audio] OK");
#endif

  // --- API Client ---
#ifdef ENABLE_API
  apiClient.begin(BACKEND_URL);
  Serial.println("[API] OK");
#endif

  // --- Web Server ---
#ifdef ENABLE_WEBSERVER
  webServer.begin(&display, &player, &currentState, &apiClient);
  Serial.println("[WebServer] OK");
#endif

  // --- Botones ---
#ifdef ENABLE_BUTTONS
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BTN_VOL_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_VOL_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_SCREEN_PIN, INPUT_PULLUP);
  Serial.println("[Buttons] OK");
#endif

  Serial.println("=== SETUP COMPLETO ===");
}

void loop() {
  // --- Display update ---
#ifdef ENABLE_DISPLAY
  display.update();
#endif

  // --- Web Server ---
#ifdef ENABLE_WEBSERVER
  webServer.handleClient();
#endif

  // --- PTT Button ---
#ifdef ENABLE_BUTTONS
#ifdef ENABLE_AUDIO
  static unsigned long lastDebounce = 0;
  static bool pttWasPressed = false;
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  // Solo en flanco de bajada (nueva presion), no mientras mantiene
  if (pressed && !pttWasPressed && currentState == STATE_IDLE && (millis() - lastDebounce > DEBOUNCE_MS)) {
    lastDebounce = millis();
    pttWasPressed = true;
    setState(STATE_LISTENING);
    #ifdef ENABLE_DISPLAY
    recorder.startRecording(onPeakUpdate);  // Bloquea con VU en vivo
    #else
    recorder.startRecording();
    #endif

    // Grabacion terminada
    Serial.printf("[Mic] Grabado: %d bytes, peak: %.2f\n",
                  recorder.getBufferSize(), recorder.getPeakLevel());

#ifdef ENABLE_API
    setState(STATE_PROCESSING);
    #ifdef ENABLE_DISPLAY
    display.update();  // Forzar redibujado antes del bloqueo
    #endif
    bool ok = apiClient.sendAudioAndPlay(
      recorder.getBuffer(), recorder.getBufferSize(), player,
      webServer.getVoiceId(), webServer.getAgentName(),
      [](){ recorder.freeBuffer(); }  // Liberar buffer tras enviar
    );

    if (ok) {
      setState(STATE_SPEAKING);
      #ifdef ENABLE_DISPLAY
      display.setConversation(apiClient.getLastUserText(), apiClient.getLastBotText());
      display.addConversation();
      #endif
      setState(STATE_IDLE);
    } else {
      Serial.printf("[API] Error: %s\n", apiClient.getLastError().c_str());
      setState(STATE_ERROR);
      delay(2000);
      setState(STATE_IDLE);
    }
#else
    // Sin API: mostrar resultado y volver a IDLE
    #ifdef ENABLE_DISPLAY
    display.setPeakLevel(recorder.getPeakLevel());
    #endif
    setState(STATE_IDLE);
#endif // ENABLE_API

    recorder.freeBuffer();
  }
  if (!pressed) pttWasPressed = false;
#endif // ENABLE_AUDIO

  // --- Vol Up ---
  static unsigned long lastVolUp = 0;
  static uint8_t currentVolume = 80;
  if (digitalRead(BTN_VOL_UP_PIN) == LOW && (millis() - lastVolUp > BTN_DEBOUNCE_MS)) {
    lastVolUp = millis();
    currentVolume = min(100, currentVolume + VOLUME_STEP);
    #ifdef ENABLE_AUDIO
    player.setVolume(currentVolume);
    #endif
    #ifdef ENABLE_DISPLAY
    display.setVolume(currentVolume);
    display.setScreen(1);  // Auto-switch a pantalla volumen
    #endif
    Serial.printf("[Vol] %d%%\n", currentVolume);
  }

  // --- Vol Down ---
  static unsigned long lastVolDown = 0;
  if (digitalRead(BTN_VOL_DOWN_PIN) == LOW && (millis() - lastVolDown > BTN_DEBOUNCE_MS)) {
    lastVolDown = millis();
    currentVolume = (currentVolume >= VOLUME_STEP) ? currentVolume - VOLUME_STEP : 0;
    #ifdef ENABLE_AUDIO
    player.setVolume(currentVolume);
    #endif
    #ifdef ENABLE_DISPLAY
    display.setVolume(currentVolume);
    display.setScreen(1);  // Auto-switch a pantalla volumen
    #endif
    Serial.printf("[Vol] %d%%\n", currentVolume);
  }

  // --- Screen button ---
  static unsigned long lastScreen = 0;
  if (digitalRead(BTN_SCREEN_PIN) == LOW && (millis() - lastScreen > BTN_DEBOUNCE_MS)) {
    lastScreen = millis();
    #ifdef ENABLE_DISPLAY
    display.nextScreen();
    #endif
  }
#endif // ENABLE_BUTTONS

  delay(10);
}
