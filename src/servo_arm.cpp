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
  attached_ = servo.attach(Config::ServoSignalPin,
                           Config::ServoMinPulseUs,
                           Config::ServoMaxPulseUs) >= 0;

  currentPulseUs_ = Config::ServoDownPulseUs;
  targetPulseUs_ = Config::ServoDownPulseUs;
  if (attached_) {
    servo.writeMicroseconds(currentPulseUs_);
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
      setTargetPulse(Config::ServoDownPulseUs);
      break;
    case AiActivity::Working:
      workSwingForward_ = true;
      setTargetPulse(Config::ServoWorkMaxPulseUs);
      break;
    case AiActivity::Pending:
    case AiActivity::Waiting:
      setTargetPulse(Config::ServoRaisedPulseUs);
      break;
  }

  Serial.print("servo target pulse us: ");
  Serial.println(targetPulseUs_);
}

void ServoArm::setWaitingForInput(bool waiting) {
  setActivity(waiting ? AiActivity::Waiting : AiActivity::Idle);
}

void ServoArm::setCalibrationPulse(int pulseUs) {
  activity_ = AiActivity::Idle;
  holdUntilMs_ = 0;
  setTargetPulse(pulseUs);
  Serial.print("servo calibration pulse us: ");
  Serial.println(targetPulseUs_);
}

void ServoArm::update() {
  if (!attached_) {
    return;
  }

  const uint32_t now = millis();
  if (activity_ == AiActivity::Working) {
    updateWorkingTarget(now);
  }

  if (now - lastStepMs_ < Config::ServoStepMs) {
    return;
  }
  lastStepMs_ = now;

  if (currentPulseUs_ < targetPulseUs_) {
    currentPulseUs_ = min(currentPulseUs_ + Config::ServoStepPulseUs, targetPulseUs_);
  } else if (currentPulseUs_ > targetPulseUs_) {
    currentPulseUs_ = max(currentPulseUs_ - Config::ServoStepPulseUs, targetPulseUs_);
  }

  servo.writeMicroseconds(currentPulseUs_);
}

void ServoArm::setTargetPulse(int pulseUs) {
  targetPulseUs_ = constrain(pulseUs, Config::ServoMinPulseUs, Config::ServoMaxPulseUs);
}

void ServoArm::updateWorkingTarget(uint32_t now) {
  if (currentPulseUs_ != targetPulseUs_ || now < holdUntilMs_) {
    return;
  }

  if (workSwingForward_) {
    setTargetPulse(Config::ServoWorkMinPulseUs);
  } else {
    setTargetPulse(Config::ServoWorkMaxPulseUs);
  }
  workSwingForward_ = !workSwingForward_;
  holdUntilMs_ = now + Config::ServoWorkPauseMs;
}
