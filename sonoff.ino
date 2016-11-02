/* 
  Alternative firmware for Sonoff switches, based on the MQTT protocol
  The very initial version of this firmware was a fork from the SonoffBoilerplate (tzapu)

  This firmware can be easily interfaced with Home Assistant, with the MQTT switch 
  component: https://home-assistant.io/components/switch.mqtt/
  
  Libraries :
    - ESP8266 core for Arduino :  https://github.com/esp8266/Arduino
    - PubSubClient:               https://github.com/knolleary/pubsubclient
    - WiFiManager:                https://github.com/tzapu/WiFiManager
  
  Sources :
    - File > Examples > ES8266WiFi > WiFiClient
    - File > Examples > PubSubClient > mqtt_auth
    - https://github.com/tzapu/SonoffBoilerplate
    - https://io.adafruit.com/blog/security/2016/07/05/adafruit-io-security-esp8266/

  Schematic:
    - VCC (Sonoff) -> VCC (FTDI)
    - RX  (Sonoff) -> TX  (FTDI)
    - TX  (Sonoff) -> RX  (FTDI)
    - GND (Sonoff) -> GND (FTDI)

  Steps:
    - Upload the firmware
    - Connect to the new Wi-Fi AP and memorize its name 
    - Choose your network and enter your MQTT username, password, broker 
      IP address and broker port
    - Update your configuration in Home Assistant

  Configuration (Home Assistant) : 
    switch:
      platform: mqtt
      name: 'Switch'
      state_topic: 'CBF777/switch/state'
      command_topic: 'CBF777/switch/switch'
      optimistic: false

  Versions:
    - 1.0: Initial version
    - 1.1: Add TLS support for CloudMQTT (or any other MQTT brokers)

  Samuel M. - v1.1 - 11.2016
  If you like this example, please add a star! Thank you!
  https://github.com/mertenats/sonoff
*/

#include <ESP8266WiFi.h>    // https://github.com/esp8266/Arduino
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/releases/tag/v2.6
#include <Ticker.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

// TLS support, make sure to edit the fingerprint and the broker address if
// your are not using CloudMQTT
#define           TLS
#define           DEBUG                       // enable debugging
#define           STRUCT_CHAR_ARRAY_SIZE 24   // size of the arrays for MQTT username, password, etc.

// macros for debugging
#ifdef DEBUG
  #define         DEBUG_PRINT(x)    Serial.print(x)
  #define         DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define         DEBUG_PRINT(x)
  #define         DEBUG_PRINTLN(x)
#endif

// Sonoff properties
const uint8_t     BUTTON_PIN = 0;
const uint8_t     RELAY_PIN  = 12;
const uint8_t     LED_PIN    = 13;

// MQTT
char              MQTT_CLIENT_ID[7]                                 = {0};
char              MQTT_SWITCH_STATE_TOPIC[STRUCT_CHAR_ARRAY_SIZE]   = {0};
char              MQTT_SWITCH_COMMAND_TOPIC[STRUCT_CHAR_ARRAY_SIZE] = {0};
const char*       MQTT_SWITCH_ON_PAYLOAD                            = "ON";
const char*       MQTT_SWITCH_OFF_PAYLOAD                           = "OFF";

// Settings for MQTT
typedef struct {
  char            mqttUser[STRUCT_CHAR_ARRAY_SIZE]                  = "";//{0};
  char            mqttPassword[STRUCT_CHAR_ARRAY_SIZE]              = "";//{0};
  char            mqttServer[STRUCT_CHAR_ARRAY_SIZE]                = "";//{0};
  char            mqttPort[6]                                       = "";//{0};
} Settings;

const uint8_t     CMD_BUTTON_NOT_PRESSED                            = 0;
const uint8_t     CMD_BUTTON_CHANGE                                 = 1;
volatile uint8_t  cmd                                               = CMD_BUTTON_NOT_PRESSED;

uint8_t           relayState                                        = HIGH;  // HIGH: closed switch
uint8_t           buttonState                                       = HIGH; // HIGH: opened switch
volatile long     startPress                                        = 0;

Settings          settings;
Ticker            ticker;
#ifdef TLS
WiFiClientSecure  wifiClient;
const char*       broker      = "m21.cloudmqtt.com"; 
// SHA1 fingerprint of the certificate
const char*       fingerprint = "A5 02 FF 13 99 9F 8B 39 8E F1 83 4F 11 23 65 0B 32 36 FC 07";
#else
WiFiClient        wifiClient;
#endif
PubSubClient      mqttClient(wifiClient);

///////////////////////////////////////////////////////////////////////////
//   Adafruit IO with SSL/TLS
///////////////////////////////////////////////////////////////////////////
/*
  Function called to verify the fingerprint of the MQTT server certificate
 */
#ifdef TLS
void verifyFingerprint() {
  DEBUG_PRINT(F("INFO: Connecting to "));
  DEBUG_PRINTLN(settings.mqttServer);

  if (!wifiClient.connect(settings.mqttServer, atoi(settings.mqttPort))) {
    DEBUG_PRINTLN(F("ERROR: Connection failed. Halting execution"));
    reset();
  }

  if (wifiClient.verify(fingerprint, settings.mqttServer)) {
    DEBUG_PRINTLN(F("INFO: Connection secure"));
  } else {
    DEBUG_PRINTLN(F("ERROR: Connection insecure! Halting execution"));
    reset();
  }
}
#endif

///////////////////////////////////////////////////////////////////////////
//   MQTT
///////////////////////////////////////////////////////////////////////////
/*
   Function called when a MQTT message arrived
   @param p_topic   The topic of the MQTT message
   @param p_payload The payload of the MQTT message
   @param p_length  The length of the payload
*/
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // handle the MQTT topic of the received message
  if (String(MQTT_SWITCH_COMMAND_TOPIC).equals(p_topic)) {
    if ((char)p_payload[0] == (char)MQTT_SWITCH_ON_PAYLOAD[0] && (char)p_payload[1] == (char)MQTT_SWITCH_ON_PAYLOAD[1]) {
      if (relayState != HIGH) {
        relayState = HIGH;
        setRelayState();
      }
    } else if ((char)p_payload[0] == (char)MQTT_SWITCH_OFF_PAYLOAD[0] && (char)p_payload[1] == (char)MQTT_SWITCH_OFF_PAYLOAD[1] && (char)p_payload[2] == (char)MQTT_SWITCH_OFF_PAYLOAD[2]) {
      if (relayState != LOW) {
        relayState = LOW;
        setRelayState();
      }
    }
  }
}

/*
  Function called to publish the state of the Sonoff relay
*/
void publishSwitchState() {
  if (relayState == HIGH) {
    if (mqttClient.publish(MQTT_SWITCH_STATE_TOPIC, MQTT_SWITCH_ON_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(MQTT_SWITCH_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_SWITCH_ON_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  } else {
    if (mqttClient.publish(MQTT_SWITCH_STATE_TOPIC, MQTT_SWITCH_OFF_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(MQTT_SWITCH_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_SWITCH_OFF_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  }
}

/*
  Function called to connect/reconnect to the MQTT broker
 */
void reconnect() {
  uint8_t i = 0;
  while (!mqttClient.connected()) {
    if (mqttClient.connect(MQTT_CLIENT_ID, settings.mqttUser, settings.mqttPassword)) {
      DEBUG_PRINTLN(F("INFO: The client is successfully connected to the MQTT broker"));
    } else {
      DEBUG_PRINTLN(F("ERROR: The connection to the MQTT broker failed"));
      DEBUG_PRINT(F("Username: "));
      DEBUG_PRINTLN(settings.mqttUser);
      DEBUG_PRINT(F("Password: "));
      DEBUG_PRINTLN(settings.mqttPassword);
      DEBUG_PRINT(F("Broker: "));
      DEBUG_PRINTLN(settings.mqttServer);
      delay(1000);
      if (i == 3) {
        reset();
      }
      i++;
    }
  }

  if (mqttClient.subscribe(MQTT_SWITCH_COMMAND_TOPIC)) {
    DEBUG_PRINT(F("INFO: Sending the MQTT subscribe succeeded. Topic: "));
    DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);
  } else {
    DEBUG_PRINT(F("ERROR: Sending the MQTT subscribe failed. Topic: "));
    DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);
  }
}

///////////////////////////////////////////////////////////////////////////
//   WiFiManager
///////////////////////////////////////////////////////////////////////////
/*
  Function called to toggle the state of the LED
 */
void tick() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

// flag for saving data
bool shouldSaveConfig = false;

// callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  ticker.attach(0.2, tick);
}

///////////////////////////////////////////////////////////////////////////
//   Sonoff switch
///////////////////////////////////////////////////////////////////////////
/*
 Function called to set the state of the relay
 */
void setRelayState() {
  digitalWrite(RELAY_PIN, relayState);
  digitalWrite(LED_PIN, (relayState + 1) % 2);
  publishSwitchState();
}

/*
  Function called when the button is pressed/released
 */
void toggleButtonISR() {
  cmd = CMD_BUTTON_CHANGE;
}

/*
  Function called to restart the switch
 */
void restart() {
  DEBUG_PRINTLN(F("INFO: Restart..."));
  ESP.reset();
  delay(1000);
}

/*
  Function called to reset the configuration of the switch
 */
void reset() {
  DEBUG_PRINTLN(F("INFO: Reset..."));
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}

///////////////////////////////////////////////////////////////////////////
//   Setup() and loop()
///////////////////////////////////////////////////////////////////////////
void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif

  // init the I/O
  pinMode(LED_PIN,    OUTPUT);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(BUTTON_PIN, toggleButtonISR, CHANGE);

  ticker.attach(0.6, tick);

  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT client ID/Hostname: "));
  DEBUG_PRINTLN(MQTT_CLIENT_ID);

  sprintf(MQTT_SWITCH_STATE_TOPIC, "%06X/switch/state", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT state topic: "));
  DEBUG_PRINTLN(MQTT_SWITCH_STATE_TOPIC);

  sprintf(MQTT_SWITCH_COMMAND_TOPIC, "%06X/switch/switch", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT command topic: "));
  DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);

  // load custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();
  
  WiFiManagerParameter custom_mqtt_user("mqtt-user", "MQTT User", settings.mqttUser, STRUCT_CHAR_ARRAY_SIZE);
  WiFiManagerParameter custom_mqtt_password("mqtt-password", "MQTT Password", settings.mqttPassword, STRUCT_CHAR_ARRAY_SIZE, "type = \"password\"");
#ifdef TLS
  WiFiManagerParameter custom_mqtt_server("mqtt-server", "MQTT Broker IP", "m21.cloudmqtt.com", STRUCT_CHAR_ARRAY_SIZE, "disabled");
#else
  WiFiManagerParameter custom_mqtt_server("mqtt-server", "MQTT Broker IP", settings.mqttServer, STRUCT_CHAR_ARRAY_SIZE);
#endif
  WiFiManagerParameter custom_mqtt_port("mqtt-port", "MQTT Broker Port", settings.mqttPort, 6);

  WiFiManager wifiManager;
  
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);
  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(MQTT_CLIENT_ID)) {
    ESP.reset();
    delay(1000);
  }

  if (shouldSaveConfig) {
#ifdef TLS
    strcpy(settings.mqttServer,   broker);
#else
    strcpy(settings.mqttServer,   custom_mqtt_server.getValue());
#endif
    strcpy(settings.mqttUser,     custom_mqtt_user.getValue());
    strcpy(settings.mqttPassword, custom_mqtt_password.getValue());
    strcpy(settings.mqttPort,     custom_mqtt_port.getValue());

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

#ifdef TLS
  // check the fingerprint of io.adafruit.com's SSL cert
  verifyFingerprint();
#endif

  // configure MQTT
  mqttClient.setServer(settings.mqttServer, atoi(settings.mqttPort));
  mqttClient.setCallback(callback);

  // connect to the MQTT broker
  reconnect();
  
  ArduinoOTA.setHostname(MQTT_CLIENT_ID);
  ArduinoOTA.begin();

  ticker.detach();

  setRelayState();
}


void loop() {
  ArduinoOTA.handle();

  yield();

  switch (cmd) {
    case CMD_BUTTON_NOT_PRESSED:
      // do nothing
      break;
    case CMD_BUTTON_CHANGE:
      cmd = CMD_BUTTON_NOT_PRESSED;
      uint8_t currentState = digitalRead(BUTTON_PIN);
      if (currentState != buttonState) {
        if (buttonState == LOW && currentState == HIGH) {
          long duration = millis() - startPress;
          if (duration < 1000) {
            relayState = relayState == HIGH ? LOW : HIGH;
            setRelayState();
          } else if (duration < 3000) {
            restart();
          } else if (duration > 5000) {
            reset();
          }
        } else if (buttonState == HIGH && currentState == LOW) {
          startPress = millis();
        }
        buttonState = currentState;
      }
      break;
  }

  yield();
  
  // keep the MQTT client connected to the broker
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();

  yield();
}
