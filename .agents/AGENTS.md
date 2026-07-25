## LovyanGFX & Continuous Loop Performance
When adding new UI overlays (like menus, text, or widgets) to a continuous physics or animation loop (e.g., `RobotEyes` loop):
- **DO NOT** clear, redraw, or `pushSprite` every frame unconditionally.
- **ALWAYS** implement a dirty flag (`needsRedraw`).
- Only execute rendering logic and SPI bus pushes when state has actually changed (e.g., coordinates updated, text changed).

## Canvas Resizing & Origins
When resizing a UI canvas or sprite bounds:
- **ALWAYS** search the codebase for hardcoded center points (e.g., `centerY = 96`) and origin offsets.
- You must mathematically shift these origins to match the new center of the resized canvas (e.g., changing `192 / 2` to `128 / 2`).

## Touch Coordinate Calibration Protocol (LovyanGFX / XPT2046)
The raw hardware ADC outputs for the XPT2046 touch controller on this device do not match the 240x320 screen coordinates.
- **ALWAYS** manually map the raw ADC outputs to the screen dimensions using the Arduino `map()` function immediately after `display.getTouch(&tx, &ty)`:
  ```cpp
  tx = map(tx, 2735, 254, 0, 240);
  ty = map(ty, -4718, -184, 0, 320);
  if (tx < 0) tx = 0; else if (tx > 240) tx = 240;
  if (ty < 0) ty = 0; else if (ty > 320) ty = 320;
  ```
- **NEVER** attempt to change `cfg.offset_rotation = 1` or `cfg.x_min`/`y_min` in the LovyanGFX setup, as it will break the raw output boundaries.

## Full-Screen Mode Architecture
When implementing full-screen modes (like Clock or Dashboards) on a split-screen display system:
- **DO NOT** hijack a smaller sub-sprite (e.g., a 128px eye sprite) to draw the mode.
- **ALWAYS** decouple the full-screen mode into its own dedicated class/sprite (e.g., `ClockUI` at 240x320).
- Selectively skip the split-screen rendering (eyes and chat) in the main loop when the full-screen mode is active.

## Touch State Management & Propagation
When propagating global touch states (like `isScreenTouched` and `wasScreenTouched`) across multiple conditional UI layers (Dashboard, Clock, Eyes, Chat) in the main loop:
- **ALWAYS** update the previous state variable (e.g., `wasScreenTouched = isScreenTouched;`) at the **very END** of the execution loop (e.g., right before `vTaskDelay()`).
- **NEVER** update `wasScreenTouched` prematurely in the middle of the loop, as downstream UI components relying on `if (isScreenTouched && !wasScreenTouched)` for one-shot touch locks will fail.

## LovyanGFX Touch Tap Detection (Touch Up)
When `isScreenTouched` becomes `false` (Touch Up), LovyanGFX immediately resets `tx` and `ty` coordinates to `0, 0`.
- **NEVER** use the current `tx`/`ty` to detect where a tap occurred during a Touch Up event.
- **ALWAYS** capture `firstTouchX` and `firstTouchY` on the initial Touch Down.
- On Touch Up, compare `_lastTouchX` against `firstTouchX` to determine if the movement was within a small threshold (e.g., `< 15px`) to register a tap.

## Emotion State Restoration
When exiting a full-screen override (like `CLOCK_MODE`) or resetting the robot's facial state, **ALWAYS** set `eyes.baseEmotion = NEUTRAL;` and `eyes.setEmotion(NEUTRAL);`. 
- **NEVER** set `baseEmotion` to transient emotions like `HAPPY` or `ANGRY`, as it will permanently overwrite the robot's default idle animation loop until rebooted.
