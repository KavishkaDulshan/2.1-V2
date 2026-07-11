#ifndef ROBOT_EYES_H
#define ROBOT_EYES_H

#include <LovyanGFX.hpp>

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
  WAKEUP,
  GUARDING,
  PANIC,
  WARNING_ANIM,
  CLOCK_MODE,
  ALARM_RINGING
};

class RobotEyes
{
private:
  // --- CONFIG (Updated for 160x128 Display) ---
  int eyeW = 48;
  int eyeH = 64;
  int eyeR = 16;
  int eyeGap = 38;
  int pupilR = 14;

  uint16_t getEmotionColor(Emotion e);

  // --- PHYSICS STATE (PUPIL) ---
  float curX = 0, curY = 0;
  float targetX = 0, targetY = 0;
  float easeFactor = 0.2;

  // --- PHYSICS STATE (WHOLE EYE / SCLERA) ---
  float eyeOffsetX = 0, eyeOffsetY = 0;
  float targetEyeOffsetX = 0, targetEyeOffsetY = 0;

  Emotion currentEmotion = NEUTRAL;

  // Normal Blink & Double Blink
  unsigned long lastBlinkTime = 0;
  int blinkInterval = 3000;
  bool isBlinking = false;
  float blinkState = 0.0;
  bool isDoubleBlinking = false;
  int doubleBlinkPhase = 0; // 0=none, 1=closing, 2=opening, 3=closing2, 4=opening2
  
  // IDLE Hover & Background Effects
  float idleHoverAngle = 0.0f;
  
  static const int MAX_FIREFLIES = 6;
  struct Firefly {
    float x, y, vx, vy, alpha;
    bool active;
  } fireflies[MAX_FIREFLIES];
  unsigned long nextFireflyTimer = 0;

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
  float happyBlinkState = 0.0;
  bool happyIsBlinking = false;
  unsigned long lastHappyBlinkTime = 0;
  int happyBlinkInterval = 4000;
  float happyEyeWidthPulse = 0.0; // breathing swell
  float happyStarAngle = 0.0f;

  // Background Hearts (Happy)
  static const int MAX_HEARTS = 5;
  struct Heart {
    float x, y, vy, size;
    bool active;
  } hearts[MAX_HEARTS];
  unsigned long heartSpawnTimer = 0;

  // Innocent & Dizzy
  float innocentPulseAngle = 0.0;
  float dizzyAngle = 0.0;
  float dizzyTrailAngle[3] = {0, 0, 0}; // spiral trail
  float dizzyBackgroundAngle = 0.0;
  float dizzyOrbitAngle = 0.0;

  // Background Stars/Fireworks (Innocent)
  static const int MAX_STARS = 6;
  struct Star {
    float x, y, vy;
    bool active;
  } stars[MAX_STARS];
  unsigned long starSpawnTimer = 0;

  struct Firework {
    float x, y, radius, maxRadius, alpha;
    bool active;
  } firework;
  unsigned long fireworkTimer = 0;

  // Transition blink (smooth crossfade on emotion switch)
  float transitionBlink = 0.0f;

  // Guard & Panic Physics
  float guardScanAngle = 0.0f;
  float guardSirenAngle = 0.0f;
  unsigned long lastLookAtTime = 0;
  float guardPupilPulseAngle = 0.0f;
  float panicAngle = 0.0f;

  // Warning
  int warningFrame = 0;
  unsigned long lastWarningFrameTime = 0;

  // --- NEW: SAD TEARDROP ---
  bool sadTearActive = false;
  float sadTearX = 0, sadTearY = 0;
  float sadTearVy = 0;
  float sadTearW = 0, sadTearH = 0;
  unsigned long sadTearTimer = 0;
  float sadLidAngle = 0.0f; // droopy top lid

  // --- NEW: ANGRY TWITCH & PARTICLES ---
  float angryTwitchAngle = 0.0f;
  float angryTwitchOffset = 0.0f;
  unsigned long angryTwitchTimer = 0;
  bool angryTwitching = false;

  static const int MAX_STEAM = 8;
  struct SteamPuff {
    float x, y, vy, radius;
    float alpha;
    bool active;
  } steamPuffs[MAX_STEAM];
  unsigned long steamSpawnTimer = 0;

  float angerIntensityAngle = 0.0f; // For radiating lines/dots

  // --- NEW: PANIC SWEAT ---
  float panicSweatY = 0.0f;
  bool panicSweatActive = false;
  unsigned long panicSweatTimer = 0;
  // --- NEW: ASLEEP Zzz PARTICLES ---
  static const int MAX_ZZZ = 5;
  struct ZParticle {
    float x, y, size;
    unsigned long spawnTime;
    bool active;
  } zParticles[MAX_ZZZ];
  unsigned long zSpawnTimer = 0;
  int panicSweatSide = 1; // which eye side

  // --- NEW: NEUTRAL IDLE GAZE ---
  float idleDriftAngle = 0.0f;
  float idleDriftX = 0.0f, idleDriftY = 0.0f;
  float idleTargetX = 0.0f, idleTargetY = 0.0f;
  unsigned long idleGazeTimer = 0;
  bool mpuActive = false; // set true if MPU is moving (overrides idle)

  // --- NEW: INNOCENT ---
  unsigned long innocentFlickTimer = 0;
  bool innocentFlicking = false;
  float innocentFlickTarget = 0.0f;

  // --- CLOCK BACKGROUND PARTICLES ---
  static const int MAX_RAIN_DROPS = 20;
  struct RainDrop {
    float x, y, speed;
    int len;
    bool active;
  } rainDrops[MAX_RAIN_DROPS];

  static const int MAX_CLOCK_STARS = 15;
  struct ClockStar {
    float x, y;
    float twinkleAngle;
    float twinkleSpeed;
    uint8_t size;
  } clockStars[MAX_CLOCK_STARS];

  static const int MAX_BG_CLOUDS = 3;
  struct BgCloud {
    float x, y, speed;
    uint8_t alpha; // dimmed
  } bgClouds[MAX_BG_CLOUDS];

  bool clockBgInitialized = false;
  int  clockBgHour = -1; // track when to re-init background

public:
  void init();
  void update();
  void draw(LGFX_Sprite *spr);
  void lookAt(float x, float y);
  void setEyeOffset(float x, float y);
  void setEmotion(Emotion e);
  void setMpuActive(bool active) { mpuActive = active; }
  Emotion getEmotion() { return currentEmotion; }
  Emotion baseEmotion = NEUTRAL; // Default mode to return to (NEUTRAL or CLOCK_MODE)

  // --- UTILITY DATA ---
  String timeString = "00:00";
  int    clockHour  = 12; // 0-23 for sky color logic
  String weatherIcon = ""; // "sun", "cloud", "rain"
  float weatherTemp = 0.0f;
  String weatherCondition = ""; // e.g. "Clear", "Rain"
  uint16_t clockColor = 0xFFFF; // Default to white (TFT_WHITE)
  
  // Timer / Alarm
  bool timerActive = false;
  float timerProgress = 0.0f; // 0.0 to 1.0

private:
  void drawEye(LGFX_Sprite *spr, int x, int y, int side, int wOverride = 0, int hOverride = 0);
};

#endif