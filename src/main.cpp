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

CRGB* m_buffer;
CRGB m_current = CRGB::White;
String mode = "";
byte counter;

void setupMqtt();
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
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

  m_buffer = new CRGB[NUM_PIXELS];
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(m_buffer, NUM_PIXELS);
  FastLED.addLeds<WS2812, SYS_LED, RGB>(m_buffer, 1);
  FastLED.clear();
  
  setColor(CRGB::White);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (mode == "party") {
    for (int i = 0; i < NUM_PIXELS; i++ ) {         // от 0 до первой трети
      m_buffer[i] = CHSV(counter + i * 2, 255, 255);  // HSV. Увеличивать HUE (цвет)
    // умножение i уменьшает шаг радуги
    }
    counter++;        // counter меняется от 0 до 255 (тип данных byte)
    FastLED.show();
    delay(5);
  } else if (mode == "neon") {
    for (int i = 0; i < 256; i++) {
      client.loop();
      if (mode != "neon") break;
      FastLED.setBrightness(i);
      FastLED.show();
      delay(5);
    }
    for (int i = 255; i >= 0; i--) {
      client.loop();
      if (mode != "neon") break;
      FastLED.setBrightness(i);
      FastLED.show();
      delay(5);
    }
  } else if (mode == "candle") {
    
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

  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  int pos = 0;
  String command = "";
  for (int i=0;i<length;i++) {
    if ((char)payload[i] == '=')
      break;
    command += (char)payload[i];
    pos++;
  }

  if (command == "1") {
    digitalWrite(BUILTIN_LED, LOW);   
    setColor(m_current);
    return;
  }

  if (command == "0") {
    mode = "";
    digitalWrite(BUILTIN_LED, HIGH); 
    setColor(CRGB::Black);
    return;
  }

  if (command == "rgb") {
    int idx = 0;
    uint8_t* map = new uint8_t[3];
    command = "";
    for (int i=pos + 1;i<length;i++) {
      if ((char)payload[i] == ':') {
        map[idx] = (uint8_t)command.toInt();
        command = "";
        idx++;
        continue;
      }

      command += (char)payload[i];  
    }
    map[idx] = (uint8_t)command.toInt();   

    mode = "";

    m_current.setRGB(map[0],map[1],map[2]);    
    setColor(m_current);
    return;
  }

  if (command == "temp") {
    command = "";
    for (int i=pos + 1;i<length;i++) {
      command += (char)payload[i];
    }

    mode = "";

    float temp = command.toInt() / 100.0;
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

  if (command == "brightness") {
    command = "";
    for (int i=pos + 1;i<length;i++) {
      command += (char)payload[i];
    }
    FastLED.setBrightness(command.toInt());
    FastLED.show();
    FastLED.delay(25); 
    return;
  }

  if (command == "scene") {
    command = "";
    for (int i=pos + 1;i<length;i++) {
      command += (char)payload[i];
    }

    mode = command;

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

void reconnect() {
  while (!client.connected()) {
    Serial.println("attempting MQTT connection...");

    String clientId = "strip1";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), username, devicepassword)) {
      Serial.println("connected");
      
      // 
      if(client.subscribe(commands.c_str(), 1)) {
        Serial.println("subscribed");
      }
      
      String message = "esp32-led-strip connected [";
      message += clientId;
      message += "]";
      
      if(client.publish(commands.c_str(), message.c_str())) {
        Serial.println("published");
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      
      // Wait 5 seconds before retrying
      delay(5000);
    }
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
