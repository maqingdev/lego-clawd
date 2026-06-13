#pragma once

#include <Arduino.h>
#include "app_state.h"

enum class EyeExpression : uint8_t {
  Neutral,
  Blink,
  Happy,
  Sleepy,
  Focused,
  Strain,
  Wide,
  LookLeft,
  LookRight,
};

class DisplayUi {
 public:
  bool begin();
  void renderFace(EyeExpression expression, AiActivity activity, int16_t idleInSeconds);
  void renderUsageSummary(const UsageWindow &codex5h, const UsageWindow &codex1w,
                          AiActivity activity, int16_t idleInSeconds);

 private:
  uint16_t rgb(uint8_t red, uint8_t green, uint8_t blue) const;
  uint16_t faceBackground() const;
  void drawEye(int16_t x, int16_t y, int16_t w, int16_t h, EyeExpression expression,
               bool leftEye);
  void drawDebugState(AiActivity activity, int16_t idleInSeconds);
  void drawUsageBlock(int16_t x, const char *label, const UsageWindow &window);
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t percent);
  uint16_t percentColor(uint8_t percent) const;
};
