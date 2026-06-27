#ifndef ROBOT_EYES_H
#define ROBOT_EYES_H

#include <LovyanGFX.hpp>

// Added ASLEEP, DIZZY, and WAKEUP
enum Emotion
{
  NEUTRAL,
  HAPPY,
  ANGRY,
  SAD,
  SLEEPY,
  ASLEEP,
  INNOCENT,
  DIZZY,
  WAKEUP
};

class RobotEyes
{
private:
  // --- CONFIG ---
  int eyeW = 32;
  int eyeH = 42;
  int eyeR = 12;
  int eyeGap = 28;
  int pupilR = 10;

  // --- PHYSICS STATE (PUPIL) ---
  float curX = 0, curY = 0;
  float targetX = 0, targetY = 0;
  float easeFactor = 0.2;

  // --- PHYSICS STATE (WHOLE EYE / SCLERA) ---
  float eyeOffsetX = 0, eyeOffsetY = 0;
  float targetEyeOffsetX = 0, targetEyeOffsetY = 0;

  Emotion currentEmotion = NEUTRAL;

  // Normal Blink
  unsigned long lastBlinkTime = 0;
  int blinkInterval = 3000;
  bool isBlinking = false;
  float blinkState = 0.0;

  // Sleepy/Asleep Physics
  float sleepyLidHeight = 0.0;
  unsigned long lastSleepCheck = 0;
  int sleepPhase = 0;
  unsigned long sleepPhaseTimer = 0;
  float sleepTrembleAngle = 0.0;
  float sleepBreathAngle = 0.0;
  float sleepBreathY = 0.0;
  float sleepMicroDriftX = 0.0;
  float sleepMicroTargetX = 0.0;
  unsigned long lastMicroDrift = 0;

  // Happy Physics
  float happyBounceY = 0;
  float happyBounceAngle = 0;
  float happyShimmerAngle = 0.0;
  float happyBlinkState = 0.0;
  bool happyIsBlinking = false;
  unsigned long lastHappyBlinkTime = 0;
  int happyBlinkInterval = 4000;

  // Innocent & Dizzy
  float innocentPulseAngle = 0.0;
  float dizzyAngle = 0.0;

  // Transition blink (smooth crossfade on emotion switch)
  float transitionBlink = 0.0f;

public:
  void init();
  void update();
  void draw(LGFX_Sprite *spr);
  void lookAt(float x, float y);
  void setEyeOffset(float x, float y); // NEW: Controls the whole eye tilt
  void setEmotion(Emotion e);
  Emotion getEmotion() { return currentEmotion; }

private:
  void drawEye(LGFX_Sprite *spr, int x, int y, int side);
};

#endif