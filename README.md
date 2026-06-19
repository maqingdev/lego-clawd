# lego-clawd

LEGO Clawd is a small ESP32-S3 desktop companion built around a Waveshare
ESP32-S3-LCD-1.9 board, a 1.9-inch LCD, and one LEGO-compatible servo arm. It
shows Codex usage and agent state on the screen, while the arm gives physical
feedback for idle, working, approval, and waiting states.

Use VS Code + PlatformIO. Do not use Arduino IDE for this project.

## Hardware

Main board:

- Waveshare ESP32-S3-LCD-1.9, non-touch variant
- Product page: https://docs.waveshare.net/ESP32-S3-LCD-1.9/?variant=ESP32-S3-LCD-1.9
- USB-C for upload, serial, and board power
- 170 x 320 LCD
- QMI8658 IMU
- Micro SD slot
- WS2812 RGB LED on the back side
- Pico-compatible GPIO header

Servo:

- LEGO-compatible AP3009-06 / JM 9g style servo
- 3-wire servo control
- Current signal pin: `GPIO42`
- Current wiring:

```text
ESP32 GPIO42 -> servo signal
5V supply +   -> servo red wire
5V supply -   -> servo brown wire
ESP32 GND     -> same 5V supply - / common ground
```

All grounds must be common. Do not power the servo from ESP32 `3V3`.

Power note from bring-up: a USB-A to USB-C cable caused unreliable servo power.
A USB-C to USB-C cable is currently stable for board upload/reset and servo
power.

Avoid these GPIOs for the servo signal:

- `GPIO47` / `GPIO48`: shared with the IMU I2C bus
- `GPIO19` / `GPIO20`: USB pins
- `GPIO39`: SD card interface, only use intentionally if SD is unused
- `GPIO9`-`GPIO14`: LCD pins
- `GPIO0`, `GPIO45`, `GPIO46`: boot/strapping related

## Firmware Layout

- `src/main.cpp`: firmware entry, screen schedule, face animation, self-test
- `include/config.h`: hardware pins and timing constants
- `src/display_ui.*`: LCD rendering
- `src/usage_data.*`: serial JSON parsing
- `src/servo_arm.*`: servo movement
- `src/persistent_settings.*`: ESP32 NVS-backed settings such as quiet mode
- `src/app_state.h`: shared app state
- `tools/codexbar_bridge.py`: host bridge from CodexBar usage JSON to serial
- `tools/run-bridge.sh`: wrapper that finds the serial port and runs the bridge
- `tools/codex_status_hook.py`: Codex hook state writer
- `tools/install_codex_hooks.py`: installs Codex hook config

## Servo Calibration

The firmware drives the servo with explicit pulse widths instead of degree
angles. Tune these values in `include/config.h`.

Current calibrated landmarks with the arm mounted in the LEGO shell:

```text
1000us = vertical up
1675us = straight forward
2300us = vertical down
```

Current state mapping:

```text
idle      -> 2200us resting pose, down and slightly forward
working   -> slow sweep from 1600us to 1750us around the forward pose
pending   -> wave between 1000us and 1150us for approval requests
waiting   -> 1000us raised hand
waiting -> idle lowers the arm back to 2200us
error     -> 2200us resting pose
disconnected -> 2200us resting pose
```

Manual calibration over serial:

```json
{"servoPulseUs":1675}
```

When `servoPulseUs` is present, the LCD footer shows the current pulse width.
The firmware constrains calibration pulses to `500us` through `2500us`.

Minimal servo-only test firmware:

```sh
~/.platformio/penv/bin/pio run -e servo_gpio42_test -t upload --upload-port /dev/cu.usbmodem101
```

The test firmware does not initialize the LCD, so the screen is expected to be
black while it continuously cycles servo pulse widths.

## Runtime Serial Protocol

The firmware accepts one JSON object per line over USB serial.

Example:

```json
{"codex5h":89,"codex1w":91,"reset5h":"Jun 14 02:17","reset1w":"Jun 18 10:43","aiState":"working"}
```

Supported keys:

- `codex5h` or `fiveHourRemainingPct`: remaining 5-hour quota percent
- `codex1w` or `oneWeekRemainingPct`: remaining 1-week quota percent
- `reset5h` or `fiveHourResetAt`: display text for 5-hour reset
- `reset1w` or `oneWeekResetAt`: display text for 1-week reset
- `aiState` or `state`: `idle`, `working`, `pending`, `waiting`, `error`, or
  `disconnected`
- `waiting` or `aiWaitingForInput`: legacy boolean; `true` maps to `waiting`
- `servoPulseUs`: manual servo calibration override
- `pendingWaveForwardPulseUs` or `pendingWavePulseUs`: temporary pending wave
  forward endpoint override
- `pendingWavePauseMs`: temporary pending wave endpoint pause override
- `quietMode` or `quiet`: suppress servo motion while keeping LCD updates active
- `selfTest`: when `true`, runs the end-to-end firmware self-test

Activity behavior:

| State | Screen behavior | Servo behavior |
|---|---|---|
| `idle` | face animations and a visible mini usage cue before occasional usage peeks | resting pose |
| `working` | focused eyes, low-frequency blink, brow animation, elapsed/deep-work footer | slow front sweep |
| `pending` | wide eyes, animated attention mark, and `APPROVAL` label | raised hand with a short wave |
| `waiting` | `DONE` label, then usage peek | raised hand |
| `error` | normal orange face with X eyes | resting pose |
| `disconnected` | low horizontal eyes with a `DISCONNECTED` label | resting pose |

When `quietMode` is true, the LCD still reflects the current state with a small
quiet icon in the footer. The servo suppresses activity motion and moves quickly
to a lower/back resting pose. Quiet mode is persisted on the ESP32 and restored
after reset until `quietMode:false` is received.

The usage screen is firmware-timed. The bridge only updates cached usage data
over serial. If the firmware does not receive a serial update for 60 seconds, it
switches to `disconnected`; the bridge sends a 10-second heartbeat while running
so a stable unchanged state does not look disconnected.

## Host Bridge

Run the bridge from the repo root:

```sh
./tools/run-bridge.sh
```

The bridge reads CodexBar usage from:

```text
~/Library/Mobile Documents/iCloud~dk~simonbs~Scriptable/Documents/codexbar-usage.json
```

It maps the `codex` provider:

- `primary.leftPercent` -> `codex5h`
- `secondary.leftPercent` -> `codex1w`
- `primary.resetsAt` -> `reset5h`
- `secondary.resetsAt` -> `reset1w`

The bridge reads AI state from:

```text
~/.lego-clawd/ai-status.json
```

## macOS Menu Bar App

A lightweight native menu bar controller is available under
`macos/LegoClawdBar`. It shows board connection, AI state, and bridge status,
and can run common hardware actions without opening a terminal.

Run from source:

```sh
./tools/run-menu-bar.sh
```

Build a double-clickable app bundle:

```sh
./tools/build-menu-bar-app.sh
```

The app bundle is written to:

```text
macos/build/Lego Clawd Bar.app
```

Menu actions:

- Connect Bridge / Disconnect Bridge
- Quiet Mode
- Test Idle, Working, Approval, Done, and Error
- Self-Test

`Disconnect Bridge` stops the background bridge, sends one final `disconnected`
state to the board, and leaves serial released.
`Test Approval` holds approval for 6 seconds, then returns the firmware to idle.
When tests run while bridge is active, the app pauses bridge, sends the test, and
restarts bridge with the current quiet mode.

The app uses the repo root from `LEGO_CLAWD_PROJECT_ROOT` when present. If the
environment variable is not set, it falls back to this iCloud project path:

```text
~/Library/Mobile Documents/com~apple~CloudDocs/toolkit/lego-clawd
```

If the status file is stale for more than 60 seconds, the bridge falls back to
`idle`. Use `--idle-timeout 0` to disable that fallback.

Useful bridge checks:

```sh
./tools/run-bridge.sh --help
./tools/run-bridge.sh --list-states
./tools/run-bridge.sh --dry-run --once
./tools/run-bridge.sh --once --state pending
./tools/run-bridge.sh --once --state error
./tools/run-bridge.sh --once --state disconnected
./tools/run-bridge.sh --once --quiet-mode true
./tools/run-bridge.sh --approval-test 15
./tools/run-bridge.sh --once --self-test
```

Use `--state idle|working|pending|waiting|error|disconnected` to force one activity.
`approval` is accepted as an alias for `pending`, and `done` is accepted as an
alias for `waiting`. `--approval-test SECONDS` sends `pending`, holds for the
requested time, then sends `idle`.

Pending wave tuning can be sent without recompiling:

```sh
./tools/run-bridge.sh --once --state pending --pending-wave-forward-pulse-us 1150
./tools/run-bridge.sh --approval-test 15 --pending-wave-forward-pulse-us 1125 --pending-wave-pause-ms 300
```

`--self-test` reads the latest CodexBar usage first, then sends that usage data
together with `selfTest: true`. Sending raw `{"selfTest":true}` over serial only
reuses whatever usage values are already cached on the device.

## Firmware Self-Test

Run:

```sh
./tools/run-bridge.sh --once --self-test
```

Sequence:

```text
idle -> working -> pending -> waiting -> error -> usage screen -> idle
```

Expected serial log:

```text
self-test start
self-test step 1/6
self-test step 2/6
self-test step 3/6
self-test step 4/6
self-test step 5/6
self-test step 6/6
self-test complete
```

## Codex Hooks

Install hooks with:

```sh
python3 tools/install_codex_hooks.py
```

Hook script:

```text
tools/codex_status_hook.py
```

Important mapping:

- `UserPromptSubmit`, `PreToolUse`, `PostToolUse` -> `working`
- `PermissionRequest` -> `pending`
- `Stop` -> `waiting`

`PermissionRequest` must map to `pending`, not `waiting`.

## Common Commands

Build:

```sh
~/.platformio/penv/bin/pio run
```

Upload normal firmware:

```sh
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101
```

Upload servo-only test firmware:

```sh
~/.platformio/penv/bin/pio run -e servo_gpio42_test -t upload --upload-port /dev/cu.usbmodem101
```

Find devices:

```sh
pio device list
```

Check whether another process owns the serial port:

```sh
lsof /dev/cu.usbmodem101
```

## Development Notes

- Work in small hardware-validation steps.
- Keep servo motion conservative until the mechanical arm is tested.
- If the board resets, LCD flickers, or servo jitters, check power and common
  ground before changing code.
- Avoid unrelated refactors while hardware behavior is still being tuned.
