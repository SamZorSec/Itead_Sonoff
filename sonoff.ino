/*
  Alternative firmware for Itead Sonoff switches, based on the MQTT protocol and a TLS connection
  The very initial version of this firmware was a fork from the SonoffBoilerplate (tzapu)

  This firmware can be easily interfaced with Home Assistant, with the MQTT switch
  component: https://home-assistant.io/components/switch.mqtt/

  CloudMQTT (free until 10 connections): https://www.cloudmqtt.com

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

  MQTT topics and payload:
    - State:    <Chip_ID>/switch/state      ON/OFF
    - Command:  <Chip_ID>/switch/switch     ON/OFF

  Configuration (Home Assistant) :
    switch:
      platform: mqtt
      name: 'Switch'
      state_topic: 'CBF777/switch/state'
      command_topic: 'CBF777/switch/switch'
      optimistic: false

  Versions:
    - 1.0: Initial version
    - 1.1: Add TLS support
    - 1.2: Add PIR sensor support

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  
  Samuel M. - v1.2 - 11.2016
  If you like this example, please add a star! Thank you!
  https://github.com/mertenats/sonoff
*/

#include <ESP8266WiFi.h>    // https://github.com/esp8266/Arduino
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/releases/tag/v2.6
#include <Ticker.h>
#include <EEPROM.h>
//#include <ArduinoOTA.h>

// TLS support, make sure to edit the fingerprint and the broker address if
// you are not using CloudMQTT
#define           TLS
#ifdef TLS
const char*       broker      = "m21.cloudmqtt.com";

// SHA1 fingerprint of the certificate
// openssl x509 -fingerprint -in  <certificate>.crt
const char*       fingerprint = "A5 02 FF 13 99 9F 8B 39 8E F1 83 4F 11 23 65 0B 32 36 FC 07";
#endif

// PIR motion sensor support, make sure to connect a PIR motion sensor to the GPIO14
//#define           PIR
#ifdef PIR
const uint8_t     PIR_SENSOR_PIN = 14;
#endif

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
  char            mqttStateTopic[STRUCT_CHAR_ARRAY_SIZE]            = "";
  char            mqttCommandTopic[STRUCT_CHAR_ARRAY_SIZE]          = "";
} Settings;

enum CMD {
  CMD_NOT_DEFINED,
  CMD_PIR_STATE_CHANGED,
  CMD_BUTTON_STATE_CHANGED,
};
volatile uint8_t cmd = CMD_NOT_DEFINED;

uint8_t           relayState                                        = HIGH;  // HIGH: closed switch
uint8_t           buttonState                                       = HIGH; // HIGH: opened switch
uint8_t           currentButtonState                                = buttonState;
long              buttonStartPressed                                = 0;
long              buttonDurationPressed                             = 0;
#ifdef PIR
uint8_t           pirState                                          = LOW;
uint8_t           currentPirState                                   = pirState;
#endif

Settings          settings;
Ticker            ticker;
#ifdef TLS
WiFiClientSecure  wifiClient;
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
  if (String(settings.mqttCommandTopic).equals(p_topic)) {
  //if (String(MQTT_SWITCH_COMMAND_TOPIC).equals(p_topic)) {
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
    if (mqttClient.publish(settings.mqttStateTopic, MQTT_SWITCH_ON_PAYLOAD, true)) {
//    if (mqttClient.publish(MQTT_SWITCH_STATE_TOPIC, MQTT_SWITCH_ON_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(settings.mqttStateTopic);
      //DEBUG_PRINT(MQTT_SWITCH_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_SWITCH_ON_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  } else {
    if (mqttClient.publish(settings.mqttStateTopic, MQTT_SWITCH_OFF_PAYLOAD, true)) {
    //if (mqttClient.publish(MQTT_SWITCH_STATE_TOPIC, MQTT_SWITCH_OFF_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      //DEBUG_PRINT(MQTT_SWITCH_STATE_TOPIC);
      DEBUG_PRINT(settings.mqttStateTopic);
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
  // test if the module has an IP address
  // if not, restart the module
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN(F("ERROR: The module isn't connected to the internet"));
    restart();
  }

  // try to connect to the MQTT broker
  // if the connection is not possible, it will reset the settings
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

  if (mqttClient.subscribe(settings.mqttCommandTopic)) {
  //if (mqttClient.subscribe(MQTT_SWITCH_COMMAND_TOPIC)) {
    DEBUG_PRINT(F("INFO: Sending the MQTT subscribe succeeded. Topic: "));
    DEBUG_PRINTLN(settings.mqttCommandTopic);
    //DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);
  } else {
    DEBUG_PRINT(F("ERROR: Sending the MQTT subscribe failed. Topic: "));
    DEBUG_PRINTLN(settings.mqttCommandTopic);
    //DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);
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
//   ISR
///////////////////////////////////////////////////////////////////////////
/*
  Function called when the button is pressed/released
 */
void buttonStateChangedISR() {
  cmd = CMD_BUTTON_STATE_CHANGED;
}

/*
  Function called when the PIR sensor detects the biginning/end of a mouvement
 */
 #ifdef PIR
void pirStateChangedISR() {
  cmd = CMD_PIR_STATE_CHANGED;
}
#endif

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
  attachInterrupt(BUTTON_PIN, buttonStateChangedISR, CHANGE);
#ifdef PIR
  pinMode(PIR_SENSOR_PIN, INPUT);
  attachInterrupt(PIR_SENSOR_PIN, pirStateChangedISR, CHANGE);
#endif
  ticker.attach(0.6, tick);

  // get the Chip ID of the switch and use it as the MQTT client ID
  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT client ID/Hostname: "));
  DEBUG_PRINTLN(MQTT_CLIENT_ID);

  // set the state topic: <Chip ID>/switch/state
  sprintf(MQTT_SWITCH_STATE_TOPIC, "%06X/switch/state", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT state topic: "));
  DEBUG_PRINTLN(MQTT_SWITCH_STATE_TOPIC);

  // set the command topic: <Chip ID>/switch/switch
  sprintf(MQTT_SWITCH_COMMAND_TOPIC, "%06X/switch/switch", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT command topic: "));
  DEBUG_PRINTLN(MQTT_SWITCH_COMMAND_TOPIC);

  // load custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

#ifdef TLS
  WiFiManagerParameter custom_text("<p>MQTT username, password and broker port</p>");
  WiFiManagerParameter custom_mqtt_server("mqtt-server", "MQTT Broker IP", "m21.cloudmqtt.com", STRUCT_CHAR_ARRAY_SIZE, "disabled");
#else
  WiFiManagerParameter custom_text("<p>MQTT username, password, broker IP address and broker port</p>");
  WiFiManagerParameter custom_mqtt_server("mqtt-server", "MQTT Broker IP", settings.mqttServer, STRUCT_CHAR_ARRAY_SIZE);
#endif
  WiFiManagerParameter custom_mqtt_user("mqtt-user", "MQTT User", settings.mqttUser, STRUCT_CHAR_ARRAY_SIZE);
  WiFiManagerParameter custom_mqtt_password("mqtt-password", "MQTT Password", settings.mqttPassword, STRUCT_CHAR_ARRAY_SIZE, "type = \"password\"");
  WiFiManagerParameter custom_mqtt_port("mqtt-port", "MQTT Broker Port", settings.mqttPort, 6);

  WiFiManagerParameter custom_mqtt_topics("<p>MQTT state and command topics</p>");
  WiFiManagerParameter custom_mqtt_state_topic("mqtt-state-topic", "MQTT State Topic", MQTT_SWITCH_STATE_TOPIC, STRUCT_CHAR_ARRAY_SIZE);
  WiFiManagerParameter custom_mqtt_command_topic("mqtt-command-topic", "MQTT Command Topic", MQTT_SWITCH_COMMAND_TOPIC, STRUCT_CHAR_ARRAY_SIZE);

  WiFiManager wifiManager;

  wifiManager.addParameter(&custom_text);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  wifiManager.addParameter(&custom_mqtt_topics);
  wifiManager.addParameter(&custom_mqtt_state_topic);
  wifiManager.addParameter(&custom_mqtt_command_topic);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);
  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(MQTT_CLIENT_ID)) {
    reset();
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

    strcpy(settings.mqttStateTopic, custom_mqtt_state_topic.getValue());
    strcpy(settings.mqttCommandTopic, custom_mqtt_command_topic.getValue());

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

  //ArduinoOTA.setHostname(MQTT_CLIENT_ID);
  //ArduinoOTA.begin();

  ticker.detach();

  setRelayState();
}


void loop() {
  //ArduinoOTA.handle();

  //yield();

  switch (cmd) {
    case CMD_NOT_DEFINED:
      // do nothing
      break;
#ifdef PIR
    case CMD_PIR_STATE_CHANGED:
      currentPirState = digitalRead(PIR_SENSOR_PIN);
      if (pirState != currentPirState) {
        if (pirState == LOW && currentPirState == HIGH) {
          if (relayState != HIGH) {
            relayState = HIGH; // closed
            setRelayState();
          }
        } else if (pirState == HIGH && currentPirState == LOW) {
          if (relayState != LOW) {
            relayState = LOW; // opened
            setRelayState();
          }
        }
        pirState = currentPirState;
      }
      cmd = CMD_NOT_DEFINED;
      break;
#endif
    case CMD_BUTTON_STATE_CHANGED:
      currentButtonState = digitalRead(BUTTON_PIN);
      if (buttonState != currentButtonState) {
        // tests if the button is released or pressed
        if (buttonState == LOW && currentButtonState == HIGH) {
          buttonDurationPressed = millis() - buttonStartPressed;
          if (buttonDurationPressed < 500) {
            relayState = relayState == HIGH ? LOW : HIGH;
            setRelayState();
          } else if (buttonDurationPressed < 3000) {
            restart();
          } else {
            reset();
          }
        } else if (buttonState == HIGH && currentButtonState == LOW) {
          buttonStartPressed = millis();
        }
        buttonState = currentButtonState;
      }
      cmd = CMD_NOT_DEFINED;
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
