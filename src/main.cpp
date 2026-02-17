/*
 * Talkbot - Asistente de voz estilo Alexa con ESP32
 * Push-to-talk con MAX9814 (mic) + MAX98357 (speaker)
 * Backend Python: Amazon Transcribe + Claude Bedrock + ElevenLabs
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Ticker.h>
#include "config.h"
#include "led_controller.h"
#include "audio_recorder.h"
#include "audio_player.h"
#include "api_client.h"
#include "web_server.h"

// Componentes
LedController leds;
AudioRecorder recorder;
AudioPlayer player;
ApiClient api;
TalkbotWebServer webServer;

// Estado actual
TalkbotState currentState = STATE_IDLE;

// Botón
volatile bool buttonPressed = false;
unsigned long lastDebounce = 0;
bool lastButtonState = HIGH;

// Error timeout
unsigned long errorStartTime = 0;
#define ERROR_DISPLAY_MS 3000

// Ticker para LEDs durante WiFi config (autoConnect es bloqueante)
Ticker ledTicker;
void ledTickerCallback() { leds.update(); }

// --- Task handle para grabación en segundo plano ---
TaskHandle_t recordTaskHandle = NULL;
volatile bool recordTaskDone = false;

// Forward declarations (necesarias en .cpp, no en .ino)
void handleButton(bool state);
void startListening();
void stopListeningAndProcess();
void setError(const char* message);

void recordTask(void* param) {
  recorder.startRecording();
  recordTaskDone = true;
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Talkbot Iniciando ===");

  // LEDs
  leds.begin();
  leds.setState(STATE_PROCESSING);  // Amarillo mientras inicia

  // Botón push-to-talk
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_SWITCH_PIN, INPUT_PULLUP);

  // Audio
  if (!recorder.begin()) {
    Serial.println("FATAL: No se pudo inicializar el grabador");
    leds.setState(STATE_ERROR);
    while (true) { leds.update(); delay(10); }
  }
  player.begin();

  // WiFi via WiFiManager (captive portal)
  WiFiManager wm;
  wm.setConfigPortalTimeout(WM_TIMEOUT);
  wm.setConnectTimeout(10);

  // Cuando entra en modo AP, activar LEDs de configuración
  wm.setAPCallback([](WiFiManager* wm) {
    Serial.println(">> Modo configuración WiFi activado");
    Serial.println(">> Conectate a la red: " WM_AP_NAME);
    leds.setState(STATE_WIFI_CONFIG);
    // Ticker para actualizar LEDs mientras autoConnect bloquea
    ledTicker.attach_ms(50, ledTickerCallback);
  });

  Serial.println("Conectando a WiFi...");
  Serial.println("Si no conecta, busca la red: " WM_AP_NAME);

  if (!wm.autoConnect(WM_AP_NAME, WM_AP_PASS)) {
    ledTicker.detach();
    Serial.println("Error: No se pudo conectar a WiFi (timeout)");
    Serial.println("Reiniciando en 3 segundos...");
    leds.setState(STATE_ERROR);
    delay(3000);
    ESP.restart();
  }

  ledTicker.detach();
  Serial.printf("WiFi conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  // mDNS → accesible como http://talkbot.local
  if (MDNS.begin("talkbot")) {
    Serial.println("mDNS: http://talkbot.local");
  }

  // Web Server (cargar config antes de API Client)
  webServer.begin(&leds, &player, &currentState, &api);

  // API Client (usa URL guardada en NVS)
  api.begin(webServer.getBackendUrl().c_str());

  // Verificar backend (reintentar, atender web server mientras espera)
  Serial.println("Esperando backend...");
  leds.setState(STATE_ERROR);  // Rojo hasta que responda
  bool backendOk = false;
  for (int i = 0; i < 10; i++) {
    leds.update();
    webServer.handleClient();  // Atender web UI durante espera
    if (api.checkHealth()) {
      backendOk = true;
      break;
    }
    Serial.printf("Reintento %d/10...\n", i + 1);
    // Esperar 3s pero seguir atendiendo web server
    unsigned long waitStart = millis();
    while (millis() - waitStart < 3000) {
      webServer.handleClient();
      leds.update();
      delay(10);
    }
  }
  Serial.println(backendOk ? "Backend disponible" : "AVISO: Backend no respondió (continuando)");

  // Listo
  leds.setState(STATE_IDLE);
  Serial.println("=== Talkbot Listo ===");
  Serial.printf("Heap libre: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
  static bool firstRun = true;
  if (firstRun) {
    Serial.println(">>> loop() iniciado");
    firstRun = false;
  }

  // Manejar web server
  webServer.handleClient();

  // Switch de LEDs: desactivado (pin 33 flota sin switch conectado)
  // bool switchClosed = (digitalRead(LED_SWITCH_PIN) == LOW);
  // bool ledsShould = !switchClosed;
  // if (ledsShould != leds.isEnabled()) {
  //   leds.setEnabled(ledsShould);
  //   Serial.printf("LEDs: %s\n", ledsShould ? "ON" : "OFF");
  // }

  // Actualizar LEDs (parpadeo en error)
  leds.update();

  // Botón push-to-talk
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState != lastButtonState) {
    lastDebounce = millis();
  }
  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    handleButton(currentButtonState);
  }
  lastButtonState = currentButtonState;

  // Timeout de error
  if (currentState == STATE_ERROR && (millis() - errorStartTime > ERROR_DISPLAY_MS)) {
    currentState = STATE_IDLE;
    leds.setState(STATE_IDLE);
  }
}

void handleButton(bool state) {
  // Botón presionado (LOW porque es INPUT_PULLUP)
  if (state == LOW && currentState == STATE_IDLE) {
    startListening();
  }
  // Botón suelto
  else if (state == HIGH && currentState == STATE_LISTENING) {
    stopListeningAndProcess();
  }
}

void startListening() {
  Serial.println(">> Escuchando...");
  currentState = STATE_LISTENING;
  leds.setState(STATE_LISTENING);

  recordTaskDone = false;
  xTaskCreatePinnedToCore(
    recordTask,
    "RecordTask",
    8192,
    NULL,
    1,
    &recordTaskHandle,
    1  // Core 1
  );
}

void stopListeningAndProcess() {
  Serial.println(">> Procesando...");

  // Señalar que pare de grabar y esperar a que termine la tarea
  recorder.stopRecording();
  unsigned long waitStart = millis();
  while (!recordTaskDone && (millis() - waitStart < 2000)) {
    delay(10);
  }
  recordTaskHandle = NULL;

  currentState = STATE_PROCESSING;
  leds.setState(STATE_PROCESSING);

  // Verificar que tenemos audio
  if (recorder.getBuffer() == nullptr || recorder.getBufferSize() == 0) {
    setError("No se grabó audio");
    return;
  }

  Serial.printf("Audio grabado: %d bytes\n", recorder.getBufferSize());

  // Enviar al backend y reproducir en streaming
  bool ok = api.sendAudioAndPlay(
    recorder.getBuffer(),
    recorder.getBufferSize(),
    player,
    webServer.getVoiceId(),
    webServer.getAgentName()
  );

  // Ya no necesitamos el buffer de grabación
  recorder.freeBuffer();

  if (!ok) {
    setError(api.getLastError().c_str());
    return;
  }

  // Volver a idle
  currentState = STATE_IDLE;
  leds.setState(STATE_IDLE);
  Serial.println(">> Listo");
  Serial.printf("Heap libre: %d bytes\n", ESP.getFreeHeap());
}

void setError(const char* message) {
  Serial.printf("ERROR: %s\n", message);
  currentState = STATE_ERROR;
  leds.setState(STATE_ERROR);
  errorStartTime = millis();
}
