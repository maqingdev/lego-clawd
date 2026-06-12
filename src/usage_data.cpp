#include "usage_data.h"

#include <ArduinoJson.h>

void UsageData::begin(AppState &state) {
  state.codex5h.remainingPercent = 76;
  state.codex5h.resetAt = "18:30";
  state.codex1w.remainingPercent = 58;
  state.codex1w.resetAt = "Mon 09:00";
  state.aiActivity = AiActivity::Idle;
  state.aiWaitingForInput = false;
  state.lastUpdateMs = millis();
}

bool UsageData::readSerialUpdate(Stream &stream, AppState &state) {
  bool updated = false;

  while (stream.available() > 0) {
    const char ch = static_cast<char>(stream.read());
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      line_.trim();
      if (line_.length() > 0) {
        updated = applyJsonLine(line_, state) || updated;
      }
      line_ = "";
      continue;
    }

    if (line_.length() < 512) {
      line_ += ch;
    } else {
      line_ = "";
    }
  }

  return updated;
}

bool UsageData::applyJsonLine(const String &line, AppState &state) {
  StaticJsonDocument<512> doc;
  const DeserializationError error = deserializeJson(doc, line);
  if (error) {
    Serial.print("usage json parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  state.codex5h.remainingPercent = percentFromJson(
      doc["codex5h"] | doc["fiveHourRemainingPct"] | -1,
      state.codex5h.remainingPercent);
  state.codex1w.remainingPercent = percentFromJson(
      doc["codex1w"] | doc["oneWeekRemainingPct"] | -1,
      state.codex1w.remainingPercent);

  if (doc["reset5h"].is<const char *>()) {
    state.codex5h.resetAt = doc["reset5h"].as<const char *>();
  } else if (doc["fiveHourResetAt"].is<const char *>()) {
    state.codex5h.resetAt = doc["fiveHourResetAt"].as<const char *>();
  }

  if (doc["reset1w"].is<const char *>()) {
    state.codex1w.resetAt = doc["reset1w"].as<const char *>();
  } else if (doc["oneWeekResetAt"].is<const char *>()) {
    state.codex1w.resetAt = doc["oneWeekResetAt"].as<const char *>();
  }

  if (doc["aiState"].is<const char *>() || doc["state"].is<const char *>()) {
    state.aiActivity = activityFromJson(doc, state.aiActivity);
    state.aiWaitingForInput = state.aiActivity == AiActivity::Waiting;
  } else if (doc["waiting"].is<bool>()) {
    state.aiWaitingForInput = doc["waiting"].as<bool>();
    state.aiActivity = state.aiWaitingForInput ? AiActivity::Waiting : AiActivity::Idle;
  } else if (doc["aiWaitingForInput"].is<bool>()) {
    state.aiWaitingForInput = doc["aiWaitingForInput"].as<bool>();
    state.aiActivity = state.aiWaitingForInput ? AiActivity::Waiting : AiActivity::Idle;
  }

  state.lastUpdateMs = millis();
  Serial.println("usage update OK");
  return true;
}

AiActivity UsageData::activityFromJson(const JsonDocument &doc, AiActivity fallback) {
  if (doc["aiState"].is<const char *>()) {
    return activityFromText(doc["aiState"].as<const char *>(), fallback);
  }
  if (doc["state"].is<const char *>()) {
    return activityFromText(doc["state"].as<const char *>(), fallback);
  }
  return fallback;
}

AiActivity UsageData::activityFromText(const char *value, AiActivity fallback) {
  if (value == nullptr) {
    return fallback;
  }

  String text(value);
  text.toLowerCase();

  if (text == "idle" || text == "ready") {
    return AiActivity::Idle;
  }
  if (text == "working" || text == "running" || text == "thinking") {
    return AiActivity::Working;
  }
  if (text == "waiting" || text == "waiting_input" || text == "waiting_approval" ||
      text == "done") {
    return AiActivity::Waiting;
  }

  return fallback;
}

uint8_t UsageData::percentFromJson(int value, uint8_t fallback) {
  if (value < 0) {
    return fallback;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<uint8_t>(value);
}
