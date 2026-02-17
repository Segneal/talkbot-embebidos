#include "led_controller.h"

void LedController::begin() {
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  allOff();
}

void LedController::setEnabled(bool enabled) {
  _enabled = enabled;
  if (!_enabled) {
    allOff();
  } else {
    setState(_currentState);
  }
}

void LedController::setState(TalkbotState state) {
  _currentState = state;
  allOff();

  if (!_enabled) return;

  switch (state) {
    case STATE_IDLE:
      Serial.println("[LED] IDLE - todos off");
      break;
    case STATE_LISTENING:
      Serial.printf("[LED] LISTENING - pin %d HIGH\n", _listeningPin);
      digitalWrite(_listeningPin, HIGH);
      break;
    case STATE_PROCESSING:
      Serial.printf("[LED] PROCESSING - pin %d HIGH\n", _processingPin);
      digitalWrite(_processingPin, HIGH);
      break;
    case STATE_SPEAKING:
      Serial.printf("[LED] SPEAKING - pin %d HIGH\n", _speakingPin);
      digitalWrite(_speakingPin, HIGH);
      break;
    case STATE_ERROR:
      Serial.printf("[LED] ERROR - pin %d BLINK\n", LED_RED_PIN);
      _blinkTimer = millis();
      _blinkOn = true;
      digitalWrite(LED_RED_PIN, HIGH);
      break;
    case STATE_WIFI_CONFIG:
      Serial.println("[LED] WIFI_CONFIG - yellow/green alternating");
      _blinkTimer = millis();
      _blinkOn = true;
      digitalWrite(LED_YELLOW_PIN, HIGH);
      break;
  }
}

void LedController::allOff() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
}

void LedController::update() {
  if (!_enabled) return;
  if (_currentState == STATE_ERROR) {
    if (millis() - _blinkTimer >= 300) {
      _blinkTimer = millis();
      _blinkOn = !_blinkOn;
      digitalWrite(LED_RED_PIN, _blinkOn ? HIGH : LOW);
    }
  } else if (_currentState == STATE_WIFI_CONFIG) {
    if (millis() - _blinkTimer >= 400) {
      _blinkTimer = millis();
      _blinkOn = !_blinkOn;
      // Amarillo y verde alternan
      digitalWrite(LED_YELLOW_PIN, _blinkOn ? HIGH : LOW);
      digitalWrite(LED_GREEN_PIN, _blinkOn ? LOW : HIGH);
    }
  }
}

void LedController::setListeningLed(uint8_t pin) {
  _listeningPin = pin;
}

void LedController::setSpeakingLed(uint8_t pin) {
  _speakingPin = pin;
}

void LedController::setProcessingLed(uint8_t pin) {
  _processingPin = pin;
}
