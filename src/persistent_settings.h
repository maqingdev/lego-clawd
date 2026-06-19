#pragma once

#include "app_state.h"

class PersistentSettings {
 public:
  void begin(AppState &state);
  void saveQuietMode(bool quietMode);
};
