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

void DisplayUi::renderFace(EyeExpression expression, AiActivity activity,
                           int16_t idleInSeconds) {
  if (activity == AiActivity::Working && expression != EyeExpression::Blink &&
      expression != EyeExpression::Strain) {
    expression = EyeExpression::Focused;
  } else if (activity == AiActivity::Pending && expression != EyeExpression::Blink) {
    expression = EyeExpression::Wide;
  }

  gfx->fillScreen(faceBackground());

  drawEye(24, 52, 64, 58, expression, true);
  drawEye(232, 52, 64, 58, expression, false);
  drawDebugState(activity, idleInSeconds);
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

void DisplayUi::drawDebugState(AiActivity activity, int16_t idleInSeconds) {
  char label[32] = "IDLE";
  if (activity == AiActivity::Working) {
    snprintf(label, sizeof(label), "WORKING");
  } else if (activity == AiActivity::Pending) {
    snprintf(label, sizeof(label), "PENDING");
  } else if (activity == AiActivity::Waiting) {
    if (idleInSeconds >= 0) {
      snprintf(label, sizeof(label), "WAITING -> IDLE %ds", idleInSeconds);
    } else {
      snprintf(label, sizeof(label), "WAITING");
    }
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
