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
uint32_t usagePeekStartedMs = 0;
uint32_t nextWorkingBlinkMs = 0;

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

void scheduleWorkingBlink() {
  nextWorkingBlinkMs = millis() + random(Config::WorkingBlinkIntervalMinMs,
                                         Config::WorkingBlinkIntervalMaxMs);
}

void renderCurrentScreen() {
  switch (screen) {
    case ScreenMode::Face:
      display.renderFace(eyeExpression, state.aiActivity, state.idleInSeconds);
      break;
    case ScreenMode::Usage:
      display.renderUsageSummary(state.codex5h, state.codex1w, state.aiActivity,
                                 state.idleInSeconds);
      break;
  }
}

void updateScreenSchedule(uint32_t now) {
  if (state.aiActivity != AiActivity::Idle) {
    if (screen != ScreenMode::Face) {
      screen = ScreenMode::Face;
      renderCurrentScreen();
    }
    return;
  }

  if (screen == ScreenMode::Usage) {
    if (now - usagePeekStartedMs >= Config::UsagePeekDurationMs) {
      screen = ScreenMode::Face;
      lastScreenSwitchMs = now;
      renderCurrentScreen();
    }
    return;
  }

  if (now - lastScreenSwitchMs >= Config::UsagePeekIntervalMs) {
    screen = ScreenMode::Usage;
    usagePeekStartedMs = now;
    renderCurrentScreen();
  }
}

void updateFaceExpression(uint32_t now) {
  if (screen != ScreenMode::Face) {
    return;
  }

  if (state.aiActivity == AiActivity::Working) {
    if (blinkUntilMs > 0 && now >= blinkUntilMs) {
      blinkUntilMs = 0;
      eyeExpression = EyeExpression::Focused;
      renderCurrentScreen();
      scheduleWorkingBlink();
    } else if (blinkUntilMs == 0 && now >= nextWorkingBlinkMs) {
      eyeExpression = EyeExpression::Blink;
      blinkUntilMs = now + Config::BlinkMs;
      renderCurrentScreen();
    } else if (blinkUntilMs == 0 && eyeExpression != EyeExpression::Focused) {
      eyeExpression = EyeExpression::Focused;
      renderCurrentScreen();
      if (nextWorkingBlinkMs == 0) {
        scheduleWorkingBlink();
      }
    }
    return;
  }

  if (state.aiActivity == AiActivity::Pending) {
    if (eyeExpression != EyeExpression::Wide) {
      eyeExpression = EyeExpression::Wide;
      blinkUntilMs = 0;
      renderCurrentScreen();
    }
    return;
  }

  if (eyeExpression == EyeExpression::Focused || eyeExpression == EyeExpression::Wide) {
    eyeExpression = EyeExpression::Neutral;
    renderCurrentScreen();
    scheduleEyeChange();
    return;
  }

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

void forceFaceScreen() {
  if (state.aiActivity != AiActivity::Idle) {
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
  servoArm.setActivity(state.aiActivity);

  display.begin();
  renderCurrentScreen();
  lastScreenSwitchMs = millis();
  scheduleEyeChange();
  scheduleWorkingBlink();
}

void loop() {
  const uint32_t now = millis();

  if (usageData.readSerialUpdate(Serial, state)) {
    servoArm.setActivity(state.aiActivity);
    forceFaceScreen();
  }

  updateScreenSchedule(now);
  updateFaceExpression(now);

  servoArm.update();
  delay(5);
}
