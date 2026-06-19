# Lego Clawd Agent Notes

## Project

PlatformIO Arduino project for an ESP32-S3 LEGO companion with a Waveshare
1.9" LCD and one servo arm. Use VS Code + PlatformIO, not Arduino IDE.

## Firmware

- Main firmware entry: `src/main.cpp`.
- Hardware/config constants: `include/config.h`.
- LCD rendering: `src/display_ui.*`.
- Serial JSON parsing: `src/usage_data.*`.
- Servo motion: `src/servo_arm.*`.
- Shared state: `src/app_state.h`.
- Activity states: `idle`, `working`, `pending`, and `waiting`.
- `idle`: face animations, occasional usage peeks, servo resting pose.
- `working`: focused eyes, low-frequency blink, local-only brow animation,
  servo slow front sweep.
- `pending`: approval requested, wide eyes with animated attention mark and
  `APPROVAL` label, servo raised with short wave.
- `waiting`: task complete and waiting for user input, `DONE` label, servo
  raised.
- Usage screen is firmware-timed; bridge only updates cached data over serial.

## Servo

- Servo signal pin: `GPIO42`.
- Current calibrated landmarks:
  - `1000us`: vertical up
  - `1675us`: straight forward
  - `2300us`: vertical down
- Current state mapping:
  - `idle`: `2200us`
  - `working`: slow sweep from `1600us` to `1750us`
  - `pending`: wave between `1000us` and `1150us`
  - `waiting`: `1000us`
- Servo commands use pulse widths, not degree angles.
- Manual calibration JSON:

```json
{"servoPulseUs":1675}
```

- Runtime pending wave tuning JSON:

```json
{"pendingWaveForwardPulseUs":1150,"pendingWavePauseMs":300}
```

- Servo-only PlatformIO environment: `servo_gpio42_test`.
- Avoid servo signal on `GPIO47`/`GPIO48` (IMU I2C), `GPIO19`/`GPIO20` (USB),
  `GPIO39` (SD card), `GPIO9`-`GPIO14` (LCD), and boot/strapping pins.
- All grounds must be common. Do not power servo from ESP32 `3V3`.
- A USB-C to USB-C cable is currently stable; an earlier USB-A to USB-C cable
  caused unreliable servo power.

## Host Bridge

- Start bridge from repo root with:

```sh
./tools/run-bridge.sh
```

- Bridge reads CodexBar usage from:

```text
~/Library/Mobile Documents/iCloud~dk~simonbs~Scriptable/Documents/codexbar-usage.json
```

- Bridge reads AI state from:

```text
~/.lego-clawd/ai-status.json
```

- Codex hooks are installed by `tools/install_codex_hooks.py`; hook script is
  `tools/codex_status_hook.py`.
- `PermissionRequest` must map to `pending`, not `waiting`.
- Bridge self-test command:

```sh
./tools/run-bridge.sh --once --self-test
```

This sends latest usage data and `selfTest: true`.
- Useful bridge discovery/test commands:

```sh
./tools/run-bridge.sh --help
./tools/run-bridge.sh --list-states
./tools/run-bridge.sh --once --state pending
./tools/run-bridge.sh --approval-test 15
```

## Firmware Self-Test

Serial JSON:

```json
{"selfTest":true}
```

Preferred command, because it includes latest usage data:

```sh
./tools/run-bridge.sh --once --self-test
```

Sequence:

```text
idle -> working -> pending -> waiting -> usage screen -> idle
```

## Common Commands

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101
~/.platformio/penv/bin/pio run -e servo_gpio42_test -t upload --upload-port /dev/cu.usbmodem101
./tools/run-bridge.sh --dry-run --once
./tools/run-bridge.sh --list-states
./tools/run-bridge.sh --once --state pending
./tools/run-bridge.sh --approval-test 15
./tools/run-bridge.sh --once --self-test
```

If upload fails, check whether the bridge owns the serial port:

```sh
lsof /dev/cu.usbmodem101
```
