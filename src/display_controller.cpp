#include "display_controller.h"
#ifdef ENABLE_DISPLAY
#include <WiFi.h>

// Update intervals (ms)
#define UPDATE_INTERVAL_NORMAL 500
#define UPDATE_INTERVAL_VU     50
#define ANIM_INTERVAL         300

void DisplayController::begin() {
  _tft.init();
  _tft.setRotation(0);
  _tft.fillScreen(CLR_BG);

  // Sprite de linea: 240 x 30 pixels (~14KB)
  _spr.createSprite(DISPLAY_WIDTH, 30);
  _spr.setTextDatum(MC_DATUM);

  _initBacklight();
  _setBacklight(200);

  _needsFullRedraw = true;
  Serial.println("[Display] ILI9341 240x320 inicializado");
}

void DisplayController::_initBacklight() {
  ledcSetup(TFT_BL_CHAN, 5000, 8);  // 5kHz, 8-bit
  ledcAttachPin(TFT_BL_PIN, TFT_BL_CHAN);
  ledcWrite(TFT_BL_CHAN, 0);
}

void DisplayController::_setBacklight(uint8_t brightness) {
  ledcWrite(TFT_BL_CHAN, brightness);
}

void DisplayController::setState(TalkbotState state) {
  if (_currentState == state) return;
  _prevState = _currentState;
  _currentState = state;
  _animFrame = 0;
  _animTimer = millis();

  // Auto-switch: VU al grabar, Estado al procesar/hablar
  if (state == STATE_LISTENING) {
    _currentScreen = 5;  // VU Meter
  } else if (state == STATE_PROCESSING || state == STATE_SPEAKING) {
    _currentScreen = 0;  // Estado (muestra PROCESANDO/HABLANDO)
  }

  _needsFullRedraw = true;
  Serial.printf("[Display] Estado: %s\n", _stateToString(state));
}

void DisplayController::setEnabled(bool enabled) {
  _enabled = enabled;
  _setBacklight(enabled ? 200 : 0);
  if (enabled) _needsFullRedraw = true;
}

void DisplayController::nextScreen() {
  _currentScreen = (_currentScreen + 1) % NUM_SCREENS;
  _needsFullRedraw = true;
}

void DisplayController::prevScreen() {
  _currentScreen = (_currentScreen + NUM_SCREENS - 1) % NUM_SCREENS;
  _needsFullRedraw = true;
}

void DisplayController::setScreen(uint8_t screen) {
  if (screen >= NUM_SCREENS) return;
  _currentScreen = screen;
  _needsFullRedraw = true;
}

void DisplayController::setVolume(uint8_t vol) {
  _volume = vol;
}

void DisplayController::setPeakLevel(float level) {
  _peakLevel = constrain(level, 0.0f, 1.0f);
}

void DisplayController::setConversation(const String& question, const String& answer) {
  _lastQuestion = question;
  _lastAnswer = answer;
}

void DisplayController::addConversation() {
  _conversationCount++;
}

void DisplayController::setLatency(uint32_t ms) {
  _latencySum += ms;
  _latencyCount++;
  _avgLatency = _latencySum / _latencyCount;
}

void DisplayController::update() {
  if (!_enabled) return;

  unsigned long now = millis();
  unsigned long interval = (_currentScreen == 5) ? UPDATE_INTERVAL_VU : UPDATE_INTERVAL_NORMAL;

  // Animacion del icono de estado
  if (now - _animTimer >= ANIM_INTERVAL) {
    _animTimer = now;
    _animFrame = (_animFrame + 1) % 4;
  }

  if (!_needsFullRedraw && (now - _lastUpdate < interval)) return;
  _lastUpdate = now;

  if (_needsFullRedraw) {
    _tft.fillScreen(CLR_BG);
    _needsFullRedraw = false;
    // Reset cached values to force full draw
    _prevPeakLevel = -1.0f;
    _prevVolume = 255;
    _prevRssi = 1;
    _prevHeap = 0;
    _prevAnimFrame = 255;
  }

  switch (_currentScreen) {
    case 0: _drawScreen0_Status(); break;
    case 1: _drawScreen1_Volume(); break;
    case 2: _drawScreen2_WiFi(); break;
    case 3: _drawScreen3_Conversation(); break;
    case 4: _drawScreen4_Stats(); break;
    case 5: _drawScreen5_VU(); break;
  }

  _drawNavDots();
}

// ==================== Pantalla 0: Estado ====================
void DisplayController::_drawScreen0_Status() {
  _drawHeader("ESTADO");

  // Icono animado del estado (centro)
  if (_animFrame != _prevAnimFrame || _currentState != _prevState) {
    _tft.fillRect(70, 60, 100, 100, CLR_BG);
    _drawStateIcon(120, 110, _currentState);
    _prevAnimFrame = _animFrame;

    // Nombre del estado
    _tft.fillRect(0, 175, 240, 30, CLR_BG);
    _drawCenteredText(_stateToString(_currentState), 190, 4, _stateToColor(_currentState));
  }

  // Info WiFi + heap (zona inferior)
  uint32_t heap = ESP.getFreeHeap();
  int32_t rssi = WiFi.RSSI();
  if (heap != _prevHeap || rssi != _prevRssi) {
    _tft.fillRect(0, 240, 240, 30, CLR_BG);
    _tft.setTextColor(CLR_DIM, CLR_BG);
    _tft.setTextDatum(MC_DATUM);
    char buf[40];
    snprintf(buf, sizeof(buf), "WiFi: %ddBm  Heap: %dKB", rssi, heap / 1024);
    _tft.drawString(buf, 120, 255, 2);
    _prevHeap = heap;
    _prevRssi = rssi;
  }
}

// ==================== Pantalla 1: Volumen ====================
void DisplayController::_drawScreen1_Volume() {
  _drawHeader("VOLUMEN");

  if (_volume != _prevVolume) {
    // Porcentaje grande
    _tft.fillRect(0, 70, 240, 70, CLR_BG);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _volume);
    _drawCenteredText(buf, 105, 7, CLR_ACCENT);

    // Barra de progreso
    float pct = _volume / 100.0f;
    uint16_t color = _volume > 80 ? CLR_RED : (_volume > 50 ? CLR_YELLOW : CLR_GREEN);
    _drawProgressBar(20, 170, 200, 20, pct, color);

    // Etiquetas
    _tft.fillRect(0, 200, 240, 20, CLR_BG);
    _tft.setTextColor(CLR_DIM, CLR_BG);
    _tft.setTextDatum(TL_DATUM);
    _tft.drawString("0", 20, 202, 2);
    _tft.setTextDatum(TR_DATUM);
    _tft.drawString("100", 220, 202, 2);

    _prevVolume = _volume;
  }
}

// ==================== Pantalla 2: WiFi ====================
void DisplayController::_drawScreen2_WiFi() {
  _drawHeader("WIFI");

  int32_t rssi = WiFi.RSSI();

  // SSID
  _tft.fillRect(0, 50, 240, 30, CLR_BG);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(WiFi.SSID(), 120, 65, 4);

  // Barras de senal
  _tft.fillRect(80, 95, 80, 70, CLR_BG);
  int bars = 0;
  if (rssi > -50) bars = 4;
  else if (rssi > -60) bars = 3;
  else if (rssi > -70) bars = 2;
  else if (rssi > -80) bars = 1;

  for (int i = 0; i < 4; i++) {
    int bh = 12 + i * 14;
    int bx = 90 + i * 18;
    int by = 165 - bh;
    uint16_t color = (i < bars) ? CLR_GREEN : CLR_DARK_GRAY;
    _tft.fillRect(bx, by, 12, bh, color);
  }

  // RSSI
  _tft.fillRect(0, 175, 240, 20, CLR_BG);
  char buf[20];
  snprintf(buf, sizeof(buf), "%d dBm", rssi);
  _drawCenteredText(buf, 185, 2, CLR_DIM);

  // IP
  _tft.fillRect(0, 215, 240, 30, CLR_BG);
  _tft.setTextColor(CLR_ACCENT, CLR_BG);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(WiFi.localIP().toString(), 120, 230, 4);

  _prevRssi = rssi;
}

// ==================== Pantalla 3: Conversacion ====================
void DisplayController::_drawScreen3_Conversation() {
  _drawHeader("CHAT");

  _tft.setTextDatum(TL_DATUM);
  _tft.setTextColor(CLR_ACCENT, CLR_BG);

  if (_lastQuestion.length() == 0) {
    _drawCenteredText("Sin conversacion", 150, 2, CLR_DIM);
    return;
  }

  // Pregunta
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Tu:", 10, 50, 2);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  // Wrap text (max ~28 chars por linea en font 2)
  int y = 68;
  String q = _lastQuestion;
  while (q.length() > 0 && y < 150) {
    int len = min((int)q.length(), 30);
    _tft.drawString(q.substring(0, len), 10, y, 2);
    q = q.substring(len);
    y += 16;
  }

  // Respuesta
  y += 8;
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Bot:", 10, y, 2);
  y += 18;
  _tft.setTextColor(CLR_GREEN, CLR_BG);
  String a = _lastAnswer;
  while (a.length() > 0 && y < 290) {
    int len = min((int)a.length(), 30);
    _tft.drawString(a.substring(0, len), 10, y, 2);
    a = a.substring(len);
    y += 16;
  }
}

// ==================== Pantalla 4: Estadisticas ====================
void DisplayController::_drawScreen4_Stats() {
  _drawHeader("STATS");

  _tft.setTextDatum(TL_DATUM);
  _tft.setTextColor(CLR_DIM, CLR_BG);

  int y = 55;
  int spacing = 35;
  char buf[40];

  // Conversaciones
  _tft.fillRect(0, y, 240, spacing, CLR_BG);
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Conversaciones:", 15, y, 2);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)_conversationCount);
  _tft.drawString(buf, 180, y, 2);
  y += spacing;

  // Latencia promedio
  _tft.fillRect(0, y, 240, spacing, CLR_BG);
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Latencia prom:", 15, y, 2);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  if (_latencyCount > 0)
    snprintf(buf, sizeof(buf), "%lums", (unsigned long)_avgLatency);
  else
    snprintf(buf, sizeof(buf), "--");
  _tft.drawString(buf, 180, y, 2);
  y += spacing;

  // Uptime
  _tft.fillRect(0, y, 240, spacing, CLR_BG);
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Uptime:", 15, y, 2);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  uint32_t secs = millis() / 1000;
  uint32_t mins = secs / 60;
  uint32_t hours = mins / 60;
  snprintf(buf, sizeof(buf), "%luh %lum", (unsigned long)hours, (unsigned long)(mins % 60));
  _tft.drawString(buf, 180, y, 2);
  y += spacing;

  // Free heap
  _tft.fillRect(0, y, 240, spacing, CLR_BG);
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Heap libre:", 15, y, 2);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  snprintf(buf, sizeof(buf), "%luKB", (unsigned long)(ESP.getFreeHeap() / 1024));
  _tft.drawString(buf, 180, y, 2);
  y += spacing;

  // Min heap
  _tft.fillRect(0, y, 240, spacing, CLR_BG);
  _tft.setTextColor(CLR_DIM, CLR_BG);
  _tft.drawString("Heap min:", 15, y, 2);
  _tft.setTextColor(CLR_TEXT, CLR_BG);
  snprintf(buf, sizeof(buf), "%luKB", (unsigned long)(ESP.getMinFreeHeap() / 1024));
  _tft.drawString(buf, 180, y, 2);
}

// ==================== Pantalla 5: VU Meter ====================
void DisplayController::_drawScreen5_VU() {
  _drawHeader("VU METER");

  if (_peakLevel == _prevPeakLevel) return;

  int barW = 60;
  int barH = 220;
  int barX = (DISPLAY_WIDTH - barW) / 2;
  int barY = 45;

  _tft.fillRect(barX, barY, barW, barH, CLR_DARK_GRAY);

  int fillH = (int)(_peakLevel * barH);
  if (fillH > 0) {
    int fillY = barY + barH - fillH;
    uint16_t color;
    if (_peakLevel > 0.8f) color = CLR_RED;
    else if (_peakLevel > 0.5f) color = CLR_YELLOW;
    else color = CLR_GREEN;
    _tft.fillRect(barX, fillY, barW, fillH, color);
  }

  _tft.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, CLR_DIM);

  _tft.fillRect(0, 275, 240, 20, CLR_BG);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", (int)(_peakLevel * 100));
  _drawCenteredText(buf, 285, 2, CLR_TEXT);

  int numBars = 12;
  for (int i = 0; i < numBars; i++) {
    float threshold = (float)i / numBars;
    int by = barY + barH - (int)((float)(i + 1) / numBars * barH);
    uint16_t c = (_peakLevel > threshold) ? CLR_GREEN : CLR_DARK_GRAY;
    if (threshold > 0.5f) c = (_peakLevel > threshold) ? CLR_YELLOW : CLR_DARK_GRAY;
    if (threshold > 0.8f) c = (_peakLevel > threshold) ? CLR_RED : CLR_DARK_GRAY;
    _tft.fillRect(15, by, 40, 14, c);
    _tft.fillRect(185, by, 40, 14, c);
  }

  _prevPeakLevel = _peakLevel;
}

// ==================== Helpers ====================

void DisplayController::_drawHeader(const char* title) {
  // Usar sprite para header sin flicker
  _spr.fillSprite(CLR_BG);
  _spr.setTextColor(CLR_ACCENT, CLR_BG);
  _spr.setTextDatum(MC_DATUM);
  _spr.drawString(title, DISPLAY_WIDTH / 2, 15, 4);
  _spr.pushSprite(0, 0);

  // Linea separadora
  _tft.drawFastHLine(10, 32, 220, CLR_DARK_GRAY);
}

void DisplayController::_drawNavDots() {
  int dotY = DISPLAY_HEIGHT - 10;  // 310 para 320
  int totalWidth = NUM_SCREENS * 12;
  int startX = (DISPLAY_WIDTH - totalWidth) / 2;

  _tft.fillRect(startX - 2, dotY - 4, totalWidth + 4, 10, CLR_BG);

  for (int i = 0; i < NUM_SCREENS; i++) {
    int cx = startX + i * 12 + 4;
    if (i == _currentScreen) {
      _tft.fillCircle(cx, dotY, 3, CLR_ACCENT);
    } else {
      _tft.fillCircle(cx, dotY, 2, CLR_DARK_GRAY);
    }
  }
}

void DisplayController::_drawProgressBar(int x, int y, int w, int h, float pct, uint16_t color) {
  _tft.fillRect(x, y, w, h, CLR_DARK_GRAY);
  int fillW = (int)(pct * w);
  if (fillW > 0) {
    _tft.fillRect(x, y, fillW, h, color);
  }
  _tft.drawRect(x - 1, y - 1, w + 2, h + 2, CLR_DIM);
}

void DisplayController::_drawCenteredText(const char* text, int y, uint8_t font, uint16_t color) {
  _tft.setTextColor(color, CLR_BG);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(text, DISPLAY_WIDTH / 2, y, font);
}

void DisplayController::_drawStateIcon(int cx, int cy, TalkbotState state) {
  uint16_t color = _stateToColor(state);

  switch (state) {
    case STATE_IDLE: {
      // Circulo pulsante
      int r = 25 + (_animFrame % 3) * 3;
      _tft.fillCircle(cx, cy, r, color);
      _tft.fillCircle(cx, cy, r - 6, CLR_BG);
      _tft.fillCircle(cx, cy, 8, color);
      break;
    }
    case STATE_LISTENING: {
      // Microfono animado (barras de audio)
      int heights[] = {15, 25, 35, 25, 15};
      int offsets[] = {2, -3, 5, -2, 3};
      for (int i = 0; i < 5; i++) {
        int h = heights[i] + offsets[(_animFrame + i) % 4];
        int bx = cx - 24 + i * 12;
        _tft.fillRect(bx, cy - h / 2, 8, h, color);
      }
      break;
    }
    case STATE_PROCESSING: {
      // Spinner (arcos rotando)
      int startAngle = _animFrame * 90;
      for (int a = 0; a < 270; a += 3) {
        float rad = (startAngle + a) * DEG_TO_RAD;
        int px = cx + (int)(28 * cos(rad));
        int py = cy + (int)(28 * sin(rad));
        _tft.fillCircle(px, py, 2, color);
      }
      break;
    }
    case STATE_SPEAKING: {
      // Ondas de sonido
      _tft.fillCircle(cx, cy, 10, color);
      for (int i = 1; i <= 3; i++) {
        int r = 10 + i * 10;
        uint16_t c = ((_animFrame + i) % 4 < 2) ? color : CLR_DARK_GRAY;
        _tft.drawCircle(cx, cy, r, c);
        _tft.drawCircle(cx, cy, r + 1, c);
      }
      break;
    }
    case STATE_ERROR: {
      // X parpadeante
      if (_animFrame % 2 == 0) {
        _tft.drawLine(cx - 20, cy - 20, cx + 20, cy + 20, CLR_RED);
        _tft.drawLine(cx - 20, cy + 20, cx + 20, cy - 20, CLR_RED);
        _tft.drawLine(cx - 19, cy - 20, cx + 21, cy + 20, CLR_RED);
        _tft.drawLine(cx - 19, cy + 20, cx + 21, cy - 20, CLR_RED);
      }
      break;
    }
    case STATE_WIFI_CONFIG: {
      // WiFi icono animado
      for (int i = 0; i < 3; i++) {
        int r = 12 + i * 10;
        uint16_t c = (i <= _animFrame % 4) ? CLR_YELLOW : CLR_DARK_GRAY;
        _tft.drawCircle(cx, cy + 15, r, c);
        _tft.drawCircle(cx, cy + 15, r + 1, c);
      }
      _tft.fillCircle(cx, cy + 15, 4, CLR_YELLOW);
      // Tapar mitad inferior
      _tft.fillRect(cx - 45, cy + 16, 90, 45, CLR_BG);
      break;
    }
  }
}

const char* DisplayController::_stateToString(TalkbotState state) {
  switch (state) {
    case STATE_IDLE:        return "LISTO";
    case STATE_LISTENING:   return "ESCUCHANDO";
    case STATE_PROCESSING:  return "PROCESANDO";
    case STATE_SPEAKING:    return "HABLANDO";
    case STATE_ERROR:       return "ERROR";
    case STATE_WIFI_CONFIG: return "CONFIG WIFI";
    default:                return "???";
  }
}

uint16_t DisplayController::_stateToColor(TalkbotState state) {
  switch (state) {
    case STATE_IDLE:        return CLR_ACCENT;
    case STATE_LISTENING:   return CLR_GREEN;
    case STATE_PROCESSING:  return CLR_YELLOW;
    case STATE_SPEAKING:    return CLR_BLUE;
    case STATE_ERROR:       return CLR_RED;
    case STATE_WIFI_CONFIG: return CLR_YELLOW;
    default:                return CLR_TEXT;
  }
}

#endif // ENABLE_DISPLAY
