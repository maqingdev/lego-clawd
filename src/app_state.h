#pragma once

#include <Arduino.h>

struct UsageWindow {
  uint8_t remainingPercent = 100;
  String resetAt = "--:--";
};

enum class AiActivity : uint8_t {
  Idle,
  Working,
  Waiting,
};

struct AppState {
  UsageWindow codex5h;
  UsageWindow codex1w;
  AiActivity aiActivity = AiActivity::Idle;
  bool aiWaitingForInput = false;
  uint32_t lastUpdateMs = 0;
};

enum class ScreenMode : uint8_t {
  Face = 0,
  Codex5h = 1,
  Codex1w = 2,
};
