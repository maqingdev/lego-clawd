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
constexpr uint32_t WaitingUsagePeekDelayMs = 5000;
constexpr uint32_t EyeExpressionIntervalMinMs = 6500;
constexpr uint32_t EyeExpressionIntervalMaxMs = 12000;
constexpr uint32_t WorkingBlinkIntervalMinMs = 12000;
constexpr uint32_t WorkingBlinkIntervalMaxMs = 22000;
constexpr uint32_t WorkingStrainIntervalMs = 900;
constexpr uint32_t BlinkMs = 120;

constexpr int ServoSignalPin = 18;
constexpr int ServoMinPulseUs = 500;
constexpr int ServoMaxPulseUs = 2500;
constexpr int ServoDownPulseUs = 700;
constexpr int ServoWorkMinPulseUs = 1300;
constexpr int ServoWorkMaxPulseUs = 1700;
constexpr int ServoRaisedPulseUs = 2300;
constexpr int ServoStepPulseUs = 20;
constexpr uint32_t ServoStepMs = 20;
constexpr uint32_t ServoWorkPauseMs = 350;

}
