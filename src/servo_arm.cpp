#include "servo_arm.h"

#include <ESP32Servo.h>
#include "config.h"

namespace {
Servo servo;
}

void ServoArm::begin() {
  if (Config::ServoSignalPin < 0) {
    Serial.println("servo disabled");
    return;
  }

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servo.setPeriodHertz(50);
  attached_ = servo.attach(Config::ServoSignalPin, 500, 2500) >= 0;

  currentAngle_ = Config::ServoDownAngle;
  targetAngle_ = Config::ServoDownAngle;
  if (attached_) {
    servo.write(currentAngle_);
    Serial.println("servo arm ready");
  } else {
    Serial.println("servo attach failed");
  }
}

void ServoArm::setActivity(AiActivity activity) {
  activity_ = activity;
  holdUntilMs_ = 0;

  switch (activity_) {
    case AiActivity::Idle:
      setTargetAngle(Config::ServoDownAngle);
      break;
    case AiActivity::Working:
      workSwingForward_ = true;
      setTargetAngle(Config::ServoWorkMaxAngle);
      break;
    case AiActivity::Pending:
    case AiActivity::Waiting:
      setTargetAngle(Config::ServoRaisedAngle);
      break;
  }
}

void ServoArm::setWaitingForInput(bool waiting) {
  setActivity(waiting ? AiActivity::Waiting : AiActivity::Idle);
}

void ServoArm::update() {
  if (!attached_) {
    return;
  }

  const uint32_t now = millis();
  if (activity_ == AiActivity::Working) {
    updateWorkingTarget(now);
  }

  if (currentAngle_ == targetAngle_) {
    return;
  }

  if (now - lastStepMs_ < Config::ServoStepMs) {
    return;
  }
  lastStepMs_ = now;

  if (currentAngle_ < targetAngle_) {
    currentAngle_ = min(currentAngle_ + Config::ServoStepDegrees, targetAngle_);
  } else {
    currentAngle_ = max(currentAngle_ - Config::ServoStepDegrees, targetAngle_);
  }

  servo.write(currentAngle_);
}

void ServoArm::setTargetAngle(int angle) {
  targetAngle_ = constrain(angle, 0, 180);
}

void ServoArm::updateWorkingTarget(uint32_t now) {
  if (currentAngle_ != targetAngle_ || now < holdUntilMs_) {
    return;
  }

  if (workSwingForward_) {
    setTargetAngle(Config::ServoWorkMinAngle);
  } else {
    setTargetAngle(Config::ServoWorkMaxAngle);
  }
  workSwingForward_ = !workSwingForward_;
  holdUntilMs_ = now + Config::ServoWorkPauseMs;
}
