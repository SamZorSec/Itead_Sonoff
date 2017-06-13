#include "Sonoff.h"

///////////////////////////////////////////////////////////////////////////
//   CONSTRUCTOR, INIT() AND LOOP()
///////////////////////////////////////////////////////////////////////////
Sonoff::Sonoff(void) {
  pinMode(SONOFF_TH_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SONOFF_TH_BUTTON), buttonStateChangedISR, RISING);
  pinMode(SONOFF_TH_RELAY, OUTPUT);
  pinMode(SONOFF_TH_LED, OUTPUT);
}

void Sonoff::init(void) {
  digitalWrite(SONOFF_TH_RELAY, SONOFF_TH_RELAY_OFF);
  setState(false);
}

///////////////////////////////////////////////////////////////////////////
//   STATE
///////////////////////////////////////////////////////////////////////////
bool Sonoff::getState(void) {
  return m_state;
}

bool Sonoff::setState(bool p_state) {
  if (p_state != m_state) {
    m_state = p_state;
    if (m_state)
      digitalWrite(SONOFF_TH_RELAY, SONOFF_TH_RELAY_ON);
    else
      digitalWrite(SONOFF_TH_RELAY, SONOFF_TH_RELAY_OFF);
  } else {
    return false;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////
//   MQTT DISCOVERY
///////////////////////////////////////////////////////////////////////////
bool Sonoff::isDiscovered(void) {
  return m_isDiscovered;
}

void Sonoff::isDiscovered(bool p_isDiscovered) {
  m_isDiscovered = p_isDiscovered;
}

///////////////////////////////////////////////////////////////////////////
//   ISR
///////////////////////////////////////////////////////////////////////////
/*
  Function called when the button is pressed/released
*/
void buttonStateChangedISR(void) {
  cmd = CMD_BUTTON_STATE_CHANGED;
}
