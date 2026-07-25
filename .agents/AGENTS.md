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
