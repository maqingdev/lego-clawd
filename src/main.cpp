#include <Arduino.h>

#include "app_state.h"
#include "config.h"
#include "display_ui.h"
#include "servo_arm.h"
#include "usage_data.h"

namespace {

AppState state;
DisplayUi display;
ServoArm servoArm;
UsageData usageData;

ScreenMode screen = ScreenMode::Face;
EyeExpression eyeExpression = EyeExpression::Neutral;
uint32_t lastScreenSwitchMs = 0;
uint32_t nextEyeChangeMs = 0;
uint32_t blinkUntilMs = 0;

EyeExpression randomEyeExpression() {
  switch (random(0, 5)) {
    case 0:
      return EyeExpression::Neutral;
    case 1:
      return EyeExpression::Happy;
    case 2:
      return EyeExpression::Sleepy;
    case 3:
      return EyeExpression::LookLeft;
    default:
      return EyeExpression::LookRight;
  }
}

void scheduleEyeChange() {
  nextEyeChangeMs = millis() + random(Config::EyeExpressionIntervalMinMs,
                                      Config::EyeExpressionIntervalMaxMs);
}

void renderCurrentScreen() {
  switch (screen) {
    case ScreenMode::Face:
      display.renderFace(eyeExpression, state.aiWaitingForInput);
      break;
    case ScreenMode::Codex5h:
      display.renderUsage("Codex 5h", state.codex5h, state.aiWaitingForInput);
      break;
    case ScreenMode::Codex1w:
      display.renderUsage("Codex 1w", state.codex1w, state.aiWaitingForInput);
      break;
  }
}

void advanceScreen() {
  if (screen == ScreenMode::Face) {
    screen = ScreenMode::Codex5h;
  } else if (screen == ScreenMode::Codex5h) {
    screen = ScreenMode::Codex1w;
  } else {
    screen = ScreenMode::Face;
  }
  renderCurrentScreen();
}

}

void setup() {
  Serial.begin(Config::SerialBaud);
  delay(500);
  Serial.println("Lego Clawd boot OK");

  randomSeed(esp_random());
  usageData.begin(state);
  servoArm.begin();
  servoArm.setWaitingForInput(state.aiWaitingForInput);

  display.begin();
  renderCurrentScreen();
  lastScreenSwitchMs = millis();
  scheduleEyeChange();
}

void loop() {
  const uint32_t now = millis();

  if (usageData.readSerialUpdate(Serial, state)) {
    servoArm.setWaitingForInput(state.aiWaitingForInput);
    renderCurrentScreen();
  }

  if (now - lastScreenSwitchMs >= Config::ScreenIntervalMs) {
    lastScreenSwitchMs = now;
    advanceScreen();
  }

  if (screen == ScreenMode::Face) {
    if (blinkUntilMs > 0 && now >= blinkUntilMs) {
      blinkUntilMs = 0;
      eyeExpression = randomEyeExpression();
      renderCurrentScreen();
      scheduleEyeChange();
    } else if (blinkUntilMs == 0 && now >= nextEyeChangeMs) {
      eyeExpression = EyeExpression::Blink;
      blinkUntilMs = now + Config::BlinkMs;
      renderCurrentScreen();
    }
  }

  servoArm.update();
  delay(5);
}
