#pragma once

#include <Arduino.h>
#include "app_state.h"
#include "config.h"

class ServoArm {
 public:
  void begin();
  void setActivity(AiActivity activity);
  void setCalibrationPulse(int pulseUs);
  void setPendingWaveConfig(int forwardPulseUs, uint32_t pauseMs);
  void setQuietMode(bool quiet);
  void setWaitingForInput(bool waiting);
  void update();
  bool attached() const { return attached_; }

 private:
  bool attached_ = false;
  bool quietMode_ = false;
  AiActivity activity_ = AiActivity::Idle;
  int currentPulseUs_ = 0;
  int targetPulseUs_ = 0;
  int stepPulseUs_ = Config::ServoStepPulseUs;
  int pendingWaveForwardPulseUs_ = Config::ServoPendingWaveForwardPulseUs;
  uint32_t pendingWavePauseMs_ = Config::ServoPendingWavePauseMs;
  bool workSwingForward_ = true;
  bool pendingWaveForward_ = true;
  uint8_t workMovesRemaining_ = 0;
  uint32_t lastStepMs_ = 0;
  uint32_t stepMs_ = Config::ServoStepMs;
  uint32_t holdUntilMs_ = 0;
  uint32_t workRestUntilMs_ = 0;

  void setMotionSpeed(int stepPulseUs, uint32_t stepMs);
  void setTargetPulse(int pulseUs);
  void applyActivityTarget();
  void updatePendingTarget(uint32_t now);
  void updateWorkingTarget(uint32_t now);
  void scheduleWorkingBurst();
};
