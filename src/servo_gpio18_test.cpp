#include <Arduino.h>
#include <ESP32Servo.h>

namespace {

constexpr int ServoPin = 18;

Servo servo;

void writePulse(int microseconds, uint32_t holdMs) {
  Serial.print("servo gpio ");
  Serial.print(ServoPin);
  Serial.print(" pulse ");
  Serial.print(microseconds);
  Serial.println("us");

  servo.writeMicroseconds(microseconds);
  delay(holdMs);
}

}

void setup() {
  Serial.begin(115200);
  delay(2500);
  Serial.println();
  Serial.println("servo GPIO18 minimal test");

  ESP32PWM::allocateTimer(0);
  servo.setPeriodHertz(50);

  const int channel = servo.attach(ServoPin, 500, 2500);
  Serial.print("attach result: ");
  Serial.println(channel);

  if (channel < 0) {
    Serial.println("servo attach failed");
    return;
  }

  writePulse(1500, 2000);
}

void loop() {
  writePulse(700, 3000);
  writePulse(1500, 2500);
  writePulse(2300, 3000);
  writePulse(1500, 2500);
}
