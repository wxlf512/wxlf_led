#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "config.h"
#include <FastLED.h>

WiFiClientSecure espClient;
PubSubClient client(espClient);

#define NUM_PIXELS 342
#define DATA_PIN 5
#define SYS_LED 10

#define COOLING  55
#define SPARKING 120

CRGB m_buffer[NUM_PIXELS];
CRGB m_current = CRGB::White;
char mode[16] = "";
byte counter;
unsigned long reconnectStartMs = 0;
unsigned long lastReconnectTryMs = 0;
const unsigned long RECONNECT_TIMEOUT_MS = 300000;
const unsigned long RECONNECT_INTERVAL_MS = 5000;

void setupMqtt();
void callback(char* topic, byte* payload, unsigned int length);
bool reconnect();
void setColor(CRGB color);

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.begin(ssid, wifiPass);
  Serial.print("connecting to ");
  Serial.print(ssid);
  Serial.println("...");

  setupMqtt();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println('\n');
  Serial.println("connection estabilished");
  Serial.print("IP Adress: \t");
  Serial.println(WiFi.localIP());

  espClient.setCACert(ca_cert);

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(m_buffer, NUM_PIXELS);
  FastLED.addLeds<WS2812, SYS_LED, RGB>(m_buffer, 1);
  FastLED.clear();
  
  setColor(CRGB::White);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    reconnect();
  } else {
    client.loop();
  }

  if (strcmp(mode, "party") == 0) {
    for (int i = 0; i < NUM_PIXELS; i++ ) {         // от 0 до первой трети
      m_buffer[i] = CHSV(counter + i * 2, 255, 255);  // HSV. Увеличивать HUE (цвет)
    // умножение i уменьшает шаг радууги
    }
    counter++;        // counter меняется от 0 до 255 (тип данных byte)
    FastLED.show();
    delay(5);
  } else if (strcmp(mode, "neon") == 0) {
    for (int i = 0; i < 256; i++) {
      client.loop();
      if (strcmp(mode, "neon") != 0) break;
      FastLED.setBrightness(i);
      FastLED.show();
      delay(5);
    }
    for (int i = 255; i >= 0; i--) {
      client.loop();
      if (strcmp(mode, "neon") != 0) break;
      FastLED.setBrightness(i);
      FastLED.show();
      delay(5);
    }
  } else if (strcmp(mode, "candle") == 0) {
    
  }
  
}

void setupMqtt() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  client.setBufferSize(1024);
  client.setKeepAlive(15);
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  int pos = 0;
  char command[16] = "";
  for (unsigned int i = 0; i < length && pos < (int)sizeof(command) - 1; i++) {
    if ((char)payload[i] == '=')
      break;
    command[pos++] = (char)payload[i];
  }
  command[pos] = '\0';

  if (strcmp(command, "1") == 0) {
    digitalWrite(BUILTIN_LED, LOW);
    setColor(m_current);
    return;
  }

  if (strcmp(command, "0") == 0) {
    mode[0] = '\0';
    digitalWrite(BUILTIN_LED, HIGH);
    setColor(CRGB::Black);
    return;
  }

  if (strcmp(command, "rgb") == 0) {
    int r = 0;
    int g = 0;
    int b = 0;
    if (pos + 1 < (int)length) {
      sscanf((const char*)payload + pos + 1, "%d:%d:%d", &r, &g, &b);
    }

    mode[0] = '\0';
    m_current.setRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
    setColor(m_current);
    return;
  }

  if (strcmp(command, "temp") == 0) {
    int tempHundred = 0;
    if (pos + 1 < (int)length) {
      sscanf((const char*)payload + pos + 1, "%d", &tempHundred);
    }

    mode[0] = '\0';
    float temp = tempHundred / 100.0;
    int red = 0;
    int green = 0;
    int blue = 0;

    if (temp <= 66) {
      red = 255;
    } else {
      red = temp - 60;
      red = 329.698727446 * pow(red, -0.1332047592);
      if (red < 0) {
        red = 0;
      }
      if (red > 255) {
        red = 255;
      }
    }

    if (temp <= 66) {
      green = temp;
      green = 99.4708025861 * log(green) - 161.1195681661;
      if (green < 0) {
        green = 0;
      }
      if (green > 255) {
        green = 255;
      }
    } else {
      green = temp - 60;
      green = 288.1221695283 * pow(green, -0.0755148492);
      if (green < 0) {
        green = 0;
      }
      if (green > 255) {
        green = 255;
      }
    }

    if (temp >= 66) {
      blue = 255;
    } else {
      blue = temp - 10;
      blue = 138.5177312231 * log(blue) - 305.0447927307;
      if (blue < 0) {
        blue = 0;
      }
      if (blue > 255) {
        blue = 255;
      }
    }

    setColor(CRGB(red, green, blue));
    return;
  }

  if (strcmp(command, "brightness") == 0) {
    int brightness = 0;
    if (pos + 1 < (int)length) {
      sscanf((const char*)payload + pos + 1, "%d", &brightness);
    }
    FastLED.setBrightness(brightness);
    FastLED.show();
    FastLED.delay(25);
    return;
  }

  if (strcmp(command, "scene") == 0) {
    unsigned int sceneLen = 0;
    if (length > (unsigned int)pos + 1) {
      sceneLen = length - (unsigned int)pos - 1;
      if (sceneLen >= sizeof(mode)) {
        sceneLen = sizeof(mode) - 1;
      }
      memcpy(mode, payload + pos + 1, sceneLen);
    }
    mode[sceneLen] = '\0';
    return;
  }
}

void setColor(CRGB color) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    m_buffer[i] = color; 
  }
  
  FastLED.show();
  FastLED.delay(25); 
}

bool reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    if (reconnectStartMs == 0) {
      reconnectStartMs = millis();
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.disconnect();
      WiFi.reconnect();
    } else if (millis() - reconnectStartMs > RECONNECT_TIMEOUT_MS) {
      Serial.println("WiFi reconnect timeout exceeded, rebooting...");
      ESP.restart();
    }
    return false;
  }

  if (client.connected()) {
    reconnectStartMs = 0;
    lastReconnectTryMs = 0;
    return true;
  }

  if (millis() - lastReconnectTryMs < RECONNECT_INTERVAL_MS) {
    return false;
  }

  lastReconnectTryMs = millis();
  if (reconnectStartMs == 0) {
    reconnectStartMs = millis();
  }

  Serial.println("attempting MQTT connection...");

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "strip1%04x", random(0xffff));

  if (client.connect(clientId, username, devicepassword)) {
    Serial.println("connected");

    if(client.subscribe(commands.c_str(), 1)) {
      Serial.println("subscribed");
    }

    char message[64];
    snprintf(message, sizeof(message), "esp32-led-strip connected [%s]", clientId);

    if(client.publish(commands.c_str(), message)) {
      Serial.println("published");
    }

    reconnectStartMs = 0;
    lastReconnectTryMs = 0;
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again later");
    if (millis() - reconnectStartMs > RECONNECT_TIMEOUT_MS) {
      Serial.println("MQTT reconnect timeout exceeded, rebooting...");
      ESP.restart();
    }
    return false;
  }
}

void CandleScene() {
  static uint8_t heat[NUM_PIXELS];
 
  // Step 1.  Cool down every cell a little
    for( int i = 0; i < NUM_PIXELS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_PIXELS) + 2));
    }
  
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= NUM_PIXELS - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }
    
    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160,255) );
    }
 
    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < NUM_PIXELS; j++) {
      CRGB color = HeatColor( heat[j]);
      int pixelnumber;
      if( false ) {
        pixelnumber = (NUM_PIXELS-1) - j;
      } else {
        pixelnumber = j;
      }
      m_buffer[pixelnumber] = color;
    }
}
