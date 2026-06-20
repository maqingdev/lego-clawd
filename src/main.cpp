#include <Arduino.h>

#include "app_state.h"
#include "config.h"
#include "display_ui.h"
#include "persistent_settings.h"
#include "servo_arm.h"
#include "usage_data.h"

namespace {

AppState state;
DisplayUi display;
ServoArm servoArm;
UsageData usageData;
PersistentSettings settings;

ScreenMode screen = ScreenMode::Face;
EyeExpression eyeExpression = EyeExpression::Neutral;
uint32_t lastScreenSwitchMs = 0;
uint32_t nextEyeChangeMs = 0;
uint32_t blinkUntilMs = 0;
uint32_t usagePeekStartedMs = 0;
uint32_t usagePeekCueStartedMs = 0;
uint32_t waitingUsagePeekAtMs = 0;
uint32_t idleStartedMs = 0;
uint32_t activityStartedMs = 0;
uint32_t lastActivityElapsedRenderMs = 0;
uint32_t nextWorkingBlinkMs = 0;
uint32_t nextWorkingStrainMs = 0;
uint32_t nextPendingAttentionMs = 0;
bool workingStrained = false;
bool pendingAttentionActive = false;
bool usagePeekCueActive = false;
bool manualUsageActive = false;
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
    {AiActivity::Pending, ScreenMode::Face, EyeExpression::Wide, 7000},
    {AiActivity::Waiting, ScreenMode::Face, EyeExpression::Neutral, 2000},
    {AiActivity::Error, ScreenMode::Face, EyeExpression::Neutral, 2500},
    {AiActivity::Disconnected, ScreenMode::Face, EyeExpression::Neutral, 2500},
    {AiActivity::Waiting, ScreenMode::UsageCue, EyeExpression::LookRight, 1600},
    {AiActivity::Waiting, ScreenMode::Usage, EyeExpression::Neutral, 4000},
    {AiActivity::Idle, ScreenMode::Face, EyeExpression::Neutral, 3000},
};

constexpr uint8_t SelfTestStepCount = sizeof(SelfTestSteps) / sizeof(SelfTestSteps[0]);

void renderCurrentScreen();

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

void startUsagePeekCue(uint32_t now) {
  manualUsageActive = false;
  usagePeekCueActive = true;
  usagePeekCueStartedMs = now;
  eyeExpression = EyeExpression::LookRight;
  screen = ScreenMode::UsageCue;
  blinkUntilMs = 0;
  renderCurrentScreen();
}

void showUsageSummary(uint32_t now) {
  manualUsageActive = true;
  usagePeekCueActive = false;
  waitingUsagePeekAtMs = 0;
  usagePeekStartedMs = now;
  screen = ScreenMode::Usage;
  renderCurrentScreen();
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
    case ScreenMode::UsageCue:
      display.renderUsagePeekCue(state);
      break;
  }
}

void updateScreenSchedule(uint32_t now) {
  if (selfTestActive) {
    return;
  }

  if (state.servoPulseUs >= 0) {
    manualUsageActive = false;
    usagePeekCueActive = false;
    if (screen != ScreenMode::Face) {
      screen = ScreenMode::Face;
      renderCurrentScreen();
    }
    return;
  }

  if (manualUsageActive) {
    if (now - usagePeekStartedMs >= Config::UsagePeekDurationMs) {
      manualUsageActive = false;
      screen = ScreenMode::Face;
      lastScreenSwitchMs = now;
      renderCurrentScreen();
    }
    return;
  }

  if (usagePeekCueActive) {
    if (now - usagePeekCueStartedMs >= Config::UsagePeekCueMs) {
      usagePeekCueActive = false;
      screen = ScreenMode::Usage;
      usagePeekStartedMs = now;
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
        startUsagePeekCue(now);
        return;
      }
    } else {
      waitingUsagePeekAtMs = 0;
    }
    usagePeekCueActive = false;

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
    startUsagePeekCue(now);
  }
}

void updateFaceExpression(uint32_t now) {
  if (screen != ScreenMode::Face) {
    return;
  }

  if (usagePeekCueActive) {
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
    if (now >= nextPendingAttentionMs) {
      pendingAttentionActive = !pendingAttentionActive;
      blinkUntilMs = 0;
      if (eyeExpression != EyeExpression::Wide) {
        eyeExpression = EyeExpression::Wide;
        renderCurrentScreen();
      }
      display.renderPendingAttention(pendingAttentionActive);
      nextPendingAttentionMs = now + Config::PendingAttentionIntervalMs;
    } else if (eyeExpression != EyeExpression::Wide) {
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

  if (state.aiActivity == AiActivity::Error) {
    if (eyeExpression != EyeExpression::Neutral) {
      eyeExpression = EyeExpression::Neutral;
      blinkUntilMs = 0;
      renderCurrentScreen();
    }
    return;
  }

  if (state.aiActivity == AiActivity::Disconnected) {
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
    activityStartedMs = now;
    state.activityElapsedSeconds = -1;
    pendingAttentionActive = false;
    manualUsageActive = false;
    usagePeekCueActive = false;
    nextPendingAttentionMs = 0;
    eyeExpression = EyeExpression::Neutral;
    blinkUntilMs = 0;
    scheduleEyeChange();
    return;
  }

  idleStartedMs = 0;
  activityStartedMs = now;
  state.activityElapsedSeconds = 0;
  lastActivityElapsedRenderMs = 0;
  manualUsageActive = false;
  usagePeekCueActive = false;
  blinkUntilMs = 0;
  if (currentActivity == AiActivity::Working) {
    pendingAttentionActive = false;
    nextPendingAttentionMs = 0;
    workingStrained = false;
    eyeExpression = EyeExpression::Focused;
    scheduleWorkingBlink();
    scheduleWorkingStrain(now);
  } else if (currentActivity == AiActivity::Pending) {
    pendingAttentionActive = false;
    nextPendingAttentionMs = now;
    eyeExpression = EyeExpression::Wide;
  } else if (currentActivity == AiActivity::Waiting) {
    pendingAttentionActive = false;
    nextPendingAttentionMs = 0;
    eyeExpression = EyeExpression::Neutral;
  } else if (currentActivity == AiActivity::Error) {
    pendingAttentionActive = false;
    nextPendingAttentionMs = 0;
    eyeExpression = EyeExpression::Neutral;
  } else if (currentActivity == AiActivity::Disconnected) {
    pendingAttentionActive = false;
    nextPendingAttentionMs = 0;
    eyeExpression = EyeExpression::Neutral;
  }
}

void updateActivityElapsed(uint32_t now) {
  if (state.aiActivity == AiActivity::Idle ||
      state.aiActivity == AiActivity::Disconnected ||
      selfTestActive || activityStartedMs == 0) {
    return;
  }

  const int16_t elapsed = min<uint32_t>((now - activityStartedMs) / 1000, 999);
  if (elapsed == state.activityElapsedSeconds) {
    return;
  }

  state.activityElapsedSeconds = elapsed;
  if (screen == ScreenMode::Face && state.aiActivity == AiActivity::Working &&
      now - lastActivityElapsedRenderMs >= 1000) {
    display.renderFooter(state);
    lastActivityElapsedRenderMs = now;
  }
}

void applySelfTestStep() {
  const SelfTestStep &step = SelfTestSteps[selfTestStep];
  const uint32_t now = millis();
  const AiActivity previousActivity = state.aiActivity;
  state.aiActivity = step.activity;
  state.aiWaitingForInput = step.activity == AiActivity::Waiting;
  state.activityElapsedSeconds = step.activity == AiActivity::Idle ? -1 : 0;
  state.servoPulseUs = -1;
  screen = step.screenMode;
  eyeExpression = step.expression;
  blinkUntilMs = 0;
  handleActivityTransition(previousActivity, state.aiActivity, now);
  if (previousActivity == state.aiActivity && state.aiActivity == AiActivity::Idle) {
    idleStartedMs = now;
  }
  activityStartedMs = now;
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
  manualUsageActive = false;
  usagePeekCueActive = false;
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
    state.activityElapsedSeconds = -1;
    state.servoPulseUs = -1;
    state.showUsageRequested = false;
    state.lastUpdateMs = now;
    screen = ScreenMode::Face;
    eyeExpression = EyeExpression::Neutral;
    idleStartedMs = now;
    activityStartedMs = now;
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

void updateConnectionState(uint32_t now) {
  if (selfTestActive || state.servoPulseUs >= 0) {
    return;
  }

  if (state.aiActivity == AiActivity::Disconnected) {
    return;
  }

  if (now - state.lastUpdateMs < Config::SerialDisconnectTimeoutMs) {
    return;
  }

  const AiActivity previousActivity = state.aiActivity;
  state.aiActivity = AiActivity::Disconnected;
  state.aiWaitingForInput = false;
  state.idleInSeconds = -1;
  state.activityElapsedSeconds = -1;
  state.servoPulseUs = -1;
  waitingUsagePeekAtMs = 0;
  manualUsageActive = false;
  handleActivityTransition(previousActivity, state.aiActivity, now);
  servoArm.setActivity(state.aiActivity);
  screen = ScreenMode::Face;
  renderCurrentScreen();
}

}

void setup() {
  Serial.begin(Config::SerialBaud);
  delay(500);
  Serial.println("Lego Clawd boot OK");

  randomSeed(esp_random());
  usageData.begin(state);
  settings.begin(state);
  servoArm.begin();
  servoArm.setQuietMode(state.quietMode);
  servoArm.setActivity(state.aiActivity);

  display.begin();
  renderCurrentScreen();
  lastScreenSwitchMs = millis();
  idleStartedMs = lastScreenSwitchMs;
  activityStartedMs = lastScreenSwitchMs;
  scheduleEyeChange();
  scheduleWorkingBlink();
  scheduleWorkingStrain(millis());
}

void loop() {
  const uint32_t now = millis();
  const AiActivity previousActivity = state.aiActivity;
  const int16_t previousServoPulseUs = state.servoPulseUs;
  const int16_t previousPendingWaveForwardPulseUs = state.pendingWaveForwardPulseUs;
  const uint16_t previousPendingWavePauseMs = state.pendingWavePauseMs;
  const bool previousQuietMode = state.quietMode;

  if (usageData.readSerialUpdate(Serial, state)) {
    if (previousQuietMode != state.quietMode) {
      settings.saveQuietMode(state.quietMode);
      servoArm.setQuietMode(state.quietMode);
    }

    if (previousPendingWaveForwardPulseUs != state.pendingWaveForwardPulseUs ||
        previousPendingWavePauseMs != state.pendingWavePauseMs) {
      servoArm.setPendingWaveConfig(state.pendingWaveForwardPulseUs,
                                    state.pendingWavePauseMs);
    }

    if (state.selfTestRequested) {
      state.selfTestRequested = false;
      state.showUsageRequested = false;
      startSelfTest(now);
    } else {
      const bool showUsageRequested = state.showUsageRequested;
      state.showUsageRequested = false;
      handleActivityTransition(previousActivity, state.aiActivity, now);
      if (previousActivity == AiActivity::Working &&
          state.aiActivity == AiActivity::Waiting) {
        waitingUsagePeekAtMs = now + Config::WaitingUsagePeekDelayMs;
      } else if (state.aiActivity != AiActivity::Waiting) {
        waitingUsagePeekAtMs = 0;
      }
      if (state.servoPulseUs >= 0 && !state.quietMode) {
        if (previousServoPulseUs != state.servoPulseUs) {
          servoArm.setCalibrationPulse(state.servoPulseUs);
        }
      } else if (previousActivity != state.aiActivity || previousServoPulseUs >= 0 ||
                 previousQuietMode != state.quietMode) {
        servoArm.setActivity(state.aiActivity);
      }
      if (showUsageRequested) {
        showUsageSummary(now);
      } else {
        forceFaceScreen();
      }
    }
  }

  updateConnectionState(now);
  updateSelfTest(now);
  updateScreenSchedule(now);
  updateActivityElapsed(now);
  updateFaceExpression(now);

  servoArm.update();
  delay(5);
}
