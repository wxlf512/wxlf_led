#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Update.h>
#include <PubSubClient.h>
#include "config.h"
#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>

WiFiClientSecure espClient;
PubSubClient client(espClient);

#define NUM_PIXELS 342
#define DATA_PIN 5
#define SYS_LED 10
#define ENABLE_SYS_LED 0

#define COOLING  55
#define SPARKING 120

NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> strip(NUM_PIXELS, DATA_PIN);
#if ENABLE_SYS_LED
NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp32Rmt1Ws2812xMethod> sysLed(1, SYS_LED);
#endif
WebServer server(80);
const char* OTA_HOSTNAME = "wxlf-led-strip";

enum SceneMode {
  SceneNone,
  SceneParty,
  SceneNeon,
  SceneCandle,
};

RgbColor m_current = RgbColor(255, 255, 255);
char mode[16] = "";
SceneMode sceneMode = SceneNone;
byte counter;
uint8_t neonBrightness = 0;
bool neonIncreasing = true;
unsigned long lastSceneUpdateMs = 0;
unsigned long reconnectStartMs = 0;
unsigned long lastReconnectTryMs = 0;
const unsigned long RECONNECT_TIMEOUT_MS = 300000;
const unsigned long RECONNECT_INTERVAL_MS = 5000;
const unsigned long SCENE_UPDATE_INTERVAL_MS = 20;

void setupMqtt();
void setupWebServer();
void callback(char* topic, byte* payload, unsigned int length);
bool reconnect();
void syncSysLed();
void setColor(const RgbColor& color);
void setSceneMode(const char* newMode);
void updateScene();
void partyScene();
void neonScene();
void candleScene();
void CandleScene();

uint8_t qsub8(uint8_t a, uint8_t b) {
  return (a > b) ? a - b : 0;
}

uint8_t qadd8(uint8_t a, uint8_t b) {
  uint16_t sum = (uint16_t)a + (uint16_t)b;
  return (sum > 255) ? 255 : (uint8_t)sum;
}

RgbColor hsvToRgb(uint8_t hue, uint8_t sat, uint8_t val) {
  if (sat == 0) {
    return RgbColor(val, val, val);
  }

  uint8_t region = hue / 43;
  uint8_t remainder = (hue - region * 43) * 6;

  uint16_t p = (uint16_t)val * (255 - sat) / 255;
  uint16_t q = (uint16_t)val * (255 - ((uint16_t)sat * remainder / 255)) / 255;
  uint16_t t = (uint16_t)val * (255 - ((uint16_t)sat * (255 - remainder) / 255)) / 255;

  switch (region) {
    case 0: return RgbColor(val, (uint8_t)t, (uint8_t)p);
    case 1: return RgbColor((uint8_t)q, val, (uint8_t)p);
    case 2: return RgbColor((uint8_t)p, val, (uint8_t)t);
    case 3: return RgbColor((uint8_t)p, (uint8_t)q, val);
    case 4: return RgbColor((uint8_t)t, (uint8_t)p, val);
    default: return RgbColor(val, (uint8_t)p, (uint8_t)q);
  }
}

RgbColor HeatColor(uint8_t temperature) {
  uint8_t heatramp = (temperature & 0x3F) << 2;
  if (temperature > 0x80) {
    return RgbColor(255, 255, heatramp);
  } else if (temperature > 0x40) {
    return RgbColor(255, heatramp, 0);
  } else {
    return RgbColor(heatramp, 0, 0);
  }
}

uint8_t random8() {
  return (uint8_t)random(0, 256);
}

uint8_t random8(uint8_t limit) {
  return (uint8_t)random(0, limit);
}

uint8_t random8(uint8_t low, uint8_t high) {
  return (uint8_t)random(low, high + 1);
}

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

  setupWebServer();
  espClient.setCACert(ca_cert);

  strip.Begin();
  strip.SetBrightness(255);
  strip.ClearTo(RgbColor(0, 0, 0));
  strip.Show();
#if ENABLE_SYS_LED
  sysLed.Begin();
  sysLed.SetBrightness(255);
  sysLed.ClearTo(RgbColor(0, 0, 0));
  sysLed.Show();
#endif
  syncSysLed();

  setColor(RgbColor(255, 255, 255));
}

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    reconnect();
  } else {
    client.loop();
  }

  updateScene();
}

void setSceneMode(const char* newMode) {
  if (strcmp(newMode, "party") == 0) {
    sceneMode = SceneParty;
    counter = 0;
    neonBrightness = 0;
    neonIncreasing = true;
  } else if (strcmp(newMode, "neon") == 0) {
    sceneMode = SceneNeon;
    neonBrightness = 0;
    neonIncreasing = true;
    strip.SetBrightness(neonBrightness);
    strip.Show();
  } else if (strcmp(newMode, "candle") == 0) {
    sceneMode = SceneCandle;
  } else {
    sceneMode = SceneNone;
  }
  strncpy(mode, newMode, sizeof(mode) - 1);
  mode[sizeof(mode) - 1] = '\0';
}

void updateScene() {
  unsigned long now = millis();
  if (sceneMode == SceneNone || now - lastSceneUpdateMs < SCENE_UPDATE_INTERVAL_MS) {
    return;
  }
  lastSceneUpdateMs = now;

  switch (sceneMode) {
    case SceneParty:
      partyScene();
      break;
    case SceneNeon:
      neonScene();
      break;
    case SceneCandle:
      candleScene();
      break;
    default:
      break;
  }
}

void partyScene() {
  for (int i = 0; i < NUM_PIXELS; i++) {
    uint8_t h = counter + i * 2;
    strip.SetPixelColor(i, hsvToRgb(h, 255, 255));
  }
  counter++;
  strip.Show();
  syncSysLed();
}

void neonScene() {
  if (neonIncreasing) {
    if (neonBrightness < 255) {
      neonBrightness++;
    } else {
      neonIncreasing = false;
    }
  } else {
    if (neonBrightness > 0) {
      neonBrightness--;
    } else {
      neonIncreasing = true;
    }
  }
  strip.SetBrightness(neonBrightness);
  strip.Show();
  syncSysLed();
}

void candleScene() {
  CandleScene();
  strip.Show();
  syncSysLed();
}

void setupWebServer() {
  server.on("/status", HTTP_GET, []() {
    String payload = "{\"mac\":\"" + WiFi.macAddress() + "\"}";
    server.send(200, "application/json", payload);
  });

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 OTA Update</title>
  <style>
    body { font-family: Arial, sans-serif; background: #111; color: #f5f5f5; margin: 0; padding: 24px; }
    .card { max-width: 520px; margin: 40px auto; background: #1c1c1c; border-radius: 12px; padding: 24px; box-shadow: 0 8px 24px rgba(0,0,0,0.25); }
    h1 { margin-top: 0; }
    input[type="file"] { width: 100%; margin: 12px 0 16px; }
    button { background: #00bcd4; color: white; border: 0; padding: 10px 16px; border-radius: 8px; cursor: pointer; }
    button:disabled { opacity: 0.6; cursor: wait; }
    .progress { display: none; margin-top: 16px; }
    .bar { width: 100%; height: 12px; background: #333; border-radius: 999px; overflow: hidden; }
    .bar > div { height: 100%; width: 0%; background: linear-gradient(90deg, #00bcd4, #5eead4); transition: width 0.2s ease; }
    .status { margin-top: 8px; color: #cfd8dc; font-size: 0.95rem; }
  </style>
</head>
<body>
  <div class="card">
    <h1>ESP32 Web OTA</h1>
    <p>Select a firmware binary file and upload it to update the device.</p>
    <form id="otaForm" method="POST" action="/update" enctype="multipart/form-data">
      <input id="firmwareInput" type="file" name="firmware" accept=".bin" required>
      <button id="uploadButton" type="submit">Upload firmware</button>
    </form>
    <div id="progressBox" class="progress">
      <div class="bar"><div id="progressBar"></div></div>
      <div id="statusText" class="status">Waiting for upload…</div>
    </div>
  </div>
  <script>
    const form = document.getElementById('otaForm');
    const button = document.getElementById('uploadButton');
    const progressBox = document.getElementById('progressBox');
    const progressBar = document.getElementById('progressBar');
    const statusText = document.getElementById('statusText');
    const firmwareInput = document.getElementById('firmwareInput');

    form.addEventListener('submit', function (event) {
      event.preventDefault();
      if (!firmwareInput.files || firmwareInput.files.length === 0) {
        statusText.textContent = 'Please select a firmware file first.';
        return;
      }

      const file = firmwareInput.files[0];
      const formData = new FormData(form);

      progressBox.style.display = 'block';
      progressBar.style.width = '0%';
      statusText.textContent = 'Uploading ' + file.name + '...';
      button.disabled = true;

      const xhr = new XMLHttpRequest();
      xhr.upload.addEventListener('progress', function (e) {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          progressBar.style.width = percent + '%';
          statusText.textContent = 'Uploading ' + file.name + ': ' + percent + '%';
        }
      });

      xhr.addEventListener('load', function () {
        progressBar.style.width = '100%';
        if (xhr.status >= 200 && xhr.status < 300) {
          statusText.textContent = xhr.responseText || 'Upload complete. Rebooting...';
        } else {
          statusText.textContent = 'Upload failed.';
        }
        button.disabled = false;
      });

      xhr.addEventListener('error', function () {
        statusText.textContent = 'Upload failed. Please try again.';
        button.disabled = false;
      });

      xhr.open('POST', '/update');
      xhr.send(formData);
    });
  </script>
</body>
</html>
)rawliteral");
  });

  server.on(
    "/update",
    HTTP_POST,
    []() {
      if (Update.hasError()) {
        server.send(200, "text/plain", "OTA update failed");
      } else {
        server.send(200, "text/plain", "OTA update successful. Rebooting...");
      }
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA upload started: %s\n", upload.filename.c_str());
        size_t fileSize = server.header("X-File-Size").toInt();
        if (fileSize == 0) fileSize = UPDATE_SIZE_UNKNOWN; 
        if (!Update.begin(fileSize)) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          Serial.printf("OTA upload finished: %u bytes\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  server.begin();
  Serial.println("Web OTA server started");
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
    setColor(m_current);
    return;
  }

  if (strcmp(command, "0") == 0) {
    mode[0] = '\0';
    setColor(RgbColor(0, 0, 0));
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
    m_current = RgbColor((uint8_t)r, (uint8_t)g, (uint8_t)b);
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

    setColor(RgbColor(red, green, blue));
    return;
  }

  if (strcmp(command, "brightness") == 0) {
    int brightness = 0;
    if (pos + 1 < (int)length) {
      sscanf((const char*)payload + pos + 1, "%d", &brightness);
    }
    strip.SetBrightness((uint8_t)brightness);
    strip.Show();
    delay(25);
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
    setSceneMode(mode);
    return;
  }
}

void syncSysLed() {
#if ENABLE_SYS_LED
  sysLed.SetPixelColor(0, strip.GetPixelColor(0));
  sysLed.Show();
#endif
}

void setColor(const RgbColor& color) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    strip.SetPixelColor(i, color);
  }
  strip.Show();
  delay(25);
  syncSysLed();
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
      RgbColor color = HeatColor( heat[j]);
      int pixelnumber;
      if (false) {
        pixelnumber = (NUM_PIXELS-1) - j;
      } else {
        pixelnumber = j;
      }
      strip.SetPixelColor(pixelnumber, color);
    }
}
