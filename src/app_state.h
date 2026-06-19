#pragma once

#include <Arduino.h>
#include "config.h"

struct UsageWindow {
  uint8_t remainingPercent = 100;
  String resetAt = "--:--";
};

enum class AiActivity : uint8_t {
  Idle,
  Working,
  Pending,
  Waiting,
  Error,
};

struct AppState {
  UsageWindow codex5h;
  UsageWindow codex1w;
  AiActivity aiActivity = AiActivity::Idle;
  bool aiWaitingForInput = false;
  bool selfTestRequested = false;
  bool quietMode = false;
  int16_t idleInSeconds = -1;
  int16_t servoPulseUs = -1;
  int16_t pendingWaveForwardPulseUs = Config::ServoPendingWaveForwardPulseUs;
  uint16_t pendingWavePauseMs = Config::ServoPendingWavePauseMs;
  uint32_t lastUpdateMs = 0;
};

enum class ScreenMode : uint8_t {
  Face = 0,
  Usage = 1,
};
