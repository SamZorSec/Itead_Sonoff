#pragma once
#ifndef _SONOFF_
#define _SONOFF_

#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#include "config.h"
#if defined(DS18B20_SENSOR)
#include <OneWire.h>            // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h>  // https://github.com/milesburton/Arduino-Temperature-Control-Library
#endif

#define SONOFF_TH_BUTTON  0                          
#define SONOFF_TH_RELAY   12                              
#define SONOFF_TH_LED     13  
#define SONOFF_TH_JACK    14

#define SONOFF_TH_LED_ON  LOW
#define SONOFF_TH_LED_OFF HIGH 
#define SONOFF_TH_RELAY_ON  HIGH 
#define SONOFF_TH_RELAY_OFF LOW 

enum CMD {
  CMD_NOT_DEFINED,
  CMD_STATE_CHANGED,
  CMD_BUTTON_STATE_CHANGED,
  CMD_SAVE_STATE
};
extern volatile uint8_t cmd;

void buttonStateChangedISR();

class Sonoff {
  public:
    Sonoff(void);

    void  init(void);

    bool  getState(void);
    bool  setState(bool p_state);

    bool  isDiscovered(void);
    void  isDiscovered(bool p_isDiscovered);

    float getTemperature(void);
  private:
    bool  m_state = false;
    bool  m_isDiscovered = false;
};

#endif
