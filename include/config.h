#pragma once

#include <Arduino.h>

namespace Config {

constexpr uint32_t SerialBaud = 115200;

constexpr int LcdBacklightPin = 14;
constexpr int LcdResetPin = 9;
constexpr int LcdDcPin = 11;
constexpr int LcdCsPin = 12;
constexpr int LcdSckPin = 10;
constexpr int LcdMosiPin = 13;
constexpr uint8_t LcdRotation = 3;

constexpr int DisplayWidth = 320;
constexpr int DisplayHeight = 170;
constexpr uint32_t UsagePeekIntervalMs = 30000;
constexpr uint32_t UsagePeekDurationMs = 5000;
constexpr uint32_t EyeExpressionIntervalMinMs = 6500;
constexpr uint32_t EyeExpressionIntervalMaxMs = 12000;
constexpr uint32_t WorkingBlinkIntervalMinMs = 12000;
constexpr uint32_t WorkingBlinkIntervalMaxMs = 22000;
constexpr uint32_t WorkingStrainIntervalMs = 900;
constexpr uint32_t BlinkMs = 120;

// Change this after wiring the servo signal line to a known free GPIO.
constexpr int ServoSignalPin = 4;
constexpr int ServoDownAngle = 35;
constexpr int ServoWorkMinAngle = 50;
constexpr int ServoWorkMaxAngle = 75;
constexpr int ServoRaisedAngle = 115;
constexpr int ServoStepDegrees = 2;
constexpr uint32_t ServoStepMs = 20;
constexpr uint32_t ServoWorkPauseMs = 350;

}
