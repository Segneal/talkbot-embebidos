#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// Colores del tema (RGB565)
#define CLR_BG        0x0000  // Negro
#define CLR_TEXT      0xFFFF  // Blanco
#define CLR_DIM       0x7BEF  // Gris
#define CLR_ACCENT    0x07FF  // Cyan
#define CLR_GREEN     0x07E0
#define CLR_RED       0xF800
#define CLR_YELLOW    0xFFE0
#define CLR_ORANGE    0xFD20
#define CLR_BLUE      0x001F
#define CLR_DARK_GRAY 0x2104

class DisplayController {
public:
  void begin();
  void setState(TalkbotState state);
  void update();  // Llamar en loop()
  bool isEnabled() { return _enabled; }
  void setEnabled(bool enabled);

  // Navegación entre pantallas
  void nextScreen();
  void prevScreen();
  void setScreen(uint8_t screen);
  uint8_t getScreen() { return _currentScreen; }

  // Datos para pantallas
  void setVolume(uint8_t vol);
  void setPeakLevel(float level);  // 0.0 - 1.0
  void setConversation(const String& question, const String& answer);
  void addConversation();  // Incrementa contador
  void setLatency(uint32_t ms);

private:
  TFT_eSPI _tft = TFT_eSPI(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  TFT_eSprite _spr = TFT_eSprite(&_tft);  // Sprite de línea para anti-flicker

  bool _enabled = true;
  TalkbotState _currentState = STATE_IDLE;
  uint8_t _currentScreen = 0;
  bool _needsFullRedraw = true;
  unsigned long _lastUpdate = 0;
  unsigned long _animTimer = 0;
  uint8_t _animFrame = 0;

  // Datos
  uint8_t _volume = 80;
  float _peakLevel = 0.0f;
  String _lastQuestion;
  String _lastAnswer;
  uint32_t _conversationCount = 0;
  uint32_t _avgLatency = 0;
  uint32_t _latencySum = 0;
  uint32_t _latencyCount = 0;

  // Cached values para updates parciales
  float _prevPeakLevel = -1.0f;
  uint8_t _prevVolume = 255;
  int32_t _prevRssi = 1;
  uint32_t _prevHeap = 0;
  TalkbotState _prevState = STATE_IDLE;
  uint8_t _prevAnimFrame = 255;

  // Dibujo de pantallas
  void _drawScreen0_Status();    // Estado actual
  void _drawScreen1_Volume();    // Volumen
  void _drawScreen2_WiFi();      // WiFi info
  void _drawScreen3_Conversation(); // Última conversación
  void _drawScreen4_Stats();     // Estadísticas
  void _drawScreen5_VU();        // VU Meter

  // Helpers
  void _drawHeader(const char* title);
  void _drawNavDots();
  void _drawProgressBar(int x, int y, int w, int h, float pct, uint16_t color);
  void _drawCenteredText(const char* text, int y, uint8_t font, uint16_t color);
  void _drawStateIcon(int cx, int cy, TalkbotState state);
  void _initBacklight();
  void _setBacklight(uint8_t brightness);
  const char* _stateToString(TalkbotState state);
  uint16_t _stateToColor(TalkbotState state);
};

#endif
