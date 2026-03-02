/*
 * Talkbot - Asistente de voz estilo Alexa con ESP32
 * Push-to-talk con MAX9814 (mic) + MAX98357 (speaker)
 * Backend Python: Amazon Transcribe + Claude Bedrock + ElevenLabs
 * Display ST7789 240x240 para feedback visual
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Ticker.h>
#include "config.h"
#include "display_controller.h"
#include "audio_recorder.h"
#include "audio_player.h"
#include "api_client.h"
#include "web_server.h"
#include "esp_bt.h"

// Componentes
DisplayController display;
AudioRecorder recorder;
AudioPlayer player;
ApiClient api;
TalkbotWebServer webServer;

// Estado actual
TalkbotState currentState = STATE_IDLE;

// Botón push-to-talk
volatile bool buttonPressed = false;
unsigned long lastDebounce = 0;
bool lastButtonState = HIGH;

// Botones Vol+/Vol-/Pantalla
unsigned long lastVolUpPress = 0;
unsigned long lastVolDownPress = 0;
unsigned long lastScreenPress = 0;

// Error timeout
unsigned long errorStartTime = 0;
#define ERROR_DISPLAY_MS 3000

// Ticker para display durante WiFi config (autoConnect es bloqueante)
Ticker displayTicker;
void displayTickerCallback() { display.update(); }

// --- Task handle para grabación en segundo plano ---
TaskHandle_t recordTaskHandle = NULL;
volatile bool recordTaskDone = false;

// Forward declarations
void handleButton(bool state);
void handleButtons();
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
  // Liberar memoria del Bluetooth (~64KB) - no lo usamos
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  Serial.println("\n=== Talkbot Iniciando ===");

  // Display
  display.begin();
  display.setState(STATE_PROCESSING);  // Amarillo mientras inicia

  // Botón push-to-talk
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Botones Vol+/Vol-/Pantalla
  pinMode(BTN_VOL_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_VOL_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_SCREEN_PIN, INPUT_PULLUP);

  // Audio
  if (!recorder.begin()) {
    Serial.println("FATAL: No se pudo inicializar el grabador");
    display.setState(STATE_ERROR);
    while (true) { display.update(); delay(10); }
  }
  player.begin();
  player.setVolume(70);

  // Test de sonido: 3 beeps ascendentes
  player.playTestTone(440, 500);   // La  - grave
  delay(100);
  player.playTestTone(880, 500);   // La  - agudo
  delay(100);
  player.playTestTone(1320, 800);  // Mi  - más agudo, más largo

  // WiFi via WiFiManager (captive portal)
  WiFiManager wm;
  wm.setConfigPortalTimeout(WM_TIMEOUT);
  wm.setConnectTimeout(10);

  // Cuando entra en modo AP, activar display de configuración
  wm.setAPCallback([](WiFiManager* wm) {
    Serial.println(">> Modo configuración WiFi activado");
    Serial.println(">> Conectate a la red: " WM_AP_NAME);
    display.setState(STATE_WIFI_CONFIG);
    // Ticker para actualizar display mientras autoConnect bloquea
    displayTicker.attach_ms(50, displayTickerCallback);
  });

  Serial.println("Conectando a WiFi...");
  Serial.println("Si no conecta, busca la red: " WM_AP_NAME);

  if (!wm.autoConnect(WM_AP_NAME, WM_AP_PASS)) {
    displayTicker.detach();
    Serial.println("Error: No se pudo conectar a WiFi (timeout)");
    Serial.println("Reiniciando en 3 segundos...");
    display.setState(STATE_ERROR);
    delay(3000);
    ESP.restart();
  }

  displayTicker.detach();
  Serial.printf("WiFi conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  // mDNS → accesible como http://talkbot.local
  if (MDNS.begin("talkbot")) {
    Serial.println("mDNS: http://talkbot.local");
  }

  // Web Server (cargar config antes de API Client)
  webServer.begin(&display, &player, &currentState, &api);

  // API Client (usa URL guardada en NVS)
  api.begin(webServer.getBackendUrl().c_str());

  // Verificar backend (un solo intento, no bloquear)
  if (api.checkHealth()) {
    Serial.println("Backend disponible");
  } else {
    Serial.println("AVISO: Backend no responde (continuando sin backend)");
  }

  // Listo
  display.setState(STATE_IDLE);
  display.setVolume(player.getVolume());
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

  // Actualizar VU meter si está grabando
  if (currentState == STATE_LISTENING) {
    display.setPeakLevel(recorder.getPeakLevel());
  }

  // Actualizar display con rate limit para reducir ruido SPI en DAC
  static unsigned long lastDisplayUpdate = 0;
  if (currentState == STATE_LISTENING) {
    // VU meter: actualizar más seguido
    if (millis() - lastDisplayUpdate > 50) {
      display.update();
      lastDisplayUpdate = millis();
    }
  } else if (currentState != STATE_SPEAKING) {
    // Idle/Processing/Error: actualizar lento
    if (millis() - lastDisplayUpdate > 500) {
      display.update();
      lastDisplayUpdate = millis();
    }
  }

  // Botón push-to-talk
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState != lastButtonState) {
    lastDebounce = millis();
  }
  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    handleButton(currentButtonState);
  }
  lastButtonState = currentButtonState;

  // Botones Vol+/Vol-/Pantalla
  handleButtons();

  // Timeout de error
  if (currentState == STATE_ERROR && (millis() - errorStartTime > ERROR_DISPLAY_MS)) {
    currentState = STATE_IDLE;
    display.setState(STATE_IDLE);
  }
}

void handleButtons() {
  unsigned long now = millis();

  // Vol+
  bool volUpPressed = (digitalRead(BTN_VOL_UP_PIN) == LOW);
  if (volUpPressed && (now - lastVolUpPress) > BTN_DEBOUNCE_MS) {
    int vol = player.getVolume();
    int newVol = min(100, vol + VOLUME_STEP);
    player.setVolume(newVol);
    display.setVolume(newVol);
    Serial.printf("[BTN] Vol+ → %d\n", newVol);
    lastVolUpPress = now;
  }
  if (!volUpPressed) lastVolUpPress = 0;

  // Vol-
  bool volDownPressed = (digitalRead(BTN_VOL_DOWN_PIN) == LOW);
  if (volDownPressed && (now - lastVolDownPress) > BTN_DEBOUNCE_MS) {
    int vol = player.getVolume();
    int newVol = max(0, vol - VOLUME_STEP);
    player.setVolume(newVol);
    display.setVolume(newVol);
    Serial.printf("[BTN] Vol- → %d\n", newVol);
    lastVolDownPress = now;
  }
  if (!volDownPressed) lastVolDownPress = 0;

  // Pantalla
  bool screenPressed = (digitalRead(BTN_SCREEN_PIN) == LOW);
  if (screenPressed && (now - lastScreenPress) > BTN_DEBOUNCE_MS) {
    display.nextScreen();
    Serial.println("[BTN] Pantalla → siguiente");
    lastScreenPress = now;
  }
  if (!screenPressed) lastScreenPress = 0;
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
  display.setState(STATE_LISTENING);

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
  display.setState(STATE_PROCESSING);

  // Verificar que tenemos audio
  if (recorder.getBuffer() == nullptr || recorder.getBufferSize() == 0) {
    setError("No se grabó audio");
    return;
  }

  Serial.printf("Audio grabado: %d bytes, heap: %d\n", recorder.getBufferSize(), ESP.getFreeHeap());

  // Medir latencia
  unsigned long latencyStart = millis();

  // Enviar al backend y reproducir en streaming
  currentState = STATE_SPEAKING;
  display.setState(STATE_SPEAKING);

  bool ok = api.sendAudioAndPlay(
    recorder.getBuffer(),
    recorder.getBufferSize(),
    player,
    webServer.getVoiceId(),
    webServer.getAgentName(),
    []() { recorder.freeBuffer(); }
  );

  uint32_t latency = millis() - latencyStart;
  display.setLatency(latency);
  display.addConversation();

  if (!ok) {
    setError(api.getLastError().c_str());
    return;
  }

  // Volver a idle
  currentState = STATE_IDLE;
  display.setState(STATE_IDLE);
  Serial.println(">> Listo");
  Serial.printf("Heap libre: %d bytes\n", ESP.getFreeHeap());
}

void setError(const char* message) {
  Serial.printf("ERROR: %s\n", message);
  currentState = STATE_ERROR;
  display.setState(STATE_ERROR);
  errorStartTime = millis();
}
