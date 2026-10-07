# wxlf LED Strip Controller

A lightweight ESP32-based controller for an addressable LED strip. The project connects to Wi‑Fi, listens to MQTT commands, drives RGB animation scenes, and supports over-the-air (OTA) firmware updates from a browser.

It is designed for a custom LED installation and is built with PlatformIO and the Arduino framework.

## Features

- ESP32 control board support (tested with Lolin C3 Mini)
- WS2812/NeoPixel-style LED strip support
- Wi‑Fi connection with reconnect handling
- MQTT control for color, brightness, scenes, and power
- Built-in Web OTA update page
- Scene modes:
  - `party`
  - `neon`
  - `candle`
- Static color and temperature-based white light modes

## Hardware

This project is intended to run on:

- ESP32 development board
- 5V LED strip (NeoPixel/WS2812 style)
- External 5V power supply for the strip
- Data line connected to GPIO 5 (`DATA_PIN` in firmware)

The default firmware is configured for 342 LEDs and uses a single data channel.

## Project structure

- `src/main.cpp` — main firmware logic
- `src/config_example.h` — example configuration template
- `platformio.ini` — PlatformIO project configuration

## Quick start

1. Install PlatformIO.
2. Clone the repository.
3. Copy `src/config_example.h` to `src/config.h`.
4. Fill in your Wi‑Fi and MQTT credentials.
5. Build and upload the firmware:

```bash
pio run -t upload
```

6. Open the device in a browser on the ESP32's IP address and use the built-in OTA page.

## Configuration

The firmware expects a local `config.h` file with values such as:

- Wi‑Fi SSID and password
- MQTT broker address and port
- MQTT username/password
- MQTT topic for commands
- Device ID
- CA certificate for secure MQTT connections

Example values are provided in `src/config_example.h`.

## MQTT commands

The controller listens for commands on the configured command topic. The payload format is similar to this:

- `1` — turn LED strip on
- `0` — turn LED strip off
- `rgb=255:0:0` — set a static RGB color
- `brightness=180` — set brightness level
- `scene=party` — switch to a scene mode
- `scene=neon` — switch to neon fade mode
- `scene=candle` — switch to candle flicker mode
- `temp=6500` — set a color temperature based on white light

Examples are parsed directly in the firmware and applied immediately.

## Web interface

The ESP32 starts a small web server on port 80 with:

- `/` — OTA firmware upload page
- `/status` — JSON status endpoint returning the device MAC address
- `/update` — firmware upload endpoint

This makes it easy to update the device without using a serial programmer.

