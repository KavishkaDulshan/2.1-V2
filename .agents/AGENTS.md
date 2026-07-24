## LovyanGFX & Continuous Loop Performance
When adding new UI overlays (like menus, text, or widgets) to a continuous physics or animation loop (e.g., `RobotEyes` loop):
- **DO NOT** clear, redraw, or `pushSprite` every frame unconditionally.
- **ALWAYS** implement a dirty flag (`needsRedraw`).
- Only execute rendering logic and SPI bus pushes when state has actually changed (e.g., coordinates updated, text changed).

## Canvas Resizing & Origins
When resizing a UI canvas or sprite bounds:
- **ALWAYS** search the codebase for hardcoded center points (e.g., `centerY = 96`) and origin offsets.
- You must mathematically shift these origins to match the new center of the resized canvas (e.g., changing `192 / 2` to `128 / 2`).
