#ifndef CONFIG_H
#define CONFIG_H

// ============== Feature Flags ==============
// Deshabilitar componentes comentando estas líneas
#define ENABLE_WIFI
#define ENABLE_AUDIO
#define ENABLE_BUTTONS
#define ENABLE_WEBSERVER
#define ENABLE_API
#define ENABLE_DISPLAY

// ============== WiFi (WiFiManager) ==============
#define WM_AP_NAME    "Talkbot-Setup"   // Nombre de la red que crea el ESP32
#define WM_AP_PASS    "talkbot123"      // Contraseña de la red de configuración
#define WM_TIMEOUT    120               // Segundos esperando config (2 min)
// ============== Backend ==============
#define BACKEND_URL   "http://192.168.0.187:8000"
#define CHAT_ENDPOINT "/api/chat"
#define CHAT_TIMEOUT  60000  // 60s timeout para STT+LLM+TTS
#define API_KEY       "6a230fa1160c238756c60531eb07fd87387ac305d430e002"

// ============== Pines I2S (reservados, no usados con DAC) ==============
#define I2S_BCLK_PIN  26
#define I2S_LRC_PIN   25
#define I2S_DOUT_PIN  22

// ============== Pin ADC - MAX9814 (Mic) ==============
#define MIC_PIN       35  // ADC1_CH7, solo input

// ============== Botón Push-to-Talk ==============
#define BUTTON_PIN    27  // INPUT_PULLUP, activo LOW
#define DEBOUNCE_MS   50

// ============== Botones Vol+/Vol-/Pantalla ==============
#define BTN_VOL_UP_PIN   14
#define BTN_VOL_DOWN_PIN 12
#define BTN_SCREEN_PIN   13
#define BTN_DEBOUNCE_MS  200  // Más largo para evitar repeticiones
#define BTN_REPEAT_MS    300  // Auto-repeat al mantener presionado
#define VOLUME_STEP      10

// ============== Display ILI9341 240x320 ==============
#define TFT_BL_PIN    15  // Backlight PWM
#define TFT_BL_CHAN    0  // LEDC channel for backlight
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320
#define NUM_SCREENS     6  // Pantallas navegables (0-5): Estado, Vol, WiFi, Chat, Stats, VU

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
