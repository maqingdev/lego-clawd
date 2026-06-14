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
uint32_t waitingUsagePeekAtMs = 0;
uint32_t idleStartedMs = 0;
uint32_t nextWorkingBlinkMs = 0;
uint32_t nextWorkingStrainMs = 0;
bool workingStrained = false;
bool selfTestActive = false;
uint8_t selfTestStep = 0;
uint32_t selfTestStepStartedMs = 0;

struct SelfTestStep {
  AiActivity activity;
  ScreenMode screenMode;
  EyeExpression expression;
  uint32_t durationMs;
};

constexpr SelfTestStep SelfTestSteps[] = {
    {AiActivity::Idle, ScreenMode::Face, EyeExpression::Neutral, 2000},
    {AiActivity::Working, ScreenMode::Face, EyeExpression::Focused, 8000},
    {AiActivity::Pending, ScreenMode::Face, EyeExpression::Wide, 3000},
    {AiActivity::Waiting, ScreenMode::Face, EyeExpression::Neutral, 2000},
    {AiActivity::Waiting, ScreenMode::Usage, EyeExpression::Neutral, 4000},
    {AiActivity::Idle, ScreenMode::Face, EyeExpression::Neutral, 3000},
};

constexpr uint8_t SelfTestStepCount = sizeof(SelfTestSteps) / sizeof(SelfTestSteps[0]);

EyeExpression randomEyeExpression(bool allowSleep) {
  switch (random(0, allowSleep ? 6 : 4)) {
    case 0:
      return EyeExpression::Neutral;
    case 1:
      return EyeExpression::Happy;
    case 2:
      return EyeExpression::LookLeft;
    case 3:
      return EyeExpression::LookRight;
    case 4:
      return EyeExpression::Sleepy;
    default:
      return EyeExpression::Doze;
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

void scheduleWorkingStrain(uint32_t now) {
  nextWorkingStrainMs = now + Config::WorkingStrainIntervalMs;
}

bool idleCanSleep(uint32_t now) {
  return idleStartedMs > 0 && now - idleStartedMs >= Config::IdleSleepDelayMs;
}

void renderCurrentScreen() {
  switch (screen) {
    case ScreenMode::Face:
      display.renderFace(eyeExpression, state);
      break;
    case ScreenMode::Usage:
      display.renderUsageSummary(state.codex5h, state.codex1w, state.aiActivity,
                                 state.idleInSeconds);
      break;
  }
}

void updateScreenSchedule(uint32_t now) {
  if (selfTestActive) {
    return;
  }

  if (state.servoPulseUs >= 0) {
    if (screen != ScreenMode::Face) {
      screen = ScreenMode::Face;
      renderCurrentScreen();
    }
    return;
  }

  if (state.aiActivity != AiActivity::Idle) {
    if (state.aiActivity == AiActivity::Waiting) {
      if (screen == ScreenMode::Usage) {
        if (now - usagePeekStartedMs >= Config::UsagePeekDurationMs) {
          screen = ScreenMode::Face;
          renderCurrentScreen();
        }
        return;
      }

      if (waitingUsagePeekAtMs > 0 &&
          static_cast<int32_t>(now - waitingUsagePeekAtMs) >= 0) {
        waitingUsagePeekAtMs = 0;
        screen = ScreenMode::Usage;
        usagePeekStartedMs = now;
        renderCurrentScreen();
        return;
      }
    } else {
      waitingUsagePeekAtMs = 0;
    }

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
      eyeExpression = workingStrained ? EyeExpression::Strain : EyeExpression::Focused;
      renderCurrentScreen();
      scheduleWorkingBlink();
    } else if (blinkUntilMs == 0 && now >= nextWorkingBlinkMs) {
      eyeExpression = EyeExpression::Blink;
      blinkUntilMs = now + Config::BlinkMs;
      renderCurrentScreen();
    } else if (blinkUntilMs == 0 && now >= nextWorkingStrainMs) {
      const bool canUpdateBrowsOnly = eyeExpression == EyeExpression::Focused ||
                                      eyeExpression == EyeExpression::Strain;
      workingStrained = !workingStrained;
      eyeExpression = workingStrained ? EyeExpression::Strain : EyeExpression::Focused;
      if (canUpdateBrowsOnly) {
        display.renderWorkingBrows(eyeExpression);
      } else {
        renderCurrentScreen();
      }
      scheduleWorkingStrain(now);
    } else if (blinkUntilMs == 0 && eyeExpression != EyeExpression::Focused &&
               eyeExpression != EyeExpression::Strain) {
      eyeExpression = workingStrained ? EyeExpression::Strain : EyeExpression::Focused;
      renderCurrentScreen();
      if (nextWorkingStrainMs == 0) {
        scheduleWorkingStrain(now);
      }
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

  if (state.aiActivity == AiActivity::Waiting) {
    if (eyeExpression != EyeExpression::Neutral) {
      eyeExpression = EyeExpression::Neutral;
      blinkUntilMs = 0;
      renderCurrentScreen();
    }
    return;
  }

  if (eyeExpression == EyeExpression::Focused || eyeExpression == EyeExpression::Strain ||
      eyeExpression == EyeExpression::Wide ||
      (!idleCanSleep(now) && (eyeExpression == EyeExpression::Sleepy ||
                              eyeExpression == EyeExpression::Doze))) {
    eyeExpression = EyeExpression::Neutral;
    renderCurrentScreen();
    scheduleEyeChange();
    return;
  }

  if (blinkUntilMs > 0 && now >= blinkUntilMs) {
    blinkUntilMs = 0;
    eyeExpression = randomEyeExpression(idleCanSleep(now));
    renderCurrentScreen();
    scheduleEyeChange();
  } else if (blinkUntilMs == 0 && now >= nextEyeChangeMs) {
    eyeExpression = EyeExpression::Blink;
    blinkUntilMs = now + Config::BlinkMs;
    renderCurrentScreen();
  }
}

void forceFaceScreen() {
  if (state.aiActivity != AiActivity::Idle || state.servoPulseUs >= 0) {
    screen = ScreenMode::Face;
  }
  renderCurrentScreen();
}

void handleActivityTransition(AiActivity previousActivity, AiActivity currentActivity,
                              uint32_t now) {
  if (previousActivity == currentActivity) {
    return;
  }

  if (currentActivity == AiActivity::Idle) {
    idleStartedMs = now;
    eyeExpression = EyeExpression::Neutral;
    blinkUntilMs = 0;
    scheduleEyeChange();
    return;
  }

  idleStartedMs = 0;
  blinkUntilMs = 0;
  if (currentActivity == AiActivity::Working) {
    workingStrained = false;
    eyeExpression = EyeExpression::Focused;
    scheduleWorkingBlink();
    scheduleWorkingStrain(now);
  } else if (currentActivity == AiActivity::Pending) {
    eyeExpression = EyeExpression::Wide;
  } else if (currentActivity == AiActivity::Waiting) {
    eyeExpression = EyeExpression::Neutral;
  }
}

void applySelfTestStep() {
  const SelfTestStep &step = SelfTestSteps[selfTestStep];
  const uint32_t now = millis();
  const AiActivity previousActivity = state.aiActivity;
  state.aiActivity = step.activity;
  state.aiWaitingForInput = step.activity == AiActivity::Waiting;
  state.servoPulseUs = -1;
  screen = step.screenMode;
  eyeExpression = step.expression;
  blinkUntilMs = 0;
  handleActivityTransition(previousActivity, state.aiActivity, now);
  if (previousActivity == state.aiActivity && state.aiActivity == AiActivity::Idle) {
    idleStartedMs = now;
  }
  eyeExpression = step.expression;
  servoArm.setActivity(state.aiActivity);
  renderCurrentScreen();
  Serial.print("self-test step ");
  Serial.print(selfTestStep + 1);
  Serial.print("/");
  Serial.println(SelfTestStepCount);
}

void startSelfTest(uint32_t now) {
  selfTestActive = true;
  selfTestStep = 0;
  selfTestStepStartedMs = now;
  waitingUsagePeekAtMs = 0;
  usagePeekStartedMs = 0;
  Serial.println("self-test start");
  applySelfTestStep();
}

void updateSelfTest(uint32_t now) {
  if (!selfTestActive) {
    return;
  }

  if (now - selfTestStepStartedMs < SelfTestSteps[selfTestStep].durationMs) {
    return;
  }

  ++selfTestStep;
  if (selfTestStep >= SelfTestStepCount) {
    selfTestActive = false;
    selfTestStep = 0;
    state.aiActivity = AiActivity::Idle;
    state.aiWaitingForInput = false;
    state.servoPulseUs = -1;
    screen = ScreenMode::Face;
    eyeExpression = EyeExpression::Neutral;
    idleStartedMs = now;
    servoArm.setActivity(state.aiActivity);
    renderCurrentScreen();
    lastScreenSwitchMs = now;
    scheduleEyeChange();
    Serial.println("self-test complete");
    return;
  }

  selfTestStepStartedMs = now;
  applySelfTestStep();
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
  idleStartedMs = lastScreenSwitchMs;
  scheduleEyeChange();
  scheduleWorkingBlink();
  scheduleWorkingStrain(millis());
}

void loop() {
  const uint32_t now = millis();
  const AiActivity previousActivity = state.aiActivity;
  const int16_t previousServoPulseUs = state.servoPulseUs;

  if (usageData.readSerialUpdate(Serial, state)) {
    if (state.selfTestRequested) {
      state.selfTestRequested = false;
      startSelfTest(now);
    } else {
      handleActivityTransition(previousActivity, state.aiActivity, now);
      if (previousActivity == AiActivity::Working &&
          state.aiActivity == AiActivity::Waiting) {
        waitingUsagePeekAtMs = now + Config::WaitingUsagePeekDelayMs;
      } else if (state.aiActivity != AiActivity::Waiting) {
        waitingUsagePeekAtMs = 0;
      }
      if (state.servoPulseUs >= 0) {
        if (previousServoPulseUs != state.servoPulseUs) {
          servoArm.setCalibrationPulse(state.servoPulseUs);
        }
      } else if (previousActivity != state.aiActivity || previousServoPulseUs >= 0) {
        servoArm.setActivity(state.aiActivity);
      }
      forceFaceScreen();
    }
  }

  updateSelfTest(now);
  updateScreenSchedule(now);
  updateFaceExpression(now);

  servoArm.update();
  delay(5);
}
