#ifndef CONFIG_H
#define CONFIG_H

// ============== WiFi (WiFiManager) ==============
#define WM_AP_NAME    "Talkbot-Setup"   // Nombre de la red que crea el ESP32
#define WM_AP_PASS    "talkbot123"      // Contraseña de la red de configuración
#define WM_TIMEOUT    120               // Segundos esperando config (2 min)
// ============== Backend ==============
#define BACKEND_URL   "http://192.168.0.100:8000"  // Cambiar a la IP del backend
#define CHAT_ENDPOINT "/api/chat"
#define CHAT_TIMEOUT  60000  // 60s timeout para STT+LLM+TTS
#define API_KEY       ""  // Opcional: proteger backend con API key

// ============== Pines I2S - MAX98357 (Speaker) ==============
#define I2S_BCLK_PIN  26
#define I2S_LRC_PIN   25
#define I2S_DOUT_PIN  22

// ============== Pin ADC - MAX9814 (Mic) ==============
#define MIC_PIN       34  // ADC1_CH6, solo input

// ============== Botón Push-to-Talk ==============
#define BUTTON_PIN    27  // INPUT_PULLUP, activo LOW
#define DEBOUNCE_MS   50

// ============== Switch LEDs On/Off ==============
#define LED_SWITCH_PIN 33  // INPUT_PULLUP, LOW = LEDs apagados

// ============== LEDs ==============
#define LED_RED_PIN   15  // Hablando (speaking)
#define LED_YELLOW_PIN 2  // Procesando (thinking)
#define LED_GREEN_PIN  4  // Escuchando (listening)

// ============== Audio Config ==============
#define SAMPLE_RATE       16000   // 16kHz para voz
#define BITS_PER_SAMPLE   16
#define NUM_CHANNELS      1       // Mono
#define MAX_RECORD_SECS   3       // Máximo 3 segundos de grabación
#define AUDIO_BUFFER_SIZE (SAMPLE_RATE * sizeof(int16_t) * MAX_RECORD_SECS)  // ~320KB

// ============== I2S Playback Config ==============
#define PLAYBACK_SAMPLE_RATE  22050  // TTS output rate
#define I2S_DMA_BUF_COUNT     8
#define I2S_DMA_BUF_LEN       1024

// ============== Agentes predefinidos ==============
struct AgentConfig {
  String agentName = "lupe";
  String voiceId   = "Lupe";
};

// ============== Estados ==============
enum TalkbotState {
  STATE_IDLE,
  STATE_LISTENING,
  STATE_PROCESSING,
  STATE_SPEAKING,
  STATE_ERROR,
  STATE_WIFI_CONFIG
};

#endif
