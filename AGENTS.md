# Lego Clawd Agent Notes

Keep this file short. Put user-facing setup, protocol, hardware behavior, and
long command explanations in `README.md`.

## Project

- PlatformIO Arduino firmware for an ESP32-S3 LEGO companion with a Waveshare
  1.9" LCD and one servo arm.
- Use VS Code + PlatformIO. Do not use Arduino IDE.
- Prefer repo-local patterns and keep hardware changes small and easy to test.

## Code Map

- Firmware entry: `src/main.cpp`
- Hardware/config constants: `include/config.h`
- LCD rendering: `src/display_ui.*`
- Serial JSON parsing: `src/usage_data.*`
- Servo motion: `src/servo_arm.*`
- Persistent ESP32 settings: `src/persistent_settings.*`
- Shared state: `src/app_state.h`
- Host bridge: `tools/codexbar_bridge.py`, `tools/run-bridge.sh`
- macOS menu bar app: `macos/LegoClawdBar`

## Firmware Rules

- Activity states are `idle`, `working`, `pending`, `waiting`, `error`, and
  `disconnected`.
- `quietMode` is persisted on the ESP32. For manual Codex/CLI commands, omit
  `--quiet-mode` unless the user explicitly asks to change the stored board
  setting; never send `--quiet-mode false` casually.
- `PermissionRequest` must map to `pending`, not `waiting`.
- Usage screen timing is firmware-owned; the bridge only sends cached usage
  data and heartbeats.
- After firmware code changes, prefer building and uploading in the same turn so
  the physical robot matches the repo. If upload is risky, unavailable, or not
  worth flashing immediately, explicitly say the firmware was not flashed.

## Servo Safety

- Servo signal pin: `GPIO42`.
- Servo commands use pulse widths, not degree angles.
- Current landmarks: `1000us` vertical up, `1675us` straight forward,
  `2300us` vertical down.
- Quiet mode moves the arm quickly to `2300us`.
- Avoid servo signal on `GPIO47`/`GPIO48` (IMU I2C), `GPIO19`/`GPIO20` (USB),
  `GPIO39` (SD card), `GPIO9`-`GPIO14` (LCD), and boot/strapping pins.
- All grounds must be common. Do not power the servo from ESP32 `3V3`.

## Command Rules

- When running PlatformIO from Codex, request escalated permissions immediately;
  PlatformIO writes under `~/.platformio` and otherwise hits sandbox cache/lock
  failures.
- If upload fails, check whether the bridge owns the serial port with
  `lsof /dev/cu.usbmodem101`.

Useful commands:

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101
~/.platformio/penv/bin/pio run -e servo_gpio42_test -t upload --upload-port /dev/cu.usbmodem101
./tools/run-bridge.sh --dry-run --once
./tools/run-bridge.sh --list-states
./tools/run-bridge.sh --once --state pending
./tools/run-bridge.sh --once --state error
./tools/run-bridge.sh --once --state disconnected
./tools/run-bridge.sh --approval-test 15
./tools/run-bridge.sh --once --self-test
./tools/run-menu-bar.sh
./tools/build-menu-bar-app.sh
```
