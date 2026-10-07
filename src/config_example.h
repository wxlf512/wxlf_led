#include <Arduino.h>

const char *mqttServer = "<MQTT_SERVER>";
int mqttPort = <MQTT_PORT>;

const char *username = "<IOT_BROKER_ID>"; 
const char *devicepassword = "<IOT_BROKER_PASSWORD>"; 
// Setup IoT
const String deviceId = "<DEVICE_ID>";
const String commands = <COMMANDS_TOPIC>;
// WiFi
const String ssid = "<SSID>";
const String wifiPass = "<WIFI_PASSWORD>";

const char *ca_cert = "<MQTT_CA_CERT>";