# Lego Clawd Agent Notes

## Project

PlatformIO Arduino project for an ESP32-S3 LEGO companion with a Waveshare 1.9" LCD and one servo arm. Use VS Code + PlatformIO, not Arduino IDE.

## Firmware

- Main firmware entry: `src/main.cpp`.
- Hardware/config constants: `include/config.h`.
- LCD rendering: `src/display_ui.*`.
- Serial JSON parsing: `src/usage_data.*`.
- Servo motion: `src/servo_arm.*`.
- Activity states are `idle`, `working`, `pending`, and `waiting`.
- `working`: focused eyes, low-frequency blink, local-only brow animation, servo small swing.
- `pending`: approval requested, wide eyes, servo raised.
- `waiting`: task complete and waiting for user input, servo raised.
- `idle`: face animations, occasional doze expression, servo down.
- Usage screen is firmware-timed; bridge only updates the cached data over serial.

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

- Codex hooks are installed by `tools/install_codex_hooks.py`; hook script is `tools/codex_status_hook.py`.
- `PermissionRequest` must map to `pending`, not `waiting`.

## Common Commands

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101
./tools/run-bridge.sh --dry-run --once
```

If upload fails, check whether the bridge owns the serial port:

```sh
lsof /dev/cu.usbmodem101
```
