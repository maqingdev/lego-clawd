#pragma once

#include <Arduino.h>

class ServoArm {
 public:
  void begin();
  void setWaitingForInput(bool waiting);
  void update();
  bool attached() const { return attached_; }

 private:
  bool attached_ = false;
  int currentAngle_ = 0;
  int targetAngle_ = 0;
  uint32_t lastStepMs_ = 0;
};
