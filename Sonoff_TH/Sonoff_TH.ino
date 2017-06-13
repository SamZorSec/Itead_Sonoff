/*
  Alternative firmware for Itead Sonoff TH switches, based on ESP8266.
  See the README at https://github.com/mertenats/Itead_Sonoff for more information.
  Licensed under the MIT license.

  If you like the content of this repo, please add a star! Thank you!

  Samuel Mertenat
  06.2017
*/

#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient
#include <ArduinoJson.h>  // https://github.com/bblanchon/ArduinoJson
#include <ArduinoOTA.h>
#include "FS.h"
#include "Sonoff.h"

#if defined(DEBUG_TELNET)
WiFiServer  telnetServer(DEBUG_TELNET_PORT);
WiFiClient  telnetClient;
#define     DEBUG_PRINT(x)    telnetClient.print(x)
#define     DEBUG_PRINTLN(x)  telnetClient.println(x)

#elif defined(DEBUG_SERIAL)
#define     DEBUG_PRINT(x)    Serial.print(x)
#define     DEBUG_PRINTLN(x)  Serial.println(x)
#else
#define     DEBUG_PRINT(x)
#define     DEBUG_PRINTLN(x)
#endif

StaticJsonBuffer<96> staticJsonBuffer;
char jsonBuffer[96] = {0};

volatile uint8_t cmd = CMD_STATE_CHANGED;

Sonoff sonoff;
#if defined(TLS)
WiFiClientSecure  wifiClient;
#else
WiFiClient        wifiClient;
#endif
PubSubClient      mqttClient(wifiClient);

///////////////////////////////////////////////////////////////////////////
//   TELNET
///////////////////////////////////////////////////////////////////////////
/*
   Function called to handle Telnet clients
   https://www.youtube.com/watch?v=j9yW10OcahI
*/
#if defined(DEBUG_TELNET)
void handleTelnet(void) {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) {
        telnetClient.stop();
      }
      telnetClient = telnetServer.available();
    } else {
      telnetServer.available().stop();
    }
  }
}
#endif


///////////////////////////////////////////////////////////////////////////
//  TLS
///////////////////////////////////////////////////////////////////////////
/*
  Function called to verify the fingerprint of the MQTT server certificate
*/
#ifdef TLS
void verifyFingerprint() {
  DEBUG_PRINT(F("INFO: Connecting to "));
  DEBUG_PRINTLN(MQTT_SERVER);

  if (!wifiClient.connect(MQTT_SERVER, MQTT_SERVER_PORT)) {
    DEBUG_PRINTLN(F("ERROR: Connection failed. Halting execution"));
    delay(1000);
    ESP.reset();
    /*
       TODO: Doing something smarter than rebooting the device
    */
  }

  if (wifiClient.verify(TLS_FINGERPRINT, MQTT_SERVER)) {
    DEBUG_PRINTLN(F("INFO: Connection secure"));
  } else {
    DEBUG_PRINTLN(F("ERROR: Connection insecure! Halting execution"));
    delay(1000);
    ESP.reset();
    /*
       TODO: Doing something smarter than rebooting the device
    */
  }
}
#endif

///////////////////////////////////////////////////////////////////////////
//   SAVE/LOAD SAVED CONFIGURATION
///////////////////////////////////////////////////////////////////////////
#if defined(SAVE_STATE)
bool loadConfig() {
  if (!SPIFFS.exists("/config.json")) {
    sonoff.init();
  } else {
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
      DEBUG_PRINTLN(F("ERROR: Failed to open config file"));
      return false;
    }

    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);

    configFile.readBytes(buf.get(), size);

    DynamicJsonBuffer dynamicJsonBuffer;
    JsonObject& root = dynamicJsonBuffer.parseObject(buf.get());

    if (!root.success()) {
      DEBUG_PRINTLN(F("ERROR: parseObject() failed"));
      return false;
    }

    bool isDiscovered = root["isDiscovered"];
    sonoff.isDiscovered(isDiscovered);

    if (strcmp(root["state"], MQTT_STATE_ON_PAYLOAD) == 0) {
      sonoff.setState(true);
    } else if (strcmp(root["state"], MQTT_STATE_OFF_PAYLOAD) == 0) {
      sonoff.setState(false);
    }
    
    return true;
  }

}

bool saveConfig() {
  DynamicJsonBuffer dynamicJsonBuffer;
  JsonObject& root = dynamicJsonBuffer.createObject();
  root["state"] = sonoff.getState() ? MQTT_STATE_ON_PAYLOAD : MQTT_STATE_OFF_PAYLOAD;
  root["isDiscovered"] = sonoff.isDiscovered();

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    DEBUG_PRINTLN(F("ERROR: Failed to open config file for writing"));
    return false;
  }

  root.printTo(configFile);
  return true;
}
#endif

///////////////////////////////////////////////////////////////////////////
//   WiFi
///////////////////////////////////////////////////////////////////////////
/*
   Function called to handle WiFi events
*/
void handleWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STAMODE_GOT_IP:
      DEBUG_PRINTLN(F("INFO: WiFi connected"));
      DEBUG_PRINT(F("INFO: IP address: "));
      DEBUG_PRINTLN(WiFi.localIP());
      break;
    case WIFI_EVENT_STAMODE_DISCONNECTED:
      DEBUG_PRINTLN(F("ERROR: WiFi losts connection"));
      /*
         TODO: Doing something smarter than rebooting the device
      */
      delay(5000);
      ESP.restart();
      break;
    default:
      DEBUG_PRINT(F("INFO: WiFi event: "));
      DEBUG_PRINTLN(event);
      break;
  }
}

/*
   Function called to setup the connection to the WiFi AP
*/
void setupWiFi() {
  DEBUG_PRINT(F("INFO: WiFi connecting to: "));
  DEBUG_PRINTLN(WIFI_SSID);

  delay(10);

  WiFi.mode(WIFI_STA);
  WiFi.onEvent(handleWiFiEvent);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  randomSeed(micros());
}

///////////////////////////////////////////////////////////////////////////
//   OTA
///////////////////////////////////////////////////////////////////////////
#if defined(OTA)
/*
   Function called to setup OTA updates
*/
void setupOTA() {
#if defined(OTA_HOSTNAME)
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  DEBUG_PRINT(F("INFO: OTA hostname sets to: "));
  DEBUG_PRINTLN(OTA_HOSTNAME);
#endif

#if defined(OTA_PORT)
  ArduinoOTA.setPort(OTA_PORT);
  DEBUG_PRINT(F("INFO: OTA port sets to: "));
  DEBUG_PRINTLN(OTA_PORT);
#endif

#if defined(OTA_PASSWORD)
  ArduinoOTA.setPassword((const char *)OTA_PASSWORD);
  DEBUG_PRINT(F("INFO: OTA password sets to: "));
  DEBUG_PRINTLN(OTA_PASSWORD);
#endif

  ArduinoOTA.onStart([]() {
    DEBUG_PRINTLN(F("INFO: OTA starts"));
  });
  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN(F("INFO: OTA ends"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINT(F("INFO: OTA progresses: "));
    DEBUG_PRINT(progress / (total / 100));
    DEBUG_PRINTLN(F("%"));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DEBUG_PRINT(F("ERROR: OTA error: "));
    DEBUG_PRINTLN(error);
    if (error == OTA_AUTH_ERROR)
      DEBUG_PRINTLN(F("ERROR: OTA auth failed"));
    else if (error == OTA_BEGIN_ERROR)
      DEBUG_PRINTLN(F("ERROR: OTA begin failed"));
    else if (error == OTA_CONNECT_ERROR)
      DEBUG_PRINTLN(F("ERROR: OTA connect failed"));
    else if (error == OTA_RECEIVE_ERROR)
      DEBUG_PRINTLN(F("ERROR: OTA receive failed"));
    else if (error == OTA_END_ERROR)
      DEBUG_PRINTLN(F("ERROR: OTA end failed"));
  });
  ArduinoOTA.begin();
}

/*
   Function called to handle OTA updates
*/
void handleOTA() {
  ArduinoOTA.handle();
}
#endif

///////////////////////////////////////////////////////////////////////////
//   MQTT
///////////////////////////////////////////////////////////////////////////

char MQTT_CLIENT_ID[7] = {0};
#if defined(MQTT_HOME_ASSISTANT_SUPPORT)
char MQTT_CONFIG_TOPIC[sizeof(MQTT_HOME_ASSISTANT_DISCOVERY_PREFIX) + sizeof(MQTT_CLIENT_ID) + sizeof(MQTT_CONFIG_TOPIC_TEMPLATE) - 4] = {0};
#endif

char MQTT_STATE_TOPIC[sizeof(MQTT_CLIENT_ID) + sizeof(MQTT_STATE_TOPIC_TEMPLATE) - 2] = {0};
char MQTT_COMMAND_TOPIC[sizeof(MQTT_CLIENT_ID) + sizeof(MQTT_COMMAND_TOPIC_TEMPLATE) - 2] = {0};
char MQTT_STATUS_TOPIC[sizeof(MQTT_CLIENT_ID) + sizeof(MQTT_STATUS_TOPIC_TEMPLATE) - 2] = {0};

volatile unsigned long lastMQTTConnection = MQTT_CONNECTION_TIMEOUT;

/*
   Function called when a MQTT message has arrived
   @param p_topic   The topic of the MQTT message
   @param p_payload The payload of the MQTT message
   @param p_length  The length of the payload
*/
void handleMQTTMessage(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concatenates the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }

  DEBUG_PRINTLN(F("INFO: New MQTT message received"));
  DEBUG_PRINT(F("INFO: MQTT topic: "));
  DEBUG_PRINTLN(p_topic);
  DEBUG_PRINT(F("INFO: MQTT payload: "));
  DEBUG_PRINTLN(payload);

  if (String(MQTT_COMMAND_TOPIC).equals(p_topic)) {
    if (payload.equals(String(MQTT_STATE_ON_PAYLOAD))) {
      if (sonoff.setState(true)) {
        DEBUG_PRINT(F("INFO: State changed to: "));
        DEBUG_PRINTLN(sonoff.getState());
        cmd = CMD_STATE_CHANGED;
      }
    } else if (payload.equals(String(MQTT_STATE_OFF_PAYLOAD))) {
      if (sonoff.setState(false)) {
        DEBUG_PRINT(F("INFO: State changed to: "));
        DEBUG_PRINTLN(sonoff.getState());
        cmd = CMD_STATE_CHANGED;
      }
    }
  }
}

/*
  Function called to subscribe to a MQTT topic
*/
void subscribeToMQTT(char* p_topic) {
  if (mqttClient.subscribe(p_topic)) {
    DEBUG_PRINT(F("INFO: Sending the MQTT subscribe succeeded for topic: "));
    DEBUG_PRINTLN(p_topic);
  } else {
    DEBUG_PRINT(F("ERROR: Sending the MQTT subscribe failed for topic: "));
    DEBUG_PRINTLN(p_topic);
  }
}

/*
  Function called to publish to a MQTT topic with the given payload
*/
void publishToMQTT(char* p_topic, char* p_payload) {
  if (mqttClient.publish(p_topic, p_payload, true)) {
    DEBUG_PRINT(F("INFO: MQTT message published successfully, topic: "));
    DEBUG_PRINT(p_topic);
    DEBUG_PRINT(F(", payload: "));
    DEBUG_PRINTLN(p_payload);
  } else {
    DEBUG_PRINTLN(F("ERROR: MQTT message not published, either connection lost, or message too large. Topic: "));
    DEBUG_PRINT(p_topic);
    DEBUG_PRINT(F(" , payload: "));
    DEBUG_PRINTLN(p_payload);
  }
}

/*
  Function called to connect/reconnect to the MQTT broker
*/
void connectToMQTT() {
  if (!mqttClient.connected()) {
    if (lastMQTTConnection + MQTT_CONNECTION_TIMEOUT < millis()) {
      if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_STATUS_TOPIC, 0, 1, "dead")) {
        DEBUG_PRINTLN(F("INFO: The client is successfully connected to the MQTT broker"));
        publishToMQTT(MQTT_STATUS_TOPIC, "alive");

#if defined(MQTT_HOME_ASSISTANT_SUPPORT)
        if (!sonoff.isDiscovered()) {
          sonoff.isDiscovered(true);
          // MQTT discovery for Home Assistant
          JsonObject& root = staticJsonBuffer.createObject();
          root["name"] = FRIENDLY_NAME;
          root["state_topic"] = MQTT_STATE_TOPIC;
          root["command_topic"] = MQTT_COMMAND_TOPIC;
          root.printTo(jsonBuffer, sizeof(jsonBuffer));
          publishToMQTT(MQTT_CONFIG_TOPIC, jsonBuffer);
        }
#endif
        subscribeToMQTT(MQTT_COMMAND_TOPIC);
      } else {
        DEBUG_PRINTLN(F("ERROR: The connection to the MQTT broker failed"));
        DEBUG_PRINT(F("INFO: MQTT username: "));
        DEBUG_PRINTLN(MQTT_USERNAME);
        DEBUG_PRINT(F("INFO: MQTT password: "));
        DEBUG_PRINTLN(MQTT_PASSWORD);
        DEBUG_PRINT(F("INFO: MQTT broker: "));
        DEBUG_PRINTLN(MQTT_SERVER);
      }
      lastMQTTConnection = millis();
    }
  }
}

///////////////////////////////////////////////////////////////////////////
//  CMD
///////////////////////////////////////////////////////////////////////////

void handleCMD() {
  switch (cmd) {
    case CMD_NOT_DEFINED:
      break;
#if defined(SAVE_STATE)
    case CMD_SAVE_STATE:
      cmd = CMD_NOT_DEFINED;
      saveConfig();
      break;
    case CMD_BUTTON_STATE_CHANGED:
      cmd = CMD_STATE_CHANGED;
      sonoff.setState(!sonoff.getState());
      break;
#endif
    case CMD_STATE_CHANGED:
#if defined(SAVE_STATE)
      cmd = CMD_SAVE_STATE;
#else
      cmd = CMD_NOT_DEFINED;
#endif
      if (sonoff.getState())
        publishToMQTT(MQTT_STATE_TOPIC, MQTT_STATE_ON_PAYLOAD);
      else
        publishToMQTT(MQTT_STATE_TOPIC, MQTT_STATE_OFF_PAYLOAD);
      break;
  }
}

///////////////////////////////////////////////////////////////////////////
//  SETUP() AND LOOP()
///////////////////////////////////////////////////////////////////////////

void setup() {
#if defined(DEBUG_SERIAL)
  Serial.begin(115200);
#elif defined(DEBUG_TELNET)
  telnetServer.begin();
  telnetServer.setNoDelay(true);
#endif

#if defined(SAVE_STATE)
  SPIFFS.begin();
  loadConfig();
#else
  sonoff.init();
#endif

  setupWiFi();

#if defined(TLS)
  verifyFingerprint();
#endif

  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());

#if defined(MQTT_HOME_ASSISTANT_SUPPORT)
  sprintf(MQTT_CONFIG_TOPIC, MQTT_CONFIG_TOPIC_TEMPLATE, MQTT_HOME_ASSISTANT_DISCOVERY_PREFIX, MQTT_CLIENT_ID);
  DEBUG_PRINT(F("INFO: MQTT config topic: "));
  DEBUG_PRINTLN(MQTT_CONFIG_TOPIC);
#endif

  sprintf(MQTT_STATE_TOPIC, MQTT_STATE_TOPIC_TEMPLATE, MQTT_CLIENT_ID);
  sprintf(MQTT_COMMAND_TOPIC, MQTT_COMMAND_TOPIC_TEMPLATE, MQTT_CLIENT_ID);
  sprintf(MQTT_STATUS_TOPIC, MQTT_STATUS_TOPIC_TEMPLATE, MQTT_CLIENT_ID);

  DEBUG_PRINT(F("INFO: MQTT state topic: "));
  DEBUG_PRINTLN(MQTT_STATE_TOPIC);
  DEBUG_PRINT(F("INFO: MQTT command topic: "));
  DEBUG_PRINTLN(MQTT_COMMAND_TOPIC);
  DEBUG_PRINT(F("INFO: MQTT status topic: "));
  DEBUG_PRINTLN(MQTT_STATUS_TOPIC);

  mqttClient.setServer(MQTT_SERVER, MQTT_SERVER_PORT);
  mqttClient.setCallback(handleMQTTMessage);

  connectToMQTT();

#if defined(OTA)
  setupOTA();
#endif
}

void loop() {
  connectToMQTT();
  mqttClient.loop();

  yield();
  
  handleCMD();

  yield();
  
#if defined(DEBUG_TELNET)
  // handle the Telnet connection
  handleTelnet();
#endif

  yield();

#if defined(OTA)
  handleOTA();
#endif

  yield();
}
