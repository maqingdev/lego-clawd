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

void ServoArm::setWaitingForInput(bool waiting) {
  targetAngle_ = waiting ? Config::ServoRaisedAngle : Config::ServoDownAngle;
}

void ServoArm::update() {
  if (!attached_ || currentAngle_ == targetAngle_) {
    return;
  }

  const uint32_t now = millis();
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
