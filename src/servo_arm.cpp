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
  workRestUntilMs_ = 0;
  pendingWaveForward_ = true;

  switch (activity_) {
    case AiActivity::Idle:
      setMotionSpeed(Config::ServoIdleStepPulseUs, Config::ServoIdleStepMs);
      setTargetPulse(Config::ServoDownPulseUs);
      break;
    case AiActivity::Working:
      setMotionSpeed(Config::ServoStepPulseUs, Config::ServoStepMs);
      workSwingForward_ = true;
      scheduleWorkingBurst();
      setTargetPulse(Config::ServoWorkMaxPulseUs);
      break;
    case AiActivity::Pending:
      setMotionSpeed(Config::ServoRaiseStepPulseUs, Config::ServoRaiseStepMs);
      setTargetPulse(Config::ServoRaisedPulseUs);
      break;
    case AiActivity::Waiting:
      setMotionSpeed(Config::ServoRaiseStepPulseUs, Config::ServoRaiseStepMs);
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
  workRestUntilMs_ = 0;
  pendingWaveForward_ = true;
  setMotionSpeed(Config::ServoStepPulseUs, Config::ServoStepMs);
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
  } else if (activity_ == AiActivity::Pending) {
    updatePendingTarget(now);
  }

  if (now - lastStepMs_ < stepMs_) {
    return;
  }
  lastStepMs_ = now;

  if (currentPulseUs_ < targetPulseUs_) {
    currentPulseUs_ = min(currentPulseUs_ + stepPulseUs_, targetPulseUs_);
  } else if (currentPulseUs_ > targetPulseUs_) {
    currentPulseUs_ = max(currentPulseUs_ - stepPulseUs_, targetPulseUs_);
  }

  servo.writeMicroseconds(currentPulseUs_);
}

void ServoArm::setMotionSpeed(int stepPulseUs, uint32_t stepMs) {
  stepPulseUs_ = max(1, stepPulseUs);
  stepMs_ = max<uint32_t>(1, stepMs);
}

void ServoArm::setTargetPulse(int pulseUs) {
  targetPulseUs_ = constrain(pulseUs, Config::ServoMinPulseUs, Config::ServoMaxPulseUs);
}

void ServoArm::updatePendingTarget(uint32_t now) {
  if (currentPulseUs_ != targetPulseUs_) {
    holdUntilMs_ = 0;
    return;
  }

  if (holdUntilMs_ == 0) {
    holdUntilMs_ = now + Config::ServoPendingWavePauseMs;
    return;
  }
  if (static_cast<int32_t>(now - holdUntilMs_) < 0) {
    return;
  }

  holdUntilMs_ = 0;
  setMotionSpeed(Config::ServoPendingWaveStepPulseUs,
                 Config::ServoPendingWaveStepMs);
  setTargetPulse(pendingWaveForward_ ? Config::ServoPendingWaveForwardPulseUs
                                     : Config::ServoRaisedPulseUs);
  pendingWaveForward_ = !pendingWaveForward_;
}

void ServoArm::updateWorkingTarget(uint32_t now) {
  if (currentPulseUs_ != targetPulseUs_ || now < holdUntilMs_) {
    return;
  }

  if (workRestUntilMs_ > 0) {
    if (static_cast<int32_t>(now - workRestUntilMs_) < 0) {
      return;
    }
    workRestUntilMs_ = 0;
    scheduleWorkingBurst();
  }

  if (workMovesRemaining_ == 0) {
    workRestUntilMs_ = now + random(Config::ServoWorkRestMinMs,
                                    Config::ServoWorkRestMaxMs + 1);
    return;
  }

  if (workSwingForward_) {
    setTargetPulse(Config::ServoWorkMinPulseUs);
  } else {
    setTargetPulse(Config::ServoWorkMaxPulseUs);
  }
  workSwingForward_ = !workSwingForward_;
  --workMovesRemaining_;
  holdUntilMs_ = now + Config::ServoWorkPauseMs;
}

void ServoArm::scheduleWorkingBurst() {
  workMovesRemaining_ = random(Config::ServoWorkBurstMinMoves,
                               Config::ServoWorkBurstMaxMoves + 1);
}
