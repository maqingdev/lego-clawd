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
constexpr uint32_t UsagePeekCueMs = 1600;
constexpr uint32_t UsagePeekDurationMs = 5000;
constexpr uint32_t WaitingUsagePeekDelayMs = 5000;
constexpr uint32_t SerialDisconnectTimeoutMs = 60000;
constexpr uint32_t EyeExpressionIntervalMinMs = 6500;
constexpr uint32_t EyeExpressionIntervalMaxMs = 12000;
constexpr uint32_t IdleSleepDelayMs = 45000;
constexpr uint32_t WorkingBlinkIntervalMinMs = 12000;
constexpr uint32_t WorkingBlinkIntervalMaxMs = 22000;
constexpr uint32_t WorkingStrainIntervalMs = 900;
constexpr uint32_t WorkingDeepWorkDelayMs = 45000;
constexpr uint32_t WorkingTiredDelayMs = 120000;
constexpr uint32_t PendingAttentionIntervalMs = 700;
constexpr uint32_t BlinkMs = 120;

constexpr int ServoSignalPin = 42;
constexpr int ServoMinPulseUs = 500;
constexpr int ServoMaxPulseUs = 2500;
constexpr int ServoDownPulseUs = 2200;
constexpr int ServoQuietPulseUs = 2300;
constexpr int ServoWorkMinPulseUs = 1600;
constexpr int ServoWorkMaxPulseUs = 1750;
constexpr int ServoRaisedPulseUs = 1000;
constexpr int ServoPendingWaveForwardPulseUs = 1150;
constexpr int ServoPendingWaveStepPulseUs = 8;
constexpr uint32_t ServoPendingWaveStepMs = 25;
constexpr uint32_t ServoPendingWavePauseMs = 300;
constexpr int ServoStepPulseUs = 10;
constexpr uint32_t ServoStepMs = 25;
constexpr int ServoIdleStepPulseUs = 5;
constexpr uint32_t ServoIdleStepMs = 35;
constexpr int ServoQuietStepPulseUs = 25;
constexpr uint32_t ServoQuietStepMs = 15;
constexpr int ServoRaiseStepPulseUs = 20;
constexpr uint32_t ServoRaiseStepMs = 20;
constexpr uint32_t ServoWorkPauseMs = 1500;
constexpr uint8_t ServoWorkBurstMinMoves = 2;
constexpr uint8_t ServoWorkBurstMaxMoves = 4;
constexpr uint32_t ServoWorkRestMinMs = 2000;
constexpr uint32_t ServoWorkRestMaxMs = 3000;

}
