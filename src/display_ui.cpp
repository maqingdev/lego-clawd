#include "display_ui.h"

#include <Arduino_GFX_Library.h>
#include <cstdio>
#include <cstring>
#include "config.h"

namespace {

Arduino_DataBus *bus = new Arduino_HWSPI(
    Config::LcdDcPin,
    Config::LcdCsPin,
    Config::LcdSckPin,
    Config::LcdMosiPin);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    Config::LcdResetPin,
    Config::LcdRotation,
    true,
    170,
    320,
    35,
    0,
    35,
    0);

constexpr uint16_t Black = 0x0000;
}

bool DisplayUi::begin() {
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
    return false;
  }

  pinMode(Config::LcdBacklightPin, OUTPUT);
  digitalWrite(Config::LcdBacklightPin, LOW);
  gfx->fillScreen(Black);
  return true;
}

void DisplayUi::renderFace(EyeExpression expression, const AppState &state) {
  if (state.aiActivity == AiActivity::Error) {
    drawErrorFace();
    drawFooter(state);
    return;
  }

  if (state.aiActivity == AiActivity::Disconnected) {
    drawDisconnectedFace();
    drawFooter(state);
    return;
  }

  if (state.aiActivity == AiActivity::Working && expression != EyeExpression::Blink &&
      expression != EyeExpression::Strain) {
    expression = EyeExpression::Focused;
  } else if (state.aiActivity == AiActivity::Pending && expression != EyeExpression::Blink) {
    expression = EyeExpression::Wide;
  }

  gfx->fillScreen(faceBackground());

  drawEye(24, 52, 64, 58, expression, true);
  drawEye(232, 52, 64, 58, expression, false);
  if (expression == EyeExpression::Doze) {
    drawDozeMarks();
  }
  drawFooter(state);
}

void DisplayUi::renderWorkingBrows(EyeExpression expression) {
  if (expression != EyeExpression::Strain) {
    expression = EyeExpression::Focused;
  }

  clearWorkingBrow(24, 52, 64);
  clearWorkingBrow(232, 52, 64);
  drawWorkingBrow(24, 52, 64, expression, true);
  drawWorkingBrow(232, 52, 64, expression, false);
}

void DisplayUi::renderPendingAttention(bool active) {
  clearApprovalMarks();
  if (active) {
    drawApprovalMarks();
  }
}

void DisplayUi::renderUsagePeekCue(const AppState &state) {
  gfx->fillScreen(faceBackground());
  drawEye(18, 52, 64, 58, EyeExpression::LookRight, true);
  drawEye(202, 52, 64, 58, EyeExpression::LookRight, false);
  drawUsageCuePanel();
  drawFooter(state);
}

void DisplayUi::renderUsageSummary(const UsageWindow &codex5h, const UsageWindow &codex1w,
                                   AiActivity activity, int16_t idleInSeconds) {
  gfx->fillScreen(faceBackground());
  gfx->setTextColor(Black);
  gfx->setTextSize(2);
  gfx->setCursor(124, 12);
  gfx->print("USAGE");

  drawUsageBlock(22, "5H", codex5h);
  drawUsageBlock(176, "1W", codex1w);
  drawDebugState(activity, idleInSeconds);
}

void DisplayUi::renderFooter(const AppState &state) {
  drawFooter(state);
}

uint16_t DisplayUi::rgb(uint8_t red, uint8_t green, uint8_t blue) const {
  return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

uint16_t DisplayUi::faceBackground() const {
  return rgb(244, 164, 10);
}

void DisplayUi::drawEye(int16_t x, int16_t y, int16_t w, int16_t h,
                        EyeExpression expression, bool leftEye) {
  int16_t offsetX = 0;
  int16_t eyeH = h;
  int16_t eyeY = y;

  if (expression == EyeExpression::Blink) {
    gfx->fillRoundRect(x, y + h / 2 - 5, w, 10, 4, Black);
    return;
  }

  if (expression == EyeExpression::Sleepy) {
    eyeH = 24;
    eyeY = y + 18;
  } else if (expression == EyeExpression::Doze) {
    eyeH = 12;
    eyeY = y + 28;
  } else if (expression == EyeExpression::Focused) {
    eyeH = 34;
    eyeY = y + 18;
  } else if (expression == EyeExpression::Strain) {
    eyeH = 34;
    eyeY = y + 18;
  } else if (expression == EyeExpression::Wide) {
    eyeH = 70;
    eyeY = y - 6;
  } else if (expression == EyeExpression::Happy) {
    eyeH = 38;
    eyeY = y + 8;
  } else if (expression == EyeExpression::LookLeft) {
    offsetX = leftEye ? -10 : -8;
  } else if (expression == EyeExpression::LookRight) {
    offsetX = leftEye ? 8 : 10;
  }

  gfx->fillRoundRect(x + offsetX, eyeY, w, eyeH, 8, Black);

  if (expression == EyeExpression::Focused || expression == EyeExpression::Strain) {
    drawWorkingBrow(x, y, w, expression, leftEye);
  }

  if (expression == EyeExpression::Happy) {
    gfx->fillRect(x + offsetX, eyeY, w, 10, Black);
  }
}

void DisplayUi::clearWorkingBrow(int16_t x, int16_t y, int16_t w) {
  gfx->fillRect(x - 6, y - 18, w + 12, 28, faceBackground());
}

void DisplayUi::drawWorkingBrow(int16_t x, int16_t y, int16_t w,
                                EyeExpression expression, bool leftEye) {
  for (int i = 0; i < 4; ++i) {
    if (leftEye) {
      const int16_t y1 = expression == EyeExpression::Strain ? y - 14 + i : y - 8 + i;
      const int16_t y2 = expression == EyeExpression::Strain ? y - 1 + i : y - 2 + i;
      gfx->drawLine(x, y1, x + w, y2, Black);
    } else {
      const int16_t y1 = expression == EyeExpression::Strain ? y - 1 + i : y - 2 + i;
      const int16_t y2 = expression == EyeExpression::Strain ? y - 14 + i : y - 8 + i;
      gfx->drawLine(x, y1, x + w, y2, Black);
    }
  }
}

void DisplayUi::clearApprovalMarks() {
  gfx->fillRect(112, 18, 22, 22, faceBackground());
  gfx->fillRect(154, 16, 14, 38, faceBackground());
  gfx->fillRect(186, 18, 22, 22, faceBackground());
}

void DisplayUi::drawApprovalMarks() {
  for (int i = 0; i < 4; ++i) {
    gfx->drawLine(158 + i, 18, 158 + i, 40, Black);
  }
  gfx->fillCircle(160, 49, 3, Black);

  for (int i = 0; i < 3; ++i) {
    gfx->drawLine(115, 24 + i, 132, 35 + i, Black);
    gfx->drawLine(205, 24 + i, 188, 35 + i, Black);
  }
}

void DisplayUi::drawDozeMarks() {
  gfx->setTextColor(Black);
  gfx->setTextSize(1);
  gfx->setCursor(150, 24);
  gfx->print("z");
  gfx->setTextSize(2);
  gfx->setCursor(166, 12);
  gfx->print("Z");
}

void DisplayUi::drawErrorFace() {
  gfx->fillScreen(faceBackground());
  drawErrorEye(42, 48, 54);
  drawErrorEye(224, 48, 54);
  gfx->fillRoundRect(130, 112, 60, 10, 4, Black);
}

void DisplayUi::drawErrorEye(int16_t x, int16_t y, int16_t size) {
  for (int i = 0; i < 5; ++i) {
    gfx->drawLine(x + i, y, x + size + i, y + size, Black);
    gfx->drawLine(x + size + i, y, x + i, y + size, Black);
  }
}

void DisplayUi::drawDisconnectedFace() {
  gfx->fillScreen(faceBackground());
  gfx->fillRoundRect(42, 78, 58, 10, 4, Black);
  gfx->fillRoundRect(220, 78, 58, 10, 4, Black);

  for (int i = 0; i < 2; ++i) {
    gfx->drawLine(132 + i, 64, 132 + i, 108, Black);
    gfx->drawLine(132, 64 + i, 152, 64 + i, Black);
    gfx->drawLine(132, 108 + i, 152, 108 + i, Black);

    gfx->drawLine(188 + i, 64, 188 + i, 108, Black);
    gfx->drawLine(168, 64 + i, 188, 64 + i, Black);
    gfx->drawLine(168, 108 + i, 188, 108 + i, Black);

    gfx->drawLine(158 + i, 104, 170 + i, 68, Black);
  }
}

void DisplayUi::drawUsageCuePanel() {
  const uint16_t fill = rgb(255, 190, 43);
  gfx->fillRoundRect(120, 30, 80, 86, 8, fill);
  gfx->drawRoundRect(120, 30, 80, 86, 8, Black);
  gfx->setTextColor(Black);
  gfx->setTextSize(1);
  gfx->setCursor(142, 42);
  gfx->print("USAGE");
  drawProgressBar(134, 64, 52, 10, 72);
  drawProgressBar(134, 84, 52, 10, 44);
}

void DisplayUi::drawFooter(const AppState &state) {
  char label[40] = "IDLE";
  if (state.servoPulseUs >= 0) {
    snprintf(label, sizeof(label), "SERVO %dus", state.servoPulseUs);
  } else if (state.aiActivity == AiActivity::Working) {
    workingLabel(state, label, sizeof(label));
  } else if (state.aiActivity == AiActivity::Pending) {
    snprintf(label, sizeof(label), "APPROVAL");
  } else if (state.aiActivity == AiActivity::Waiting) {
    if (state.idleInSeconds >= 0) {
      snprintf(label, sizeof(label), "DONE -> IDLE %ds", state.idleInSeconds);
    } else {
      snprintf(label, sizeof(label), "DONE");
    }
  } else if (state.aiActivity == AiActivity::Error) {
    snprintf(label, sizeof(label), "ERROR");
  } else if (state.aiActivity == AiActivity::Disconnected) {
    snprintf(label, sizeof(label), "DISCONNECTED");
  }

  char footer[56];
  snprintf(footer, sizeof(footer), "%u%% | %s", state.codex5h.remainingPercent, label);

  gfx->fillRect(0, 150, Config::DisplayWidth, 20, faceBackground());
  gfx->setTextColor(Black);
  gfx->setTextSize(1);
  const int16_t textWidth = static_cast<int16_t>(strlen(footer) * 6);
  gfx->setCursor(max<int16_t>(0, (Config::DisplayWidth - textWidth) / 2), 158);
  gfx->print(footer);

  if (state.quietMode && state.servoPulseUs < 0) {
    drawQuietIcon();
  }
}

void DisplayUi::drawQuietIcon() {
  constexpr int16_t cx = 303;
  constexpr int16_t cy = 160;
  constexpr int16_t r = 6;
  gfx->drawCircle(cx, cy, r, Black);
  gfx->drawCircle(cx, cy, r - 1, Black);
  for (int i = 0; i < 2; ++i) {
    gfx->drawLine(cx - 5, cy + 5 - i, cx + 5, cy - 5 - i, Black);
  }
}

void DisplayUi::workingLabel(const AppState &state, char *label, size_t size) {
  const int16_t elapsed = state.activityElapsedSeconds;
  if (elapsed >= static_cast<int16_t>(Config::WorkingTiredDelayMs / 1000)) {
    snprintf(label, size, "FOCUSED %dm", max<int16_t>(1, elapsed / 60));
  } else if (elapsed >= static_cast<int16_t>(Config::WorkingDeepWorkDelayMs / 1000)) {
    snprintf(label, size, "DEEP WORK %dm", max<int16_t>(1, elapsed / 60));
  } else if (elapsed >= 0) {
    snprintf(label, size, "WORKING %ds", elapsed);
  } else {
    snprintf(label, size, "WORKING");
  }
}

void DisplayUi::drawDebugState(AiActivity activity, int16_t idleInSeconds) {
  char label[32] = "IDLE";
  if (activity == AiActivity::Working) {
    snprintf(label, sizeof(label), "WORKING");
  } else if (activity == AiActivity::Pending) {
    snprintf(label, sizeof(label), "APPROVAL");
  } else if (activity == AiActivity::Waiting) {
    if (idleInSeconds >= 0) {
      snprintf(label, sizeof(label), "DONE -> IDLE %ds", idleInSeconds);
    } else {
      snprintf(label, sizeof(label), "DONE");
    }
  } else if (activity == AiActivity::Error) {
    snprintf(label, sizeof(label), "ERROR");
  } else if (activity == AiActivity::Disconnected) {
    snprintf(label, sizeof(label), "DISCONNECTED");
  }

  gfx->setTextColor(Black);
  gfx->setTextSize(1);
  const int16_t textWidth = static_cast<int16_t>(strlen(label) * 6);
  gfx->setCursor(max<int16_t>(0, (Config::DisplayWidth - textWidth) / 2), 158);
  gfx->print(label);
}

void DisplayUi::drawUsageBlock(int16_t x, const char *label, const UsageWindow &window) {
  gfx->setTextColor(Black);
  gfx->setTextSize(2);
  gfx->setCursor(x, 42);
  gfx->print(label);

  gfx->setTextSize(4);
  gfx->setCursor(x, 70);
  gfx->print(window.remainingPercent);
  gfx->print("%");

  gfx->setTextSize(1);
  gfx->setCursor(x, 116);
  gfx->print("reset ");
  gfx->print(window.resetAt);

  drawProgressBar(x, 138, 122, 12, window.remainingPercent);
}

void DisplayUi::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                                uint8_t percent) {
  gfx->drawRoundRect(x, y, w, h, 4, Black);
  const int16_t fillW = max<int16_t>(0, (w - 4) * percent / 100);
  gfx->fillRoundRect(x + 2, y + 2, fillW, h - 4, 3, percentColor(percent));
}

uint16_t DisplayUi::percentColor(uint8_t percent) const {
  if (percent >= 50) {
    return rgb(59, 214, 118);
  }
  if (percent >= 20) {
    return rgb(245, 199, 68);
  }
  return rgb(238, 82, 83);
}
