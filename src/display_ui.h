#pragma once

#include <Arduino.h>
#include "app_state.h"

enum class EyeExpression : uint8_t {
  Neutral,
  Blink,
  Happy,
  Sleepy,
  LookLeft,
  LookRight,
};

class DisplayUi {
 public:
  bool begin();
  void renderFace(EyeExpression expression, bool waiting);
  void renderUsage(const char *title, const UsageWindow &window, bool waiting);

 private:
  uint16_t rgb(uint8_t red, uint8_t green, uint8_t blue) const;
  void drawEye(int16_t x, int16_t y, int16_t w, int16_t h, EyeExpression expression,
               bool leftEye);
  void drawStatusPill(bool waiting);
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t percent);
  uint16_t percentColor(uint8_t percent) const;
};
