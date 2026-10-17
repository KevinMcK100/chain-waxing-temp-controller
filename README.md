# Chain Waxing Temperature Controller

An ESP32-based temperature controller for melting and maintaining chain wax at a precise temperature. Features an OLED display, real-time web dashboard with Chart.js, ESPHome smart plug control, and push notifications via ntfy.sh.

## Motivation & Background

Chain waxing with a standard slow cooker is a brilliant method for drivetrain maintenance, but it comes with a frustrating operational bottleneck: it takes roughly 45 minutes for the wax to reach its operating temperature.

Historically, this process relied on setting a manual timer, walking away, and returning to drop the chain in. However, this workflow introduces a major safety and convenience flaw—human error. If you get caught up in another task when the alarm sounds, it is all too easy to dismiss the notification and forget about the pot entirely, risking degraded wax, smoking pots, or worse. Out of an abundance of caution, I used to rely solely on low-heat settings to prevent runaway temperatures, sacrificing valuable time when a fast, high-heat ramp-up would be far more optimal.

## The Solution

To eliminate the guesswork and remove the safety hazard, I built an automated, network-connected temperature regulation system.

Instead of relying on passive timers, the controller acts as a precision thermostat. It safely leverages high heat to bring the wax up to temperature rapidly, then intelligently cycles power to the cooker (much like a kitchen oven hob) to maintain a rock-solid target temperature indefinitely. Even if you get distracted and walk away for hours, the wax remains safely locked at its target temperature—well below the smoking point.

## Hardware

| Component | Details |
|-----------|---------|
| **Microcontroller** | ESP32 Dev Module |
| **Temperature Sensor** | DS18B20 (OneWire, GPIO 4) |
| **Display** | SSD1306 OLED 128×64 (I2C, pins 21/22) |
| **Smart Plug** | ESPHome-compatible switch (HTTP API) |

### Wiring

```
ESP32                  DS18B20
GPIO 4   ────────────── DATA (yellow)
3.3V     ────────────── VCC  (red)
GND      ────────────── GND  (black)
(4.7kΩ pull-up between DATA and VCC)

ESP32                  SSD1306 OLED
GPIO 21  ────────────── SDA
GPIO 22  ────────────── SCL
3.3V     ────────────── VCC
GND      ────────────── GND
```

## Setup

### 1. Clone & prepare config

```bash
git clone git@github.com:KevinMcK100/chain-waxing-temp-controller.git
cd chain-waxing-temp-controller
cp src/config.h.example src/config.h
```

### 2. Edit `src/config.h`

Fill in your WiFi credentials, smart plug details, and ntfy topic:

| Setting | Description |
|---------|-------------|
| `ssid` / `password` | Your 2.4 GHz WiFi network |
| `smartPlugIP` | IP of your ESPHome smart plug |
| `smartPlugEntity` | Entity name of the plug |
| `ntfyTopic` | Your ntfy.sh topic for push notifications |

### 3. Build & upload

Using [PlatformIO](https://platformio.org) (VS Code extension or CLI):

```bash
# Build
pio run

# Upload to ESP32 (USB connected)
pio run --target upload

# Monitor serial output
pio device monitor
```

## Key Features

- **Intelligent Two-Stage Thermal Control:** Uses a bulk heat-up phase with thermal momentum "coasting" followed by a tight holding phase to prevent temperature overshoot.
- **Instant Push Notifications (ntfy):** Sends real-time alerts to your phone when the wax reaches target temperature, enters coasting phases, or if any safety thresholds are triggered.
- **On-Device OLED Display:** A local hardware display mounted directly on the wax pot, providing large typography, dynamic progress tracking, and live status animations.
- **Network Web Dashboard:** A mobile-accessible local control panel featuring a real-time Chart.js telemetry graph, interactive configuration tooltips, and persistent flash memory settings.

## Project Structure

```
├── platformio.ini          # PlatformIO configuration & library dependencies
├── src/
│   ├── main.cpp            # Main program logic & state machine
│   ├── config.h.example    # Configuration template (copy to config.h)
│   └── wax_webserver.h     # Web server, HTML dashboard, Chart.js
└── .gitignore              # config.h excluded from version control
```

## Library Dependencies

Automatically managed by PlatformIO (`lib_deps` in `platformio.ini`):

- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [OneWire](https://github.com/PaulStoffregen/OneWire)
- [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library)

## Safety

- Over-temperature shutoff: heater turns off if temp exceeds target + 10°C
- Sensor fault detection: invalid readings (disconnected sensor) immediately kill power
- Coast margin validation: hold coast margin can never exceed the reheat trigger
- All configuration is validated before being persisted