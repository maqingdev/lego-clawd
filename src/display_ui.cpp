#include "display_ui.h"

#include <Arduino_GFX_Library.h>
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
    1,
    true,
    170,
    320,
    35,
    0,
    35,
    0);

constexpr uint16_t Black = 0x0000;
constexpr uint16_t White = 0xffff;
constexpr uint16_t Dark = 0x18e3;

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

void DisplayUi::renderFace(EyeExpression expression, bool waiting) {
  const uint16_t orange = rgb(245, 126, 32);
  gfx->fillScreen(orange);

  drawEye(64, 52, 64, 58, expression, true);
  drawEye(192, 52, 64, 58, expression, false);
  drawStatusPill(waiting);
}

void DisplayUi::renderUsage(const char *title, const UsageWindow &window, bool waiting) {
  const uint16_t background = rgb(18, 18, 18);
  const uint16_t muted = rgb(168, 168, 168);
  const uint16_t accent = percentColor(window.remainingPercent);

  gfx->fillScreen(background);
  gfx->setTextColor(White);
  gfx->setTextSize(2);
  gfx->setCursor(14, 14);
  gfx->print(title);

  gfx->setTextColor(accent);
  gfx->setTextSize(5);
  gfx->setCursor(22, 52);
  gfx->print(window.remainingPercent);
  gfx->print("%");

  gfx->setTextColor(muted);
  gfx->setTextSize(2);
  gfx->setCursor(22, 114);
  gfx->print("reset ");
  gfx->print(window.resetAt);

  drawProgressBar(22, 142, 276, 14, window.remainingPercent);
  drawStatusPill(waiting);
}

uint16_t DisplayUi::rgb(uint8_t red, uint8_t green, uint8_t blue) const {
  return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
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
  } else if (expression == EyeExpression::Happy) {
    eyeH = 38;
    eyeY = y + 8;
  } else if (expression == EyeExpression::LookLeft) {
    offsetX = leftEye ? -10 : -8;
  } else if (expression == EyeExpression::LookRight) {
    offsetX = leftEye ? 8 : 10;
  }

  gfx->fillRoundRect(x + offsetX, eyeY, w, eyeH, 8, Black);

  if (expression == EyeExpression::Happy) {
    gfx->fillRect(x + offsetX, eyeY, w, 10, Black);
  }
}

void DisplayUi::drawStatusPill(bool waiting) {
  const uint16_t pill = waiting ? rgb(35, 120, 255) : rgb(45, 45, 45);
  const uint16_t text = waiting ? White : rgb(190, 190, 190);
  gfx->fillRoundRect(232, 10, 76, 22, 6, pill);
  gfx->setTextColor(text);
  gfx->setTextSize(1);
  gfx->setCursor(244, 17);
  gfx->print(waiting ? "WAITING" : "IDLE");
}

void DisplayUi::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                                uint8_t percent) {
  gfx->drawRoundRect(x, y, w, h, 4, rgb(72, 72, 72));
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
