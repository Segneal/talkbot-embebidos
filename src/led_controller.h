#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

class LedController {
public:
  void begin();
  void setState(TalkbotState state);
  void allOff();
  void update();  // Llamar en loop() para manejar parpadeo
  bool isEnabled() { return _enabled; }
  void setEnabled(bool enabled);

  // Configuración de colores por estado (para web app)
  void setListeningLed(uint8_t pin);
  void setSpeakingLed(uint8_t pin);
  void setProcessingLed(uint8_t pin);

  uint8_t getListeningLed() { return _listeningPin; }
  uint8_t getSpeakingLed()  { return _speakingPin; }
  uint8_t getProcessingLed(){ return _processingPin; }

private:
  uint8_t _listeningPin  = LED_GREEN_PIN;
  uint8_t _speakingPin   = LED_RED_PIN;
  uint8_t _processingPin = LED_YELLOW_PIN;

  TalkbotState _currentState = STATE_IDLE;
  unsigned long _blinkTimer = 0;
  bool _blinkOn = false;
  bool _enabled = true;
};

#endif
