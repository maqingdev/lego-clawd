#include "persistent_settings.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {
Preferences preferences;
constexpr const char *Namespace = "lego-clawd";
constexpr const char *QuietModeKey = "quiet";
}

void PersistentSettings::begin(AppState &state) {
  preferences.begin(Namespace, false);
  state.quietMode = preferences.getBool(QuietModeKey, state.quietMode);
  Serial.print("quiet mode restored: ");
  Serial.println(state.quietMode ? "on" : "off");
}

void PersistentSettings::saveQuietMode(bool quietMode) {
  preferences.putBool(QuietModeKey, quietMode);
}
