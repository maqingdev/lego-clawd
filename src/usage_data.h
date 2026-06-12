#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "app_state.h"

class UsageData {
 public:
  void begin(AppState &state);
  bool readSerialUpdate(Stream &stream, AppState &state);

 private:
  String line_;

  bool applyJsonLine(const String &line, AppState &state);
  static AiActivity activityFromJson(const JsonDocument &doc, AiActivity fallback);
  static AiActivity activityFromText(const char *value, AiActivity fallback);
  static uint8_t percentFromJson(int value, uint8_t fallback);
};
