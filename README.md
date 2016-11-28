# Itead Sonoff
## Alternative firmware
Alternative firmware for Itead Sonoff switches, based on the MQTT protocol and a TLS connection.
Sonoff is a small ESP8266 based module, that can toggle mains power and costs only $4.85. More information can be found [here](https://www.itead.cc/sonoff-wifi-wireless-switch.html).

## Features
- Wi-Fi credentials configuration using WiFiManager
- Web configuration portal to setup MQTT username, password, broker IP address and port
- TLS support for CloudMQTT. For any other MQTT brokers, you need to change:
	- `broker`: IP address of the MQTT broker, and
	- `fingerprint`: Fingerprint of its certificate (`openssl x509 -fingerprint -in  <certificate>.crt`)
- Onboard button:
  - Short press: Toggle the state of the relay
  - Medium press: Restart the relay (~3 [s])
  - Long press: Reset the relay
- PIR motion sensor support. Make sure to connect your sensor on GPIO14

## Steps
- Connect the Sonoff to a FTDI adapter and hold down the button, while powering it (programing mode)
- Upload the firmware with the Arduino IDE (use the settings below)
- Connect to the new Wi-Fi AP and memorize its name (1)
- Select `Configure WiFi`(2)
- Choose your network (3) and enter your MQTT username, password, broker IP address and broker port (4)
- Update your configuration in your home automation system. An example for `Home Assistant`is available below

### Settings for the Arduino IDE

| Parameter       | Value                    |
| ----------------|--------------------------|
| Board           | Generic ESP8266 Module   |
| Flash Mode      | DIO                      |  
| Flash Frequency | 40 MHz                   |  
| Upload Using    | Serial                   |  
| CPU Frequency   | 80 MHz                   |  
| Flash Size      | 512K (64K SPIFFS)        |  
| Reset Method    | ck                       |  
| Upload Speed    | 115200                   |  
| Port            | COMX, /dev/ttyUSB0, etc. |


### Wi-Fi and MQTT Configuration
![Steps](images/Steps.png)

### MQTT topics
| #          | Topic                     | Payload   |
| -----------|---------------------------|-----------|
| State      | `<Chip_ID>/switch/state`  | `ON`/`OFF`|
| Command    | `<Chip_ID>/switch/switch` | `ON`/`OFF`|

### Configuration (Home Assistant)
configuration.yaml :

```yaml
switch:
  platform: mqtt
  name: 'Switch'
  state_topic: 'CBF777/switch/state'
  command_topic: 'CBF777/switch/switch'
  optimistic: false
```

## Schematic
- VCC (Sonoff) -> VCC (FTDI)
- RX  (Sonoff) -> TX  (FTDI)
- TX  (Sonoff) -> RX  (FTDI)
- GND (Sonoff) -> GND (FTDI)

![Schematic](images/Schematic.jpg)

## Versions
- 1.0: Initial version
- 1.1: Add TLS support
- 1.2: Add PIR motion sensor support
