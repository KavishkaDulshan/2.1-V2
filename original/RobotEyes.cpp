#include "RobotEyes.h"

void RobotEyes::init()
{
  randomSeed(analogRead(1));
}

void RobotEyes::setEmotion(Emotion e)
{
  currentEmotion = e;
  blinkState = 0;
  isBlinking = false;
  transitionBlink = 0.45f; // Quick blink on every emotion switch
  lastBlinkTime = millis(); // Prevent double-blink after transition

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
    // Deep Sleep Mode - uses SLEEPY drawEye with lid at 0.92
    sleepBreathAngle = 0;
    sleepyLidHeight = 0.92f;
    sleepPhase = 0;
    sleepPhaseTimer = millis();
    sleepTrembleAngle = 0;
    sleepMicroDriftX = 0;
    sleepMicroTargetX = 0;
    blinkState = 0; // SLEEPY drawEye handles rendering
    targetY = 20.0f;
    curY = 20.0f;
    easeFactor = 0.04f;
  }
  else if (e == HAPPY)
  {
    happyBounceAngle = 0;
    happyShimmerAngle = 0;
    easeFactor = 0.2f;
  }
  else if (e == SAD)
  {
    targetX = 0;
    targetY = 8.0f;
    easeFactor = 0.08f;
  }
  else if (e == INNOCENT)
  {
    targetX = 0;
    targetY = -5.0f;
    easeFactor = 0.15f;
  }
  else if (e == DIZZY)
  {
    dizzyAngle = 0;
    easeFactor = 0.3f; // Fast, erratic
  }
  else if (e == WAKEUP)
  {
    targetX = 0;
    targetY = 0;
    blinkState = 1.0f; // Start closed, will snap open in update
    easeFactor = 0.3f;
  }
  else
  {
    targetX = 0;
    targetY = 0;
    easeFactor = 0.2f;
  }
}

void RobotEyes::lookAt(float x, float y)
{
  // Only controls PUPIL
  targetX = constrain(x, -1.0, 1.0) * 14.0;
  targetY = constrain(y, -1.0, 1.0) * 10.0;
}

void RobotEyes::setEyeOffset(float x, float y)
{
  // Controls WHOLE EYE (Sclera) bounds
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

  // --- NEW: DIZZY ANIMATION ---
  if (currentEmotion == DIZZY)
  {
    dizzyAngle += 0.4f;
    // Spin pupils in circles
    targetX = sin(dizzyAngle) * 10.0f;
    targetY = cos(dizzyAngle) * 10.0f;
    // Wobble blink slightly
    blinkState = 0.2f + (sin(dizzyAngle * 0.5f) * 0.1f);
    return;
  }

  // --- NEW: WAKEUP ANIMATION ---
  if (currentEmotion == WAKEUP)
  {
    if (blinkState > 0.0f)
    {
      blinkState -= 0.15f; // Snap open rapidly
      if (blinkState <= 0)
        blinkState = 0;
    }
    return; // Hold wide open
  }

  // --- ASLEEP ANIMATION (Deep Sleep with REM phases) ---
  if (currentEmotion == ASLEEP)
  {
    sleepBreathAngle += 0.010f; // Very slow, heavy breathing
    sleepBreathY = sin(sleepBreathAngle) * 2.5f;
    sleepTrembleAngle += 0.08f;
    if (now - lastSleepCheck > 50)
    {
      switch (sleepPhase)
      {
      case 0: // Deep sleep: lid barely stirs
        sleepyLidHeight = 0.92f + sin(sleepTrembleAngle) * 0.012f;
        if (now - sleepPhaseTimer > (unsigned long)random(3000, 6000))
        {
          sleepPhase = 1;
          sleepPhaseTimer = now;
        }
        break;
      case 1: // REM: lid slowly drifts open (dreaming)
        sleepyLidHeight -= 0.008f;
        if (sleepyLidHeight <= 0.60f)
        {
          sleepyLidHeight = 0.60f;
          sleepPhase = 2;
          sleepPhaseTimer = now;
        }
        break;
      case 2: // REM: flutter briefly
        sleepyLidHeight = 0.60f + sin(sleepTrembleAngle * 1.5f) * 0.04f;
        if (now - sleepPhaseTimer > (unsigned long)random(400, 900))
        {
          sleepPhase = 3;
          sleepPhaseTimer = now;
        }
        break;
      case 3: // REM: close back to deep sleep
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
    blinkState = 0; // SLEEPY drawEye handles rendering via sleepyLidHeight
    return;
  }

  // --- HAPPY ---
  if (currentEmotion == HAPPY)
  {
    happyBounceAngle += 0.15f;
    happyBounceY = sin(happyBounceAngle) * 3.0f;
    happyShimmerAngle += 0.10f;
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
    return;
  }

  // --- SLEEPY (Your existing awesome state machine) ---
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
        // Lazy micro-drift: pupils slowly wander while drooping
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
    // Pupils gaze upward and sway gently side to side – alive and curious
    targetY = -6.0f + sin(innocentPulseAngle * 0.4f) * 2.0f;
    targetX = sin(innocentPulseAngle * 0.3f) * 5.0f;
  }

  // Normal blink (INNOCENT uses slower, more deliberate blink speed)
  float blinkSpeed = (currentEmotion == INNOCENT) ? 0.08f : 0.25f;
  if (!isBlinking && (now - lastBlinkTime > (unsigned long)blinkInterval))
  {
    isBlinking = true;
    blinkInterval = random(2000, 6000);
  }
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

void RobotEyes::draw(LGFX_Sprite *spr)
{
  spr->fillScreen(TFT_BLACK);

  // APPLY MPU6050 EYE OFFSET HERE
  int centerX = 64 + (int)eyeOffsetX;
  int centerY = 32 + (int)eyeOffsetY;
  int drawY = centerY;

  if (currentEmotion == HAPPY)
    drawY = centerY + (int)happyBounceY;
  if (currentEmotion == SLEEPY || currentEmotion == ASLEEP)
    drawY = centerY + (int)sleepBreathY;

  drawEye(spr, centerX - eyeGap, drawY, -1);
  drawEye(spr, centerX + eyeGap, drawY, 1);

  // Smile mouth for HAPPY (∪ crescent shape)
  if (currentEmotion == HAPPY)
  {
    int mR = 5;
    int mY = constrain(drawY + eyeH / 2 + 4, mR, 63 - mR);
    spr->fillCircle(centerX, mY, mR, TFT_WHITE);
    spr->fillRect(centerX - mR, mY - mR, mR * 2 + 1, mR, TFT_BLACK);
  }
}

void RobotEyes::drawEye(LGFX_Sprite *spr, int x, int y, int side)
{
  // HAPPY
  if (currentEmotion == HAPPY)
  {
    float effectiveBlink = max(happyBlinkState, transitionBlink);
    // Squish vertically at bounce peak for a springy feel
    int squish = (int)(max(0.0f, happyBounceY) * 0.4f);
    int rawH   = eyeH - squish;
    int happyH = (effectiveBlink > 0) ? max(3, (int)(rawH * (1.0f - effectiveBlink * 0.90f))) : rawH;
    int rr     = min(eyeR, happyH / 2 - 1);

    // Main eye: wide-open rounded rectangle
    spr->fillRoundRect(x - eyeW / 2, y - happyH / 2, eyeW, happyH, rr, TFT_WHITE);

    if (happyH > 10 && effectiveBlink < 0.6f)
    {
      // Large upward-gazing pupil in upper portion
      int pX = x + 1;
      int pY = y - happyH / 6;
      int pR = 9;
      pY = constrain(pY, y - happyH / 2 + pR + 2, y + happyH / 2 - pR - 2);
      spr->fillCircle(pX, pY, pR, TFT_BLACK);

      // Primary catchlight (upper-left, large)
      spr->fillCircle(pX - 3, pY - 4, 3, TFT_WHITE);
      // Secondary catchlight (lower-right, small)
      spr->fillCircle(pX + 4, pY + 2, 1, TFT_WHITE);

      // Cheek blush: clear horizontal row of 3 dots below eye, outer side
      int chBaseX = x + side * (eyeW / 2 - 6);
      int chY     = y + happyH / 2 + 6;
      for (int i = 0; i < 3; i++) {
        spr->fillCircle(chBaseX + side * i * 4, chY, 2, TFT_WHITE);
      }

      // Corner star sparkle: 3-line cross at outer upper corner
      int spX = x + side * (eyeW / 2 + 4);
      int spY = y - happyH / 2 - 3;
      spr->drawLine(spX - 3, spY,     spX + 3, spY,     TFT_WHITE); // horizontal
      spr->drawLine(spX,     spY - 3, spX,     spY + 3, TFT_WHITE); // vertical
      spr->drawLine(spX - 2, spY - 2, spX + 2, spY + 2, TFT_WHITE); // diagonal

      // Orbiting shimmer dot: figure-eight path around the eye
      int sX = x + (int)(cos(happyShimmerAngle) * (eyeW / 2 + 6));
      int sY = (y - happyH / 3) + (int)(sin(happyShimmerAngle * 2.0f) * 6);
      spr->fillCircle(sX, sY, 1, TFT_WHITE);
    }
    return;
  }

  // SLEEPY / ASLEEP (shared curved lid rendering)
  if (currentEmotion == SLEEPY || currentEmotion == ASLEEP)
  {
    int eyeTop = y - eyeH / 2;
    spr->fillRoundRect(x - eyeW / 2, eyeTop, eyeW, eyeH, eyeR, TFT_WHITE);
    int lidR = 44;
    int lidCY = eyeTop - lidR + (int)(eyeH * sleepyLidHeight);
    int lidCX = x + (-side) * (int)(sleepyLidHeight * 5);
    spr->fillCircle(lidCX, lidCY, lidR, TFT_BLACK);
    int lidLineY = eyeTop + (int)(eyeH * sleepyLidHeight);
    int visH = y + eyeH / 2 - lidLineY;
    // Only draw pupil for SLEEPY, not ASLEEP (eyes closed during sleep)
    if (currentEmotion == SLEEPY && visH > 8)
    {
      int pX = x + (int)curX + (int)sleepMicroDriftX;
      int pY = constrain(y + (int)curY, lidLineY + pupilR + 2, y + eyeH / 2 - pupilR - 2);
      int effR = (visH < 24) ? max(4, pupilR - (24 - visH) / 3) : pupilR;
      spr->fillCircle(pX, pY, effR, TFT_BLACK);
      if (visH > 18)
        spr->fillCircle(pX + 3, pY - 3, 2, TFT_WHITE);
    }
    return;
  }

  // INNOCENT (big round eyes, large pupils, prominent catchlights, alive gaze)
  if (currentEmotion == INNOCENT)
  {
    float eb  = max(blinkState, transitionBlink);
    int   iH  = 46; // slightly taller than normal eyeH (42)
    int   iHe = (eb > 0) ? max(3, (int)(iH * (1.0f - eb))) : iH;
    int   rr  = min(eyeR + 3, iHe / 2 - 1);

    spr->fillRoundRect(x - eyeW / 2, y - iHe / 2, eyeW, iHe, rr, TFT_WHITE);

    if (iHe > 14 && eb < 0.6f)
    {
      int pR = 11; // large pupil
      int pX = x + (int)curX;
      int pY = constrain(y + (int)curY, y - iHe / 2 + pR + 2, y + iHe / 2 - pR - 2);
      spr->fillCircle(pX, pY, pR, TFT_BLACK);

      // Large primary catchlight (upper-left)
      spr->fillCircle(pX - 4, pY - 5, 4, TFT_WHITE);
      // Secondary catchlight (lower-right)
      spr->fillCircle(pX + 4, pY + 3, 2, TFT_WHITE);

      // Subtle shimmer dot orbiting slowly
      int shX = x + (int)(cos(innocentPulseAngle) * (eyeW / 2 + 3));
      int shY = y - iHe / 3 + (int)(sin(innocentPulseAngle * 1.5f) * 3);
      spr->fillCircle(shX, shY, 1, TFT_WHITE);
    }
    return;
  }

  // STANDARD RENDERING (Neutral, Angry, Sad, Dizzy, Wakeup)
  float effectiveBlink = max(blinkState, transitionBlink);
  int h = max(2, (int)(eyeH * (1.0f - effectiveBlink)));
  spr->fillRoundRect(x - eyeW / 2, y - h / 2, eyeW, h, eyeR, TFT_WHITE);

  if (h > 8)
  {
    int pX = x + curX;
    int pY = constrain(y + (int)curY, y - h / 2 + pupilR + 2, y + h / 2 - pupilR - 2);

    // Dizzy pupil shrinks and expands slightly
    int effR = pupilR;
    if (currentEmotion == ANGRY)
      effR = pupilR - 3;
    if (currentEmotion == DIZZY)
      effR = pupilR - 2 + (int)(sin(dizzyAngle) * 2);

    spr->fillCircle(pX, pY, effR, TFT_BLACK);
    spr->fillCircle(pX + 3, pY - 3, 2, TFT_WHITE);
  }

  if (currentEmotion == ANGRY)
  {
    if (side == -1)
      spr->fillTriangle(x + eyeW / 2, y - eyeH / 2 + 15, x + eyeW / 2, y - eyeH / 2 - 5, x - 10, y - eyeH / 2 - 5, TFT_BLACK);
    else
      spr->fillTriangle(x - eyeW / 2, y - eyeH / 2 + 15, x - eyeW / 2, y - eyeH / 2 - 5, x + 10, y - eyeH / 2 - 5, TFT_BLACK);
  }
}