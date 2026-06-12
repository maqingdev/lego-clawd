#pragma once

#include <Arduino.h>
#include "app_state.h"

class ServoArm {
 public:
  void begin();
  void setActivity(AiActivity activity);
  void setWaitingForInput(bool waiting);
  void update();
  bool attached() const { return attached_; }

 private:
  bool attached_ = false;
  AiActivity activity_ = AiActivity::Idle;
  int currentAngle_ = 0;
  int targetAngle_ = 0;
  bool workSwingForward_ = true;
  uint32_t lastStepMs_ = 0;
  uint32_t holdUntilMs_ = 0;

  void setTargetAngle(int angle);
  void updateWorkingTarget(uint32_t now);
};
