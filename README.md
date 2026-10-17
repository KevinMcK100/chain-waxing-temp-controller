# Chain Waxing Temperature Controller

An ESP32-based temperature controller for melting and maintaining chain wax at a precise temperature. Features an OLED display, real-time web dashboard with Chart.js, ESPHome smart plug control, and push notifications via ntfy.sh.

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

## Features

### State Machine (Heating → Coasting → Holding)

The controller uses a simple, reliable state machine — no PID tuning required:

```
                 ┌──────────────┐
                 │   HEATING    │──── temp ≥ (target - bulkCoastMargin)
                 └──────────────┘              │
                      │                        ▼
                      │                 ┌──────────────┐
                      │                 │   COASTING   │──── temp ≥ target
                      │                 └──────────────┘       │
                      │                        │               ▼
                      │                        │        ┌──────────────┐
                      │                        │        │   HOLDING    │
                      │                        │        └──────────────┘
                      │                        │               │
                      │◄─── temp ≤ restart ────┴───────────────┘
                      │      (dropped below hold band)
                      ▼
```

- **Heating**: Heater ON, wax rising toward target
- **Coasting**: Heater OFF, wax continues rising from thermal inertia
- **Holding**: At target temperature, heater cycles on/off to maintain the band

### OLED Display

- Animated status indicator (`HEATING`, `COASTING`, `HOLDING`, `COOLING`)
- Large live temperature reading
- Progress bar showing position between start and target temperatures

### Web Dashboard

A built-in web server hosts a dark-themed dashboard with:

- Live temperature, heater status, and mode
- Real-time Chart.js line chart (up to ~66 minutes of history)
- Coloured chart segments (blue = heating, green = holding, red = overshoot/cooling)
- Adjustable settings: target temperature, coast margins, and hold triggers

Access it at the ESP32's IP address (check serial monitor output).

### Push Notifications

Uses [ntfy.sh](https://ntfy.sh) for notifications on key events:

- Wax approaching target (coasting started)
- Target temperature reached
- Over-temperature warning (+10°C above target)
- Sensor fault detection
- Reheating phase started

### Smart Plug Control

Controls an ESPHome-based smart plug over HTTP to turn the heater on/off. The plug is switched off during coasting, hold, and fault/safety conditions.

### Persistent Settings

Target temperature, coast margins, and hold trigger are saved to ESP32 NVS (non-volatile storage) and survive reboots. Settings can be changed live via the web dashboard.

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