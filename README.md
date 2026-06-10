# lego-clawd

A tiny LEGO-based Clawd desktop companion powered by an ESP32-S3 LCD board.

The device is built around a LEGO brick Clawd shell, a Waveshare ESP32-S3-LCD-1.9 display board, and a LEGO-compatible servo arm. It will display expressive eyes, Codex usage status, and other developer-tool usage metrics, while using a side arm to provide physical feedback.

## Project Goals

- Build a small desktop hardware companion inspired by the Claude Code / Clawd style.
- Display useful developer status information on a 1.9-inch LCD:
  - Codex 5-hour usage
  - Codex 1-week usage
  - Other AI/software usage metrics
  - Idle / working / warning / limit states
  - Simple pixel-style facial expressions
- Use a LEGO-compatible servo to move one side arm:
  - idle pose
  - light wave
  - warning wave
  - usage-limit alert
- Keep the first version simple and reliable:
  - no touch screen
  - one display
  - one servo
  - USB-C powered during development
  - optional external 5V servo power if needed

## Hardware

### LEGO Shell

The body is based on a LEGO brick Clawd figure.

Current reference build:

- Orange rectangular body
- Black rectangular eyes
- Side protrusion for arm mounting
- Front face area planned for 1.9-inch LCD + 3D-printed bezel

The LEGO shell may be enlarged slightly to fit the display board, wiring, and servo.

### Main Board

Board:

- Waveshare ESP32-S3-LCD-1.9
- Variant: non-touch version
- Product page: https://docs.waveshare.net/ESP32-S3-LCD-1.9/?variant=ESP32-S3-LCD-1.9

Important known hardware features:

- ESP32-S3R8
- 1.9-inch LCD
- 170 × 320 resolution
- 16MB Flash
- 8MB PSRAM
- USB Type-C for power, upload, and serial debugging
- 3.7V lithium battery connector
- Micro SD slot
- QMI8658 IMU
- WS2812 RGB LED on the back side for the non-touch version
- Pico-compatible GPIO header

### Display

The display will be used in landscape orientation.

Primary screens:

1. Face / idle screen
2. Codex 5-hour usage screen
3. Codex 1-week usage screen
4. Other software usage screen
5. Warning / limit screen

Visual direction:

- black background
- high-contrast text
- simple pixel face
- green / yellow / red status indicators
- large percentage number or progress ring
- small status text

### Servo

Servo:

- LEGO-compatible servo
- Model: AP3009-06 / JM 9g servo
- Voltage: 6V nominal
- Signal: 3-wire servo control
- Angle: 270° / 120° version, depending on purchased unit
- Intended use: one side arm movement

Mechanical design note:

The servo output shaft is perpendicular to the side face of the LEGO body. Therefore the arm rotates within the side-plane of the body, like a small wing, side pointer, or side flipper. It is not a forward-facing waving arm.

Recommended first arm design:

- short block-style side arm
- length: 20-30mm
- width: 6-10mm
- thickness: 6-8mm
- movement range: roughly -60° to +60° around the neutral position
- use a small 3D-printed or LEGO-compatible adapter on the white cross axle

## Power

During development:

- ESP32-S3 board: USB-C from computer or USB charger
- Servo: external 5V-6V power supply is recommended
- ESP32 GND and servo power GND must be connected together

For a very light single-servo test, the servo may be powered from the board's 5V pin if the USB power source is strong enough, but if the board resets, the screen flickers, or the servo jitters, use an external servo power supply.

Never power the servo from the ESP32 3.3V pin.

## Development Environment

Use:

- VS Code
- PlatformIO extension
- Arduino framework inside PlatformIO

Do not require Arduino IDE.

The first version should prioritize a simple PlatformIO + Arduino framework project. ESP-IDF may be considered later if the project needs more advanced display performance, lower-level power control, or more complex multitasking.

## Suggested Project Structure

```text
lego-clawd/
├─ platformio.ini
├─ README.md
├─ src/
│  ├─ main.cpp
│  ├─ app_state.h
│  ├─ display_ui.cpp
│  ├─ display_ui.h
│  ├─ servo_arm.cpp
│  ├─ servo_arm.h
│  ├─ usage_data.cpp
│  ├─ usage_data.h
│  ├─ wifi_manager.cpp
│  └─ wifi_manager.h
├─ include/
│  └─ config.h
├─ data/
│  ├─ sample_usage.json
│  └─ assets/
└─ docs/
   ├─ hardware.md
   ├─ wiring.md
   └─ ui.md
```

## PlatformIO Setup

Initial `platformio.ini` target:

```ini
[env:waveshare_esp32_s3_lcd_1_9]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

monitor_speed = 115200
upload_speed = 921600

board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_build.psram_type = opi
board_build.partitions = huge_app.csv

lib_deps =
  madhephaestus/ESP32Servo
  bblanchon/ArduinoJson
```

The LCD driver should be added after the official Waveshare display demo is validated. Prefer starting from the Waveshare example code for the ESP32-S3-LCD-1.9 and then migrating the display initialization into this PlatformIO project.

## First Milestones

### Milestone 1: Boot Test

Goal:

- compile and upload from PlatformIO
- serial monitor works
- board prints boot messages every second

Expected output:

```text
Lego Clawd boot OK
```

### Milestone 2: Servo Test

Goal:

- control the side servo with ESP32Servo
- move between three safe angles
- confirm power is stable

Suggested test sequence:

- 90°
- 40°
- 120°
- 90°

### Milestone 3: LCD Test

Goal:

- run the Waveshare LCD initialization
- display a simple black screen with white text:

```text
lego-clawd
LCD OK
```

### Milestone 4: Basic UI

Goal:

Create three display modes:

1. Face mode
2. Codex 5h usage mode
3. Codex 1w usage mode

Use sample/mock data first.

## Runtime Status Input

The firmware accepts one JSON object per line over USB serial. This keeps the
ESP32 firmware independent from the host-side tool that collects real Codex
usage and agent state.

Example:

```json
{"codex5h":76,"codex1w":58,"reset5h":"18:30","reset1w":"Mon 09:00","waiting":false}
```

Supported keys:

- `codex5h` or `fiveHourRemainingPct`: remaining 5-hour quota percent
- `codex1w` or `oneWeekRemainingPct`: remaining 1-week quota percent
- `reset5h` or `fiveHourResetAt`: display text for the 5-hour reset time
- `reset1w` or `oneWeekResetAt`: display text for the 1-week reset time
- `waiting` or `aiWaitingForInput`: when `true`, the servo arm raises

Without serial input, the firmware displays built-in mock usage data.

### CodexBar Bridge

Codex usage can be read from the CodexBar JSON file generated by Scriptable:

```text
~/Library/Mobile Documents/iCloud~dk~simonbs~Scriptable/Documents/codexbar-usage.json
```

Run the host bridge while the ESP32 is connected over USB:

```sh
~/.platformio/penv/bin/python tools/codexbar_bridge.py
```

The bridge reads the `codex` provider and maps:

- `primary.leftPercent` to `codex5h`
- `secondary.leftPercent` to `codex1w`
- `primary.resetsAt` to `reset5h`
- `secondary.resetsAt` to `reset1w`

To inspect the converted payload without serial:

```sh
~/.platformio/penv/bin/python tools/codexbar_bridge.py --dry-run --once
```

To force the arm raised while testing:

```sh
~/.platformio/penv/bin/python tools/codexbar_bridge.py --waiting true
```

### Milestone 5: Usage Data Integration

Goal:

- read usage data from local sample JSON first
- later replace with real Codex usage source
- update screen periodically
- trigger servo movement based on usage thresholds

## Servo Behavior Mapping

Initial behavior proposal:

| State | Screen | Arm Behavior |
|---|---|---|
| Idle | pixel face | arm rests near neutral |
| Working | Codex usage screen | slight movement every few minutes |
| Usage 60-80% | yellow status | one slow wave |
| Usage >80% | warning status | two quick waves |
| Usage >95% | limit warning | arm stays raised / repeated alert |
| Refresh success | happy face | small flap |
| Error | sad face | short shake |

## UI Data Model

Initial mock usage JSON:

```json
{
  "codex": {
    "five_hour": {
      "used": 12480,
      "limit": 18000,
      "percent": 68,
      "reset_in_minutes": 132
    },
    "weekly": {
      "used": 42000,
      "limit": 100000,
      "percent": 42
    }
  },
  "status": {
    "wifi": true,
    "last_updated": "10:30",
    "mode": "running"
  }
}
```

## Codex Development Instructions

When using Codex to implement this project, work in small hardware-validation steps:

1. Create minimal PlatformIO project.
2. Verify serial output.
3. Add servo test only.
4. Add LCD test only.
5. Add simple UI with mock data.
6. Add usage data parsing.
7. Add Wi-Fi / data source integration.
8. Add arm behavior mapping.
9. Refactor modules only after the hardware works.

Avoid implementing LCD, Wi-Fi, API, servo, OTA, and UI all in one step.

## Safety Notes

- Do not connect servo power to 3.3V.
- Use external 5V-6V power for servo if movement causes reset or flicker.
- Always connect servo power GND to ESP32 GND.
- Keep servo motion range conservative until the mechanical arm is tested.
- Avoid forcing the LEGO arm if the servo stalls.
- If the servo jitters, check power first before changing code.

## Current Status

Repository created.

Next task:

- initialize VS Code + PlatformIO project
- add minimal boot test
- then add servo test
