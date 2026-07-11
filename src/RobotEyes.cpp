#include "RobotEyes.h"
#include <math.h>
#include "WarningAnimation.h"

static void drawRealStar(LGFX_Sprite *spr, int x, int y, int radius, uint16_t color) {
  int innerRadius = radius / 2;
  int points[10][2];
  for (int i = 0; i < 10; i++) {
    float angle = i * (PI / 5.0f) - (PI / 2.0f);
    int r = (i % 2 == 0) ? radius : innerRadius;
    points[i][0] = x + (int)(cos(angle) * r);
    points[i][1] = y + (int)(sin(angle) * r);
  }
  for (int i = 0; i < 10; i++) {
    int next = (i + 1) % 10;
    spr->drawLine(points[i][0], points[i][1], points[next][0], points[next][1], color);
    // Fill the star roughly by drawing lines to the center
    spr->fillTriangle(x, y, points[i][0], points[i][1], points[next][0], points[next][1], color);
  }
}

void RobotEyes::init()
{
  randomSeed(esp_random());
  for (int i = 0; i < 3; i++) dizzyTrailAngle[i] = 0;
  for (int i = 0; i < MAX_HEARTS; i++) hearts[i].active = false;
  for (int i = 0; i < MAX_STARS; i++) stars[i].active = false;
  for (int i = 0; i < MAX_STEAM; i++) steamPuffs[i].active = false;
  for (int i = 0; i < MAX_ZZZ; i++) zParticles[i].active = false;
  firework.active = false;
}

uint16_t RobotEyes::getEmotionColor(Emotion e)
{
  // All emotions use white sclera - emotion is conveyed purely through shape
  return 0xFFFF; // TFT_WHITE
}

void RobotEyes::setEmotion(Emotion e)
{
  currentEmotion = e;
  blinkState = 0;
  isBlinking = false;
  transitionBlink = 0.45f;
  lastBlinkTime = millis();

  // Reset emotion-specific state
  sadTearActive = false;
  sadTearTimer = millis();
  sadLidAngle = 0;
  angryTwitching = false;
  angryTwitchOffset = 0;
  panicSweatActive = false;
  innocentFlicking = false;

  if (e == SLEEPY)
  {
    sleepyLidHeight = 0.05f;
    sleepPhase = 0;
    sleepPhaseTimer = millis();
    sleepTrembleAngle = 0;
    sleepMicroDriftX = 0;
    sleepMicroTargetX = 0;
    lastMicroDrift = 0;
    targetY = 0;
    curY = 0;
    easeFactor = 0.05f;
  }
  else if (e == ASLEEP)
  {
    sleepBreathAngle = 0;
    sleepyLidHeight = 0.92f;
    sleepPhase = 0;
    sleepPhaseTimer = millis();
    sleepTrembleAngle = 0;
    sleepMicroDriftX = 0;
    sleepMicroTargetX = 0;
    blinkState = 0;
    targetY = 20.0f;
    curY = 20.0f;
    easeFactor = 0.04f;
    for (int i = 0; i < MAX_ZZZ; i++) zParticles[i].active = false;
    zSpawnTimer = millis();
  }
  else if (e == HAPPY)
  {
    happyBounceAngle = 0;
    happyEyeWidthPulse = 0;
    easeFactor = 0.2f;
    for (int i = 0; i < MAX_HEARTS; i++) hearts[i].active = false;
  }
  else if (e == SAD)
  {
    targetX = 0;
    targetY = 8.0f;
    easeFactor = 0.08f;
    sadLidAngle = 0;
    sadTearTimer = millis() + random(2000, 5000);
  }
  else if (e == INNOCENT)
  {
    targetX = 0;
    targetY = -5.0f;
    easeFactor = 0.15f;
    innocentFlickTimer = millis() + random(3000, 7000);
    for (int i = 0; i < MAX_STARS; i++) stars[i].active = false;
    firework.active = false;
  }
  else if (e == DIZZY)
  {
    dizzyAngle = 0;
    for (int i = 0; i < 3; i++) dizzyTrailAngle[i] = 0;
    easeFactor = 0.3f;
  }
  else if (e == WAKEUP)
  {
    targetX = 0;
    targetY = 0;
    blinkState = 1.0f;
    easeFactor = 0.3f;
  }
  else if (e == GUARDING)
  {
    guardScanAngle = 0.0f;
    guardSirenAngle = 0.0f;
    guardPupilPulseAngle = 0.0f;
    targetX = 0;
    targetY = 8.0f; // lower the eyes slightly
    easeFactor = 0.1f;
  }
  else if (e == PANIC)
  {
    panicAngle = 0.0f;
    panicSweatTimer = millis() + random(500, 1500);
    easeFactor = 0.4f;
  }
  else if (e == ANGRY)
  {
    angryTwitchAngle = 0;
    angryTwitchTimer = millis() + random(800, 2000);
    easeFactor = 0.2f;
    for (int i = 0; i < MAX_STEAM; i++) steamPuffs[i].active = false;
    steamSpawnTimer = millis();
    targetX = 0;
    targetY = 0; // intense stare
  }
  else if (e == WARNING_ANIM)
  {
    warningFrame = 0;
    lastWarningFrameTime = millis();
    targetX = 0;
    targetY = 0;
  }
  else // NEUTRAL
  {
    targetX = 0;
    targetY = 0;
    easeFactor = 0.2f;
    idleGazeTimer = millis() + random(1000, 3000);
    idleDriftX = 0;
    idleDriftY = 0;
    idleTargetX = 0;
    idleTargetY = 0;
  }
}

void RobotEyes::lookAt(float x, float y)
{
  targetX = constrain(x, -1.0, 1.0) * 14.0;
  targetY = constrain(y, -1.0, 1.0) * 10.0;
  lastLookAtTime = millis();
}

void RobotEyes::setEyeOffset(float x, float y)
{
  targetEyeOffsetX = constrain(x, -15.0, 15.0);
  targetEyeOffsetY = constrain(y, -15.0, 15.0);
}

void RobotEyes::update()
{
  // Smooth movement for Pupil
  curX += (targetX - curX) * easeFactor;
  curY += (targetY - curY) * easeFactor;

  // Smooth movement for Whole Eye Offset
  eyeOffsetX += (targetEyeOffsetX - eyeOffsetX) * 0.15f;
  eyeOffsetY += (targetEyeOffsetY - eyeOffsetY) * 0.15f;

  // Decay transition blink
  if (transitionBlink > 0) {
    transitionBlink -= 0.10f;
    if (transitionBlink < 0) transitionBlink = 0;
  }

  unsigned long now = millis();

  // --- DIZZY ANIMATION ---
  if (currentEmotion == DIZZY)
  {
    // Store trail before advancing
    dizzyTrailAngle[2] = dizzyTrailAngle[1];
    dizzyTrailAngle[1] = dizzyTrailAngle[0];
    dizzyTrailAngle[0] = dizzyAngle;
    dizzyAngle += 0.4f;
    dizzyBackgroundAngle += 0.05f;
    dizzyOrbitAngle -= 0.1f;
    targetX = sin(dizzyAngle) * 10.0f;
    targetY = cos(dizzyAngle) * 10.0f;
    blinkState = 0.2f + (sin(dizzyAngle * 0.5f) * 0.1f);
    return;
  }

  // --- PANIC ANIMATION ---
  if (currentEmotion == PANIC)
  {
    panicAngle += 1.2f;
    targetX = sin(panicAngle) * 6.0f;
    targetY = cos(panicAngle * 1.5f) * 6.0f;
    blinkState = 0.1f + (sin(panicAngle * 0.5f) * 0.1f);
    // Sweat drop lifecycle
    if (!panicSweatActive && now > panicSweatTimer) {
      panicSweatActive = true;
      panicSweatY = 0.0f;
      panicSweatSide = (random(2) == 0) ? -1 : 1;
    }
    if (panicSweatActive) {
      panicSweatY += 1.2f;
      if (panicSweatY > 25) {
        panicSweatActive = false;
        panicSweatTimer = now + random(800, 2000);
      }
    }
    return;
  }

  // --- WARNING ANIMATION ---
  if (currentEmotion == WARNING_ANIM)
  {
    if (now - lastWarningFrameTime > WARNING_FRAME_DELAY)
    {
      warningFrame = (warningFrame + 1) % WARNING_FRAME_COUNT;
      lastWarningFrameTime = now;
    }
    return;
  }

  // --- WAKEUP ANIMATION ---
  if (currentEmotion == WAKEUP)
  {
    if (blinkState > 0.0f)
    {
      blinkState -= 0.15f;
      if (blinkState <= 0)
        blinkState = 0;
    }
    return;
  }

  // --- GUARDING ANIMATION ---
  if (currentEmotion == GUARDING)
  {
    guardPupilPulseAngle += 0.05f;
    
    // Spin the siren
    guardSirenAngle += 6.0f; // 6 degrees per frame
    if (guardSirenAngle >= 360.0f) guardSirenAngle -= 360.0f;
    
    if (now - lastLookAtTime > 2000) {
      guardScanAngle += 0.03f;
      targetX = sin(guardScanAngle) * 12.0f;
      targetY = 8.0f; // keep eyes lowered slightly
    }
    // Allow fallthrough so the global blink timer updates blinkState for GUARDING
  }

  // --- ASLEEP ANIMATION ---
  if (currentEmotion == ASLEEP)
  {
    sleepBreathAngle += 0.010f;
    sleepBreathY = sin(sleepBreathAngle) * 2.5f;
    sleepTrembleAngle += 0.08f;
    
    // Update Zzz Particles
    if (now > zSpawnTimer) {
      for (int i = 0; i < MAX_ZZZ; i++) {
        if (!zParticles[i].active) {
          zParticles[i].active = true;
          zParticles[i].x = 80; // middle of the screen
          zParticles[i].y = 64; // middle of the screen
          zParticles[i].size = 0.5f;
          zParticles[i].spawnTime = now;
          break;
        }
      }
      zSpawnTimer = now + random(1200, 1800); // reduced frequency to increase gap
    }
    for (int i = 0; i < MAX_ZZZ; i++) {
      if (zParticles[i].active) {
        zParticles[i].y -= 0.4f;
        zParticles[i].size += 0.01f;
        // Drift rightwards to disappear at top right, while still swaying
        zParticles[i].x += 0.3f + sin((now - zParticles[i].spawnTime) * 0.003f) * 0.4f;
        if (zParticles[i].y < -20 || zParticles[i].x > 180) zParticles[i].active = false;
      }
    }

    if (now - lastSleepCheck > 50)
    {
      switch (sleepPhase)
      {
      case 0:
        sleepyLidHeight = 0.92f + sin(sleepTrembleAngle) * 0.012f;
        if (now - sleepPhaseTimer > (unsigned long)random(3000, 6000))
        {
          sleepPhase = 1;
          sleepPhaseTimer = now;
        }
        break;
      case 1:
        sleepyLidHeight -= 0.008f;
        if (sleepyLidHeight <= 0.60f)
        {
          sleepyLidHeight = 0.60f;
          sleepPhase = 2;
          sleepPhaseTimer = now;
        }
        break;
      case 2:
        sleepyLidHeight = 0.60f + sin(sleepTrembleAngle * 1.5f) * 0.04f;
        if (now - sleepPhaseTimer > (unsigned long)random(400, 900))
        {
          sleepPhase = 3;
          sleepPhaseTimer = now;
        }
        break;
      case 3:
        sleepyLidHeight += 0.010f;
        if (sleepyLidHeight >= 0.92f)
        {
          sleepyLidHeight = 0.92f;
          sleepPhase = 0;
          sleepPhaseTimer = now;
        }
        break;
      }
      lastSleepCheck = now;
    }
    blinkState = 0;
    return;
  }

  // --- HAPPY ---
  if (currentEmotion == HAPPY)
  {
    happyBounceAngle += 0.15f;
    happyBounceY = sin(happyBounceAngle) * 3.0f;
    happyEyeWidthPulse = sin(happyBounceAngle * 0.5f) * 2.0f; // breathing swell
    happyStarAngle += 0.15f; // Orbit speed
    if (!happyIsBlinking && (now - lastHappyBlinkTime > (unsigned long)happyBlinkInterval))
    {
      happyIsBlinking = true;
      happyBlinkInterval = random(2500, 5000);
    }
    if (happyIsBlinking)
    {
      happyBlinkState += 0.20f;
      if (happyBlinkState >= 1.0f)
      {
        happyBlinkState = 1.0f;
        happyIsBlinking = false;
        lastHappyBlinkTime = now;
      }
    }
    else if (happyBlinkState > 0)
    {
      happyBlinkState -= 0.20f;
      if (happyBlinkState < 0)
        happyBlinkState = 0;
    }

    // Update Hearts
    if (now > heartSpawnTimer) {
      for (int i = 0; i < MAX_HEARTS; i++) {
        if (!hearts[i].active) {
          hearts[i].active = true;
          hearts[i].x = random(20, 140);
          hearts[i].y = 130 + random(0, 20); // spawn below screen
          hearts[i].vy = -((float)random(10, 25) / 10.0f);
          hearts[i].size = (float)random(3, 7);
          break;
        }
      }
      heartSpawnTimer = now + random(500, 1500);
    }
    for (int i = 0; i < MAX_HEARTS; i++) {
      if (hearts[i].active) {
        hearts[i].y += hearts[i].vy;
        if (hearts[i].y < -20) hearts[i].active = false;
      }
    }
    return;
  }

  // --- SLEEPY ---
  if (currentEmotion == SLEEPY)
  {
    sleepBreathAngle += 0.018f;
    sleepBreathY = sin(sleepBreathAngle) * 1.2f;
    sleepTrembleAngle += 0.30f;
    if (now - lastSleepCheck > 50)
    {
      switch (sleepPhase)
      {
      case 0:
        sleepyLidHeight = 0.05f;
        targetY = 0;
        sleepPhase = 1;
        sleepPhaseTimer = now;
        break;
      case 1:
      {
        sleepyLidHeight += 0.010f;
        targetY = sleepyLidHeight * 22.0f;
        if (now - lastMicroDrift > 300)
        {
          sleepMicroTargetX = (float)random(-5, 6);
          lastMicroDrift = now;
        }
        sleepMicroDriftX += (sleepMicroTargetX - sleepMicroDriftX) * 0.04f;
        if (sleepyLidHeight >= 0.85f)
        {
          sleepyLidHeight = 0.85f;
          sleepPhase = 2;
          sleepPhaseTimer = now;
        }
        break;
      }
      case 2:
        targetY = 22.0f;
        sleepyLidHeight = 0.85f + sin(sleepTrembleAngle * 0.4f) * 0.02f;
        if (now - sleepPhaseTimer > (unsigned long)random(900, 2200))
        {
          sleepPhase = 3;
          sleepPhaseTimer = now;
          targetY = 3.0f;
        }
        break;
      case 3:
        sleepyLidHeight -= 0.050f;
        if (sleepyLidHeight <= 0.20f)
        {
          sleepyLidHeight = 0.20f;
          sleepPhase = 4;
          sleepPhaseTimer = now;
          targetY = 7.0f;
        }
        break;
      case 4:
        sleepyLidHeight = 0.20f + fabs(sin(sleepTrembleAngle * 0.6f)) * 0.22f;
        targetY = 7.0f + sin(sleepTrembleAngle * 0.3f) * 2.0f;
        if (now - sleepPhaseTimer > (unsigned long)random(450, 950))
        {
          sleepPhase = 1;
          sleepPhaseTimer = now;
        }
        break;
      }
      lastSleepCheck = now;
    }
    blinkState = 0;
    return;
  }

  // --- INNOCENT ---
  if (currentEmotion == INNOCENT)
  {
    innocentPulseAngle += 0.025f;
    targetY = -6.0f + sin(innocentPulseAngle * 0.4f) * 2.0f;
    targetX = sin(innocentPulseAngle * 0.3f) * 5.0f;
    // Occasional attention flick
    if (!innocentFlicking && now > innocentFlickTimer) {
      innocentFlicking = true;
      innocentFlickTarget = (random(2) == 0) ? 14.0f : -14.0f;
    }
    if (innocentFlicking) {
      targetX = innocentFlickTarget;
      if (fabs(curX - innocentFlickTarget) < 1.5f) {
        innocentFlicking = false;
        innocentFlickTimer = now + random(4000, 8000);
        targetX = sin(innocentPulseAngle * 0.3f) * 5.0f; // return to sway
      }
    }

    // Update Stars
    if (now > starSpawnTimer) {
      for (int i = 0; i < MAX_STARS; i++) {
        if (!stars[i].active) {
          stars[i].active = true;
          stars[i].x = random(10, 150);
          stars[i].y = 130 + random(0, 20);
          stars[i].vy = -((float)random(15, 30) / 10.0f);
          break;
        }
      }
      starSpawnTimer = now + random(300, 1000);
    }
    for (int i = 0; i < MAX_STARS; i++) {
      if (stars[i].active) {
        stars[i].y += stars[i].vy;
        if (stars[i].y < -20) stars[i].active = false;
      }
    }

    // Update Firework
    if (!firework.active && now > fireworkTimer) {
      firework.active = true;
      firework.x = random(30, 130);
      firework.y = random(20, 100);
      firework.radius = 0;
      firework.maxRadius = random(15, 35);
      firework.alpha = 1.0f;
    }
    if (firework.active) {
      firework.radius += 1.5f;
      firework.alpha -= 0.04f;
      if (firework.alpha <= 0) {
        firework.active = false;
        fireworkTimer = now + random(800, 2000); // More frequent pops
      }
    }
  }

  // --- SAD ---
  if (currentEmotion == SAD)
  {
    sadLidAngle += 0.02f;
    // Teardrop lifecycle
    if (!sadTearActive && now > sadTearTimer) {
      sadTearActive = true;
      sadTearX = (random(2) == 0) ? -eyeGap : eyeGap; // which eye
      sadTearY = 64 + eyeH / 2 - 4;
      sadTearVy = 0.5f;
      sadTearW = 4;
      sadTearH = 6;
    }
    if (sadTearActive) {
      sadTearVy += 0.08f; // gravity
      sadTearY += sadTearVy;
      sadTearH += 0.15f; // stretches as it falls
      if (sadTearY > 130) {
        sadTearActive = false;
        sadTearTimer = now + random(2000, 5000);
      }
    }
  }

  // --- ANGRY ---
  if (currentEmotion == ANGRY)
  {
    angryTwitchAngle += 0.5f;
    angerIntensityAngle += 0.2f;

    if (!angryTwitching && now > angryTwitchTimer) {
      angryTwitching = true;
      angryTwitchTimer = now + random(1000, 2500);
    }
    if (angryTwitching) {
      angryTwitchOffset = sin(angryTwitchAngle * 5.0f) * 1.5f;
      if (now > angryTwitchTimer) {
        angryTwitching = false;
        angryTwitchOffset = 0;
      }
    }

    // Steam puffs
    if (now > steamSpawnTimer) {
      for (int i = 0; i < MAX_STEAM; i++) {
        if (!steamPuffs[i].active) {
          steamPuffs[i].active = true;
          // spawn from sides (ears)
          int side = (random(2) == 0) ? -1 : 1;
          steamPuffs[i].x = (side == -1) ? random(10, 30) : random(130, 150);
          steamPuffs[i].y = 80 + random(0, 15);
          steamPuffs[i].vy = -((float)random(5, 15) / 10.0f);
          steamPuffs[i].radius = random(4, 10);
          steamPuffs[i].alpha = 1.0f;
          break;
        }
      }
      steamSpawnTimer = now + random(150, 400); // fast steam
    }
    for (int i = 0; i < MAX_STEAM; i++) {
      if (steamPuffs[i].active) {
        steamPuffs[i].y += steamPuffs[i].vy;
        steamPuffs[i].radius += 0.1f;
        steamPuffs[i].alpha -= 0.02f;
        if (steamPuffs[i].alpha <= 0) steamPuffs[i].active = false;
      }
    }

    // Lightning (Removed)
  }

  // Normal blink & Double Blink (INNOCENT uses slower blink speed)
  float blinkSpeed = (currentEmotion == INNOCENT) ? 0.08f : 0.25f;
  if (!isBlinking && !isDoubleBlinking && (now - lastBlinkTime > (unsigned long)blinkInterval))
  {
    if (random(100) < 15 && currentEmotion != INNOCENT && currentEmotion != DIZZY) {
      isDoubleBlinking = true;
      doubleBlinkPhase = 1;
    } else {
      isBlinking = true;
    }
    blinkInterval = random(2000, 6000);
  }

  if (isDoubleBlinking) {
    float dbSpeed = blinkSpeed * 0.8f; // 20% slower than normal blink
    if (doubleBlinkPhase == 1) { // closing 1
      blinkState += dbSpeed;
      if (blinkState >= 1.0f) { blinkState = 1.0f; doubleBlinkPhase = 2; }
    } else if (doubleBlinkPhase == 2) { // opening 1
      blinkState -= dbSpeed;
      if (blinkState <= 0.3f) { blinkState = 0.3f; doubleBlinkPhase = 3; }
    } else if (doubleBlinkPhase == 3) { // closing 2
      blinkState += dbSpeed;
      if (blinkState >= 1.0f) { blinkState = 1.0f; doubleBlinkPhase = 4; }
    } else if (doubleBlinkPhase == 4) { // opening 2
      blinkState -= dbSpeed;
      if (blinkState <= 0) {
        blinkState = 0;
        isDoubleBlinking = false;
        doubleBlinkPhase = 0;
        lastBlinkTime = now;
      }
    }
  } else {
    if (isBlinking)
    {
      blinkState += blinkSpeed;
      if (blinkState >= 1.0f)
      {
        blinkState = 1.0f;
        isBlinking = false;
        lastBlinkTime = now;
      }
    }
    else if (blinkState > 0)
    {
      blinkState -= blinkSpeed;
      if (blinkState < 0)
        blinkState = 0;
    }
  }

  // --- NEUTRAL: Idle gaze wander & Effects ---
  if (currentEmotion == NEUTRAL)
  {
    idleHoverAngle += 0.03f;
    
    // Fireflies
    if (now > nextFireflyTimer) {
      for (int i=0; i<MAX_FIREFLIES; i++) {
        if (!fireflies[i].active) {
          fireflies[i].active = true;
          fireflies[i].x = random(10, 150);
          fireflies[i].y = random(80, 140);
          fireflies[i].vx = (random(-10, 10) / 20.0f);
          fireflies[i].vy = -(random(5, 12) / 20.0f);
          fireflies[i].alpha = 1.0f;
          break;
        }
      }
      nextFireflyTimer = now + random(500, 2000);
    }
    for (int i=0; i<MAX_FIREFLIES; i++) {
      if (fireflies[i].active) {
        fireflies[i].x += fireflies[i].vx;
        fireflies[i].y += fireflies[i].vy;
        fireflies[i].alpha -= 0.005f;
        if (fireflies[i].alpha <= 0 || fireflies[i].y < -5) fireflies[i].active = false;
      }
    }

    if (!mpuActive) {
      if (now > idleGazeTimer) {
        idleTargetX = (float)random(-8, 9);
        idleTargetY = (float)random(-4, 5);
        idleGazeTimer = now + random(1500, 4000);
      }
      targetX += (idleTargetX - targetX) * 0.02f;
      targetY += (idleTargetY - targetY) * 0.02f;
    }
  }
}

void RobotEyes::draw(LGFX_Sprite *spr)
{
  spr->fillScreen(TFT_BLACK);

  if (currentEmotion == WARNING_ANIM) {
    int wX = (160 - WARNING_FRAME_WIDTH) / 2;
    int wY = (128 - WARNING_FRAME_HEIGHT) / 2;
    spr->drawBitmap(wX, wY, warning_frames[warningFrame], WARNING_FRAME_WIDTH, WARNING_FRAME_HEIGHT, 0xF800);
    spr->setTextColor(0xF800, TFT_BLACK);
    spr->setTextDatum(textdatum_t::top_center);
    spr->drawString("WARNING", 80, wY + WARNING_FRAME_HEIGHT + 10);
    return;
  }
  
  int centerX = 80 + (int)eyeOffsetX;
  int centerY = 64 + (int)eyeOffsetY;
  int drawY = centerY;

  if (currentEmotion == ALARM_RINGING) {
    drawY = centerY + (int)(sin(millis() / 20.0f) * 4.0f);
    
    int bellX = 80;
    int bellY = 18;
    int shake = (int)(sin(millis() / 30.0f) * 3.0f);
    bellX += shake;
    
    // Draw Bell
    spr->fillCircle(bellX, bellY, 10, TFT_ORANGE);
    spr->fillRect(bellX - 10, bellY, 21, 12, TFT_BLACK); // cut off the bottom half
    spr->fillRect(bellX - 12, bellY - 2, 25, 4, TFT_ORANGE); // Base rim
    spr->fillCircle(bellX, bellY + 2, 3, TFT_YELLOW); // clapper
    
    // Draw ringing lines
    if ((millis() / 100) % 2 == 0) {
       spr->drawLine(bellX - 15, bellY - 8, bellX - 22, bellY - 12, TFT_WHITE);
       spr->drawLine(bellX + 15, bellY - 8, bellX + 22, bellY - 12, TFT_WHITE);
    }
    
    // Draw the time text nicely below the bell
    spr->setTextFont(2);
    spr->setTextSize(1);
    spr->setTextDatum(textdatum_t::top_center);
    spr->setTextColor(TFT_WHITE, TFT_BLACK);
    spr->drawString(timeString, 80, 26);
  } else if (currentEmotion == CLOCK_MODE) {
    // CLOCK MODE DESIGN (Stacked Layout)
    extern String weatherCity;
    
    // Background gradient or simple clear (already cleared in main draw loop)
    
    // 1. Top Section: Location
    spr->setTextFont(2);
    spr->setTextSize(1);
    spr->setTextDatum(textdatum_t::top_center);
    spr->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    // Replace commas or %20 for display
    String displayCity = weatherCity;
    displayCity.replace("%20", " ");
    spr->drawString(displayCity, 80, 5);

    // 2. Middle Section: Time (Using custom color)
    spr->setTextFont(4); 
    spr->setTextSize(2);
    spr->setTextDatum(textdatum_t::middle_center);
    
    // Extract RGB from clockColor to create a darker glow
    uint8_t r = (clockColor >> 11) & 0x1F;
    uint8_t g = (clockColor >> 5) & 0x3F;
    uint8_t b = clockColor & 0x1F;
    uint16_t glowColor = ((r >> 2) << 11) | ((g >> 2) << 5) | (b >> 2); // 25% brightness
    
    // Glow effect
    spr->setTextColor(glowColor, TFT_BLACK);
    spr->drawString(timeString, 80 - 2, 60 - 2);
    spr->drawString(timeString, 80 + 2, 60 + 2);
    spr->drawString(timeString, 80 - 2, 60 + 2);
    spr->drawString(timeString, 80 + 2, 60 - 2);
    
    // Main Time text
    spr->setTextColor(clockColor, TFT_BLACK);
    spr->drawString(timeString, 80, 60);

    // 3. Bottom Section: Weather
    if (weatherIcon != "") {
        spr->setTextFont(2);
        spr->setTextSize(1);
        spr->setTextDatum(textdatum_t::bottom_center);
        spr->setTextColor(TFT_WHITE, TFT_BLACK);
        
        String bottomStr = "";
        if (weatherIcon == "loading") {
            int spin = (millis() / 200) % 4;
            if (spin == 0) bottomStr = "Fetching Weather -";
            else if (spin == 1) bottomStr = "Fetching Weather \\";
            else if (spin == 2) bottomStr = "Fetching Weather |";
            else if (spin == 3) bottomStr = "Fetching Weather /";
        } else {
            String tempStr = String((int)weatherTemp) + "C";
            bottomStr = weatherCondition + "  |  " + tempStr;
        }
        
        spr->drawString(bottomStr, 80, 120);
    }
    
    return;
  }

  if (currentEmotion == SLEEPY || currentEmotion == ASLEEP)
  {
    drawY = centerY + (int)sleepBreathY;
  }
  else if (currentEmotion == NEUTRAL)
  {
    drawY = centerY + (int)(sin(idleHoverAngle) * 2.0f);
    
    // Draw Fireflies
    for (int i=0; i<MAX_FIREFLIES; i++) {
      if (fireflies[i].active) {
        int fx = (int)fireflies[i].x;
        int fy = (int)fireflies[i].y;
        uint16_t fColor = (fireflies[i].alpha > 0.5f) ? 0xFFE0 : 0x7BE0; // Yellow-green
        spr->drawPixel(fx, fy, fColor);
        // tiny soft glow cross
        if (fireflies[i].alpha > 0.3f) {
          spr->drawPixel(fx-1, fy, 0x0A20);
          spr->drawPixel(fx+1, fy, 0x0A20);
          spr->drawPixel(fx, fy-1, 0x0A20);
          spr->drawPixel(fx, fy+1, 0x0A20);
        }
      }
    }
  }

  // Draw teardrop (SAD) - behind eyes
  if (currentEmotion == SAD && sadTearActive) {
    int tx = centerX + (int)sadTearX;
    int ty = (int)sadTearY;
    // Very light grey teardrop (ghost colour, not bright blue)
    spr->fillEllipse(tx, ty, (int)(sadTearW / 2), (int)(sadTearH / 2), 0xC618);
  }

  // Background particles for HAPPY (Hearts)
  if (currentEmotion == HAPPY) {
    uint16_t pinkColor = 0xF80F; // Bright pink #FF007F
    for (int i = 0; i < MAX_HEARTS; i++) {
      if (hearts[i].active) {
        int hX = (int)hearts[i].x;
        int hY = (int)hearts[i].y;
        int s = (int)hearts[i].size;
        // Draw heart shape: two circles and a triangle
        spr->fillCircle(hX - s/2, hY, s/2, pinkColor);
        spr->fillCircle(hX + s/2, hY, s/2, pinkColor);
        spr->fillTriangle(hX - s, hY, hX + s, hY, hX, hY + s + 1, pinkColor);
      }
    }
  }

  // Background particles for DIZZY (Spirals & Stars)
  if (currentEmotion == DIZZY) {
    uint16_t spiralColor = 0x18E3; // dim cyan/blue for spiral
    uint16_t dizzStarColor = 0xFFE0; // yellow stars
    
    // Background spirals (3 sweeping arcs)
    for (int i = 0; i < 3; i++) {
      float a = dizzyBackgroundAngle + (i * TWO_PI / 3.0f);
      int sx = centerX + (int)(cos(a) * 80.0f);
      int sy = centerY + (int)(sin(a) * 80.0f);
      spr->drawLine(centerX, centerY, sx, sy, spiralColor); // simple sweep representation
    }

    // Orbiting rings and stars around the head/eyes
    int orbitRx = 70;
    int orbitRy = 20; // flattened ellipse to look 3D
    // Draw the ring path as dotted curve
    for (float a = 0; a < TWO_PI; a += 0.4f) {
      int rx = centerX + (int)(cos(a) * orbitRx);
      int ry = centerY - 30 + (int)(sin(a) * orbitRy);
      spr->drawPixel(rx, ry, spiralColor);
    }
    
    // Draw two orbiting stars on the ring
    for (int i = 0; i < 2; i++) {
      float sa = dizzyOrbitAngle + (i * PI);
      int stx = centerX + (int)(cos(sa) * orbitRx);
      int sty = centerY - 30 + (int)(sin(sa) * orbitRy);
      // Bring stars to front if they are on the bottom half of the orbit (sin > 0)
      if (sin(sa) > 0) { 
        drawRealStar(spr, stx, sty, 5, dizzStarColor);
      } else {
        // Draw dim star if it's behind the eyes
        drawRealStar(spr, stx, sty, 3, 0xCE59); // darker yellow
      }
    }
  }

  // Background particles for INNOCENT (Stars & Fireworks)
  if (currentEmotion == INNOCENT) {
    uint16_t yellowColor = 0xFFE0; // TFT_YELLOW
    for (int i = 0; i < MAX_STARS; i++) {
      if (stars[i].active) {
        int sX = (int)stars[i].x;
        int sY = (int)stars[i].y;
        drawRealStar(spr, sX, sY, 4, yellowColor);
      }
    }
    if (firework.active && firework.alpha > 0) {
      int fX = (int)firework.x;
      int fY = (int)firework.y;
      float r = firework.radius;
      // Draw 8 firework sparks in a circle
      for (int a = 0; a < 8; a++) {
        float angle = a * (PI / 4.0f);
        int sx = fX + (int)(cos(angle) * r);
        int sy = fY + (int)(sin(angle) * r);
        spr->fillCircle(sx, sy, 1, yellowColor);
      }
    }
  }

  // Background particles for ASLEEP (Zzz) moved to the end of draw() to overlay eyelids

  // Background particles for ANGRY (Steam)
  if (currentEmotion == ANGRY) {
    uint16_t steamColor = 0xFC60; // Dark Orange #FF8C00

    // Steam puffs
    for (int i = 0; i < MAX_STEAM; i++) {
      if (steamPuffs[i].active && steamPuffs[i].alpha > 0) {
        // approximate alpha by drawing outline if fading
        if (steamPuffs[i].alpha > 0.5f) {
          spr->fillCircle((int)steamPuffs[i].x, (int)steamPuffs[i].y, (int)steamPuffs[i].radius, steamColor);
        } else {
          spr->drawCircle((int)steamPuffs[i].x, (int)steamPuffs[i].y, (int)steamPuffs[i].radius, steamColor);
        }
      }
    }
  }

  // GUARDING: asymmetric eye sizes for peeking feel
  if (currentEmotion == GUARDING) {
    // Draw Siren in 3D side-view perspective
    int bx = 80;
    int by = 8;
    float rA = guardSirenAngle * PI / 180.0f;
    float bA = (guardSirenAngle + 180.0f) * PI / 180.0f;
    
    float rdx = cos(rA), rdz = sin(rA);
    float bdx = cos(bA), bdz = sin(bA);
    
    // 1. Draw sideways beams (Background)
    // Red beam with fade
    int rL = (int)(45.0f * rdx);
    int rW = (int)(15.0f * fabs(rdx));
    if (abs(rL) > 2) {
        spr->fillTriangle(bx, by, bx + rL, by - rW, bx + rL, by + rW, 0x5000); // Dark outer
        int rL2 = (int)(rL * 0.66f);
        int rW2 = (int)(rW * 0.66f);
        spr->fillTriangle(bx, by, bx + rL2, by - rW2, bx + rL2, by + rW2, 0xA000); // Medium mid
        int rL1 = (int)(rL * 0.33f);
        int rW1 = (int)(rW * 0.33f);
        spr->fillTriangle(bx, by, bx + rL1, by - rW1, bx + rL1, by + rW1, TFT_RED); // Bright core
    }
    
    // Blue beam with fade
    int bL = (int)(45.0f * bdx);
    int bW = (int)(15.0f * fabs(bdx));
    if (abs(bL) > 2) {
        spr->fillTriangle(bx, by, bx + bL, by - bW, bx + bL, by + bW, 0x000A); // Dark outer
        int bL2 = (int)(bL * 0.66f);
        int bW2 = (int)(bW * 0.66f);
        spr->fillTriangle(bx, by, bx + bL2, by - bW2, bx + bL2, by + bW2, 0x0015); // Medium mid
        int bL1 = (int)(bL * 0.33f);
        int bW1 = (int)(bW * 0.33f);
        spr->fillTriangle(bx, by, bx + bL1, by - bW1, bx + bL1, by + bW1, TFT_BLUE); // Bright core
    }
    
    // 2. Draw Dome and Base
    spr->fillRoundRect(70, -2, 20, 18, 6, 0xCE79); // Light silver dome
    spr->fillRect(62, 0, 36, 4, 0x4208); // Dark grey base at top edge
    
    // 3. Draw Lens Flares (Foreground) - grows when pointing forward
    if (rdz > 0) {
        int fx = bx + (int)(8.0f * rdx);
        int fSize = (int)(rdz * 9.0f);
        if (fSize > 0) spr->fillCircle(fx, by, fSize, TFT_RED);
    }
    if (bdz > 0) {
        int fx = bx + (int)(8.0f * bdx);
        int fSize = (int)(bdz * 9.0f);
        if (fSize > 0) spr->fillCircle(fx, by, fSize, TFT_BLUE);
    }

    // Determine which way pupils are panning
    float panDir = curX; // positive = looking right
    // Eye that is "looking towards" gets bigger (the leading eye)
    float leftScale  = 1.0f - (panDir / 12.0f) * 0.15f;  // right pan = left eye shrinks
    float rightScale = 1.0f + (panDir / 12.0f) * 0.15f;  // right pan = right eye grows
    leftScale  = constrain(leftScale,  0.80f, 1.20f);
    rightScale = constrain(rightScale, 0.80f, 1.20f);
    int leftW  = (int)(eyeW * leftScale);
    int leftH  = (int)(eyeH * leftScale);
    int rightW = (int)(eyeW * rightScale);
    int rightH = (int)(eyeH * rightScale);
    drawEye(spr, centerX - eyeGap, drawY, -1, leftW, leftH);
    drawEye(spr, centerX + eyeGap, drawY,  1, rightW, rightH);
  } else {
    // INNOCENT: Larger and taller eyes
    if (currentEmotion == INNOCENT) {
      int innW = (int)(eyeW * 1.3f);
      int innH = (int)(eyeH * 1.25f);
      drawEye(spr, centerX - eyeGap, drawY, -1, innW, innH);
      drawEye(spr, centerX + eyeGap, drawY,  1, innW, innH);
    } else {
      drawEye(spr, centerX - eyeGap, drawY, -1);
      drawEye(spr, centerX + eyeGap, drawY,  1);
    }
  }

  // Smile mouth for HAPPY
  if (currentEmotion == HAPPY)
  {
    int mR = 8;
    int mY = constrain(drawY + eyeH / 2 + 8, mR, 127 - mR);
    spr->fillCircle(centerX, mY, mR, TFT_WHITE);
    spr->fillRect(centerX - mR, mY - mR, mR * 2 + 1, mR, TFT_BLACK);

    // Orbiting Star around the top of the eyes
    int orbitRx = 50;
    int orbitRy = 15;
    int starX = centerX + (int)(cos(happyStarAngle) * orbitRx);
    int starY = drawY - 35 + (int)(sin(happyStarAngle) * orbitRy);
    
    // Star tail (trailing slightly behind)
    int tailX = centerX + (int)(cos(happyStarAngle - 0.4f) * orbitRx);
    int tailY = drawY - 35 + (int)(sin(happyStarAngle - 0.4f) * orbitRy);
    spr->drawLine(starX, starY, tailX, tailY, 0xFFE0); // Yellow tail
    
    drawRealStar(spr, starX, starY, 4, 0xFFE0); // Yellow star
  }

  // Panic sweat drop
  if (currentEmotion == PANIC && panicSweatActive) {
    int sweatEyeX = centerX + panicSweatSide * eyeGap;
    int sweatX = sweatEyeX + panicSweatSide * (eyeW / 2 - 4);
    int sweatY = drawY - eyeH / 2 - 6 + (int)panicSweatY;
    spr->fillEllipse(sweatX, sweatY, 3, 5, 0xDEDB); // very light grey
  }

  // Anger Vein (Anime popping vein symbol) drawn LAST so it overlaps the eye
  if (currentEmotion == ANGRY) {
    int cx = centerX + eyeGap + (eyeW / 2) + 5; // Top right of the right eye
    int cy = drawY - (eyeH / 2) - 5;
    uint16_t veinColor = 0xF800; // Bright Red

    int r = 5 + (int)(sin(angryTwitchAngle * 2.0f) * 1.5f); // 3 to 6
    int w = 6; // offset from center (larger gap between pieces)
    
    // Draw 4 distinct corners that bulge outward
    // Top-Left corner (Left to Up -> 270 to 360)
    spr->drawArc(cx - w, cy - w, r, r - 3, 270, 360, veinColor);
    // Top-Right corner (Up to Right -> 0 to 90)
    spr->drawArc(cx + w, cy - w, r, r - 3, 0, 90, veinColor);
    // Bottom-Right corner (Right to Down -> 90 to 180)
    spr->drawArc(cx + w, cy + w, r, r - 3, 90, 180, veinColor);
    // Bottom-Left corner (Down to Left -> 180 to 270)
    spr->drawArc(cx - w, cy + w, r, r - 3, 180, 270, veinColor);
  }

  // Foreground particles for ASLEEP (Zzz) drawn LAST so it overlaps the eyelids
  if (currentEmotion == ASLEEP) {
    uint16_t zColor = 0x7E3F; // Light blue
    for (int i = 0; i < MAX_ZZZ; i++) {
      if (zParticles[i].active) {
        int zx = (int)zParticles[i].x;
        int zy = (int)zParticles[i].y;
        int zs = (int)(zParticles[i].size * 4.0f); // Size modifier (grows as it floats)
        
        // Draw a Z using 3 thick lines
        spr->drawLine(zx - zs, zy - zs, zx + zs, zy - zs, zColor); // Top bar
        spr->drawLine(zx + zs, zy - zs, zx - zs, zy + zs, zColor); // Diagonal
        spr->drawLine(zx - zs, zy + zs, zx + zs, zy + zs, zColor); // Bottom bar
        
        // Thicken the Z
        spr->drawLine(zx - zs, zy - zs + 1, zx + zs, zy - zs + 1, zColor); 
      }
    }
  }

  // --- DRAW TIMER PROGRESS (POMODORO) ---
  if (timerActive && timerProgress > 0.0f) {
    // Sleek progress bar at the bottom edge (4px thick)
    int barWidth = (int)(160.0f * timerProgress);
    spr->fillRect(0, 124, barWidth, 4, TFT_GREEN);
  }

  // --- DRAW WEATHER INFO ---
  if (weatherIcon != "") {
    spr->setTextFont(1);
    spr->setTextSize(1);
    spr->setTextDatum(textdatum_t::top_right);
    spr->setTextColor(TFT_WHITE, TFT_BLACK); // Make it bright white to ensure visibility
    
    String wStr;
    if (weatherIcon == "loading") {
        int spin = (millis() / 200) % 4;
        if (spin == 0) wStr = " - ";
        else if (spin == 1) wStr = " \\ ";
        else if (spin == 2) wStr = " | ";
        else if (spin == 3) wStr = " / ";
    } else {
        wStr = String((int)weatherTemp) + "C ";
        if (weatherIcon == "sun") wStr += "O"; // Use O as sun symbol in basic font
        else if (weatherIcon == "cloud") wStr += "C"; // C as cloud
        else if (weatherIcon == "rain") wStr += "R"; // R as rain
    }
    
    spr->drawString(wStr, 155, 5);
  }
}

void RobotEyes::drawEye(LGFX_Sprite *spr, int x, int y, int side, int wOverride, int hOverride)
{
  uint16_t scleraColor = TFT_WHITE;
  int eW = (wOverride > 0) ? wOverride : eyeW;
  int eH = (hOverride > 0) ? hOverride : eyeH;

  // HAPPY
  if (currentEmotion == HAPPY)
  {
    float effectiveBlink = max(happyBlinkState, transitionBlink);
    int squish = (int)(max(0.0f, happyBounceY) * 0.4f);
    int rawW   = eW + (int)happyEyeWidthPulse; // breathing swell
    int rawH   = eH - squish;
    int happyH = (effectiveBlink > 0) ? max(3, (int)(rawH * (1.0f - effectiveBlink * 0.90f))) : rawH;
    int rr     = min(eyeR, happyH / 2 - 1);

    spr->fillRoundRect(x - rawW / 2, y - happyH / 2, rawW, happyH, rr, scleraColor);

    if (happyH > 10 && effectiveBlink < 0.6f)
    {
      int pR = 16; // Larger than idle 14 for extra joy
      int pX = x + (int)curX;
      int pY = constrain(y + (int)curY, y - happyH / 2 + pR + 2, y + happyH / 2 - pR - 2);
      
      spr->fillCircle(pX, pY, pR, TFT_BLACK);
      // Extra large happy highlights
      spr->fillCircle(pX - 4, pY - 5, 4, TFT_WHITE);
      spr->fillCircle(pX + 5, pY + 3, 2, TFT_WHITE);

      int chBaseX = x + side * (rawW / 2 - 6);
      int chY     = y + happyH / 2 + 6;
      uint16_t pinkColor = 0xFDDF; // Lighter Pink (to contrast with hearts)
      for (int i = 0; i < 3; i++) {
        spr->fillCircle(chBaseX + side * i * 4, chY, 2, pinkColor);
      }
    }
    return;
  }

  // SLEEPY / ASLEEP
  if (currentEmotion == SLEEPY || currentEmotion == ASLEEP)
  {
    int eyeTop = y - eH / 2;
    spr->fillRoundRect(x - eW / 2, eyeTop, eW, eH, eyeR, scleraColor);
    int lidR = 44;
    int lidCY = eyeTop - lidR + (int)(eH * sleepyLidHeight);
    int lidCX = x + (-side) * (int)(sleepyLidHeight * 5);
    spr->fillCircle(lidCX, lidCY, lidR, TFT_BLACK);
    int lidLineY = eyeTop + (int)(eH * sleepyLidHeight);
    int visH = y + eH / 2 - lidLineY;
    if (currentEmotion == SLEEPY && visH > 8)
    {
      int pX = x + (int)curX + (int)sleepMicroDriftX;
      int pY = constrain(y + (int)curY, lidLineY + pupilR + 2, y + eH / 2 - pupilR - 2);
      int effR = (visH < 24) ? max(4, pupilR - (24 - visH) / 3) : pupilR;
      spr->fillCircle(pX, pY, effR, TFT_BLACK);
      if (visH > 18)
        spr->fillCircle(pX + 3, pY - 3, 2, TFT_WHITE);
    }
    return;
  }

  // INNOCENT
  if (currentEmotion == INNOCENT)
  {
    float eb  = max(blinkState, transitionBlink);
    int   iH  = eH; // uses the override size passed in from draw()
    int   iHe = (eb > 0) ? max(3, (int)(iH * (1.0f - eb))) : iH;
    int   rr  = min(eyeR + 3, iHe / 2 - 1);

    spr->fillRoundRect(x - eW / 2, y - iHe / 2, eW, iHe, rr, scleraColor);

    if (iHe > 14 && eb < 0.6f)
    {
      int pR = 22; // Much larger pupil for innocent
      int pX = x + (int)curX;
      int pY = constrain(y + (int)curY, y - iHe / 2 + pR + 2, y + iHe / 2 - pR - 2);
      spr->fillCircle(pX, pY, pR, TFT_BLACK);
      spr->fillCircle(pX - 6, pY - 7, 5, TFT_WHITE); // Larger highlight
      spr->fillCircle(pX + 6, pY + 4, 3, TFT_WHITE);
    }
    return;
  }

  // GUARDING (with large dilated pupils + peeking asymmetry handled in draw())
  if (currentEmotion == GUARDING)
  {
    float effectiveBlink = max(blinkState, transitionBlink);
    int h = max(2, (int)(eH * (1.0f - effectiveBlink)));

    spr->fillRoundRect(x - eW / 2, y - h / 2, eW, h, eyeR, scleraColor);

    if (h > 6)
    {
      int pX = x + (int)curX;
      int pY = constrain(y + (int)curY, y - h / 2 + pupilR + 2, y + h / 2 - pupilR - 2);

      // Large pulsing dilated pupil (not slit). Never shrinks below pupilR.
      int effR = pupilR + (int)(sin(guardPupilPulseAngle) + 1.0f);
      spr->fillCircle(pX, pY, effR, TFT_BLACK);
      spr->fillCircle(pX - 3, pY - 3, 2, TFT_WHITE);
      spr->fillCircle(pX + 3, pY + 2, 1, TFT_WHITE);
    }
    return;
  }

  // STANDARD RENDERING (Neutral, Angry, Sad, Dizzy, Wakeup, Panic)
  float effectiveBlink = max(blinkState, transitionBlink);
  int h = max(2, (int)(eH * (1.0f - effectiveBlink)));

  // SAD: droopy top lid (shallow black arc clipping the top)
  if (currentEmotion == SAD) {
    int sadY = y - h / 2 + (int)(sin(sadLidAngle) * 2.0f); // slight sway
    spr->fillRoundRect(x - eW / 2, sadY, eW, h, eyeR, scleraColor);
    // Clip top with a shallow black arc to look heavy-lidded
    int lidClipR = eW + 10;
    spr->fillCircle(x, sadY - lidClipR + 8, lidClipR, TFT_BLACK);
  } else if (currentEmotion == ANGRY) {
    // Narrow eyes
    int angryH = h - 12; // Narrower
    if (angryH < 4) angryH = 4;
    spr->fillRoundRect(x - eW / 2, y - angryH / 2, eW, angryH, eyeR, scleraColor);
    
    // Pupils
    int pX = x + (int)curX;
    int pY = constrain(y + (int)curY, y - angryH / 2 + pupilR + 2, y + angryH / 2 - pupilR - 2);
    int effR = pupilR - 4; // small glaring pupils
    if (angryH > 8) {
      spr->fillCircle(pX, pY, effR, TFT_BLACK);
      spr->fillCircle(pX + 3, pY - 3, 2, TFT_WHITE);
    }

    // Deep Furrowed Eyebrow (V-shape clipping the top inner corner)
    int twitchY = (int)angryTwitchOffset;
    if (side == -1) {
      // Left eye brow goes down towards the center (right)
      spr->fillTriangle(x - eW/2 - 5, y - angryH/2 - 5 + twitchY, 
                        x + eW/2 + 5, y - angryH/2 - 10 + twitchY, 
                        x + eW/2 + 5, y + angryH/2 - 10 + twitchY, TFT_BLACK);
    } else {
      // Right eye brow goes down towards the center (left)
      spr->fillTriangle(x + eW/2 + 5, y - angryH/2 - 5 + twitchY, 
                        x - eW/2 - 5, y - angryH/2 - 10 + twitchY, 
                        x - eW/2 - 5, y + angryH/2 - 10 + twitchY, TFT_BLACK);
    }
  } else {
    // Standard rendering
    spr->fillRoundRect(x - eW / 2, y - h / 2, eW, h, eyeR, scleraColor);
    if (h > 8)
    {
      int pX = x + (int)curX;
      int pY = constrain(y + (int)curY, y - h / 2 + pupilR + 2, y + h / 2 - pupilR - 2);

      int effR = pupilR;
      if (currentEmotion == DIZZY)
        effR = pupilR - 2 + (int)(sin(dizzyAngle) * 2);
      if (currentEmotion == PANIC || currentEmotion == ALARM_RINGING)
        effR = 5; // Small terrified pupils

      // DIZZY: spiral ghost trail
      if (currentEmotion == DIZZY) {
        for (int t = 0; t < 3; t++) {
          int gX = x + (int)(sin(dizzyTrailAngle[t]) * 10.0f);
          int gY = y + (int)(cos(dizzyTrailAngle[t]) * 10.0f);
          int gR = effR - (t + 1) * 2;
          if (gR > 1) spr->drawCircle(gX, gY, gR, 0x4208); // very dark grey ghost
        }
      }

      spr->fillCircle(pX, pY, effR, TFT_BLACK);
      spr->fillCircle(pX + 3, pY - 3, 2, TFT_WHITE);

      // PANIC: concentric ring around pupil
      if (currentEmotion == PANIC || currentEmotion == ALARM_RINGING) {
        spr->drawCircle(pX, pY, effR + 3, 0x8410); // dark grey ring
        spr->drawCircle(pX, pY, effR + 6, 0x4208); // darker outer ring
      }
    }
  }
}