#include <Arduino.h>

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("Lego Clawd boot OK");
  delay(1000);
}
