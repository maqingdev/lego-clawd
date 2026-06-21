#pragma once

#include <Arduino.h>
#include "app_state.h"

enum class EyeExpression : uint8_t {
  Neutral,
  Blink,
  Happy,
  Sleepy,
  Doze,
  Focused,
  Strain,
  Wide,
  LookLeft,
  LookRight,
  Wink,
  Curious,
  Squint,
};

class DisplayUi {
 public:
  bool begin();
  void renderBooting(const AppState &state);
  void renderFace(EyeExpression expression, const AppState &state);
  void renderWorkingBrows(EyeExpression expression);
  void renderPendingAttention(bool active);
  void renderUsagePeekCue(const AppState &state);
  void renderUsageSummary(const UsageWindow &codex5h, const UsageWindow &codex1w,
                          AiActivity activity, int16_t idleInSeconds);
  void renderFooter(const AppState &state);

 private:
  uint16_t rgb(uint8_t red, uint8_t green, uint8_t blue) const;
  uint16_t faceBackground() const;
  uint8_t workingFaceLevel(const AppState &state) const;
  void drawEye(int16_t x, int16_t y, int16_t w, int16_t h, EyeExpression expression,
               bool leftEye);
  void drawFaceDetails(const AppState &state);
  void drawSweatDrop(int16_t x, int16_t y, int16_t size);
  void drawWorkingStress(const AppState &state, uint8_t level);
  void clearWorkingBrow(int16_t x, int16_t y, int16_t w);
  void drawWorkingBrow(int16_t x, int16_t y, int16_t w, EyeExpression expression,
                       bool leftEye);
  void clearApprovalMarks();
  void drawApprovalMarks();
  void drawDozeMarks();
  void drawErrorFace();
  void drawErrorEye(int16_t x, int16_t y, int16_t size);
  void drawDisconnectedFace();
  void drawUsageCuePanel();
  void drawFooter(const AppState &state);
  void drawQuietIcon(int16_t x, int16_t y);
  void workingLabel(const AppState &state, char *label, size_t size);
  void drawDebugState(AiActivity activity, int16_t idleInSeconds);
  void drawUsageBlock(int16_t x, const char *label, const UsageWindow &window);
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t percent);
  uint16_t percentColor(uint8_t percent) const;
};
