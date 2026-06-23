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
uint32_t bootingStartedMs = 0;
uint32_t lastDeviceStatusSentMs = 0;
uint8_t lastWorkingFaceLevel = 0;
bool workingStrained = false;
bool pendingAttentionActive = false;
bool usagePeekCueActive = false;
bool manualUsageActive = false;
bool selfTestActive = false;
bool bootingScreenActive = true;
uint8_t selfTestStep = 0;
uint32_t selfTestStepStartedMs = 0;

enum class LcdPowerState : uint8_t {
  On,
  Dim,
  Off,
};

LcdPowerState lcdPowerState = LcdPowerState::On;

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
void sendDeviceStatus(uint32_t now, bool force = false);

EyeExpression randomEyeExpression(bool allowSleep) {
  switch (random(0, allowSleep ? 9 : 7)) {
    case 0:
      return EyeExpression::Neutral;
    case 1:
      return EyeExpression::Happy;
    case 2:
      return EyeExpression::LookLeft;
    case 3:
      return EyeExpression::LookRight;
    case 4:
      return EyeExpression::Wink;
    case 5:
      return EyeExpression::Curious;
    case 6:
      return EyeExpression::Squint;
    case 7:
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

uint8_t workingFaceLevelForElapsed(int16_t elapsed) {
  if (elapsed >= static_cast<int16_t>(Config::WorkingTiredDelayMs / 1000)) {
    return 2;
  }
  if (elapsed >= static_cast<int16_t>(Config::WorkingSweatDelayMs / 1000)) {
    return 1;
  }
  return 0;
}

bool idleCanSleep(uint32_t now) {
  return idleStartedMs > 0 && now - idleStartedMs >= Config::IdleSleepDelayMs;
}

bool idleShouldDim(uint32_t now) {
  return idleStartedMs > 0 && now - idleStartedMs >= Config::LcdDimAfterIdleMs;
}

bool deviceIsActive() {
  return bootingScreenActive || selfTestActive || manualUsageActive ||
         state.servoPulseUs >= 0 || state.showUsageRequested ||
         state.selfTestRequested || state.aiActivity != AiActivity::Idle;
}

const char *lcdPowerStateText() {
  switch (lcdPowerState) {
    case LcdPowerState::Dim:
      return "dim";
    case LcdPowerState::Off:
      return "off";
    case LcdPowerState::On:
    default:
      return "on";
  }
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
  if (display.isSleeping()) {
    return;
  }

  if (bootingScreenActive && screen == ScreenMode::Face) {
    display.renderBooting(state);
    return;
  }

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
  if (bootingScreenActive) {
    return;
  }

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

  if (!idleShouldDim(now) &&
      now - lastScreenSwitchMs >= Config::UsagePeekIntervalMs) {
    startUsagePeekCue(now);
  }
}

void updateFaceExpression(uint32_t now) {
  if (display.isSleeping()) {
    return;
  }

  if (bootingScreenActive) {
    return;
  }

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
      workingStrained = !workingStrained;
      eyeExpression = workingStrained ? EyeExpression::Strain : EyeExpression::Focused;
      renderCurrentScreen();
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
    lastWorkingFaceLevel = 0;
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
      workingFaceLevelForElapsed(elapsed) != lastWorkingFaceLevel) {
    lastWorkingFaceLevel = workingFaceLevelForElapsed(elapsed);
    renderCurrentScreen();
    lastActivityElapsedRenderMs = now;
  } else if (screen == ScreenMode::Face && state.aiActivity == AiActivity::Working &&
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

void restoreSelfTestStepState() {
  const SelfTestStep &step = SelfTestSteps[selfTestStep];
  state.aiActivity = step.activity;
  state.aiWaitingForInput = step.activity == AiActivity::Waiting;
  state.activityElapsedSeconds = step.activity == AiActivity::Idle ? -1 : 0;
  state.servoPulseUs = -1;
  state.selfTestRequested = false;
  state.showUsageRequested = false;
  screen = step.screenMode;
  eyeExpression = step.expression;
  blinkUntilMs = 0;
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
  if (bootingScreenActive || selfTestActive || state.servoPulseUs >= 0) {
    return;
  }

  if (state.aiActivity == AiActivity::Disconnected) {
    return;
  }

  const int32_t updateAgeMs = static_cast<int32_t>(now - state.lastUpdateMs);
  if (updateAgeMs < 0 ||
      updateAgeMs < static_cast<int32_t>(Config::SerialDisconnectTimeoutMs)) {
    return;
  }

  Serial.print("connection timeout -> disconnected after ");
  Serial.print(updateAgeMs);
  Serial.println("ms without serial update");

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

void updateBootingScreen(uint32_t now) {
  if (!bootingScreenActive ||
      now - bootingStartedMs < Config::BootingScreenMs) {
    return;
  }

  bootingScreenActive = false;
  renderCurrentScreen();
}

void updateDisplayPower(uint32_t now) {
  const LcdPowerState previousState = lcdPowerState;

  if (deviceIsActive()) {
    lcdPowerState = LcdPowerState::On;
    if (display.isSleeping()) {
      display.wake();
      renderCurrentScreen();
    } else if (display.backlightPercent() != Config::LcdBacklightFullPercent) {
      display.setBacklightPercent(Config::LcdBacklightFullPercent);
    }
  } else if (idleStartedMs > 0 &&
             now - idleStartedMs >= Config::LcdOffAfterIdleMs) {
    lcdPowerState = LcdPowerState::Off;
    display.sleep();
  } else if (idleShouldDim(now)) {
    lcdPowerState = LcdPowerState::Dim;
    if (display.isSleeping()) {
      display.wake();
      renderCurrentScreen();
    }
    display.setBacklightPercent(Config::LcdBacklightDimPercent);
  } else {
    lcdPowerState = LcdPowerState::On;
    if (display.isSleeping()) {
      display.wake();
      renderCurrentScreen();
    } else if (display.backlightPercent() != Config::LcdBacklightFullPercent) {
      display.setBacklightPercent(Config::LcdBacklightFullPercent);
    }
  }

  if (previousState != lcdPowerState) {
    Serial.print("lcd power: ");
    Serial.println(lcdPowerStateText());
    sendDeviceStatus(now, true);
  }
}

void sendDeviceStatus(uint32_t now, bool force) {
  if (!force && now - lastDeviceStatusSentMs < Config::DeviceStatusIntervalMs) {
    return;
  }
  lastDeviceStatusSentMs = now;

  const float tempC = temperatureRead();
  Serial.print("{\"deviceStatus\":true,\"temperatureC\":");
  if (isnan(tempC)) {
    Serial.print("null");
  } else {
    Serial.print(tempC, 1);
  }
  Serial.print(",\"lcd\":\"");
  Serial.print(lcdPowerStateText());
  Serial.print("\",\"backlightPercent\":");
  Serial.print(display.backlightPercent());
  Serial.print(",\"uptimeMs\":");
  Serial.print(now);
  Serial.println("}");
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
  bootingStartedMs = millis();
  bootingScreenActive = true;
  renderCurrentScreen();
  lastScreenSwitchMs = bootingStartedMs;
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
    const bool firstPayloadAfterBoot = bootingScreenActive;
    bootingScreenActive = false;

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
    } else if (selfTestActive) {
      restoreSelfTestStepState();
    } else {
      const bool showUsageRequested = state.showUsageRequested;
      state.showUsageRequested = false;
      const bool activityChanged = previousActivity != state.aiActivity;
      const bool servoModeChanged = previousServoPulseUs != state.servoPulseUs;
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
      } else if (activityChanged || servoModeChanged || firstPayloadAfterBoot) {
        forceFaceScreen();
      } else if (previousQuietMode != state.quietMode) {
        renderCurrentScreen();
      } else if (screen == ScreenMode::Usage) {
        renderCurrentScreen();
      }
    }
  }

  updateConnectionState(now);
  updateSelfTest(now);
  updateBootingScreen(now);
  updateScreenSchedule(now);
  updateActivityElapsed(now);
  updateFaceExpression(now);
  updateDisplayPower(now);
  sendDeviceStatus(now);

  servoArm.update();
  delay(5);
}
