## LovyanGFX & Continuous Loop Performance
When adding new UI overlays (like menus, text, or widgets) to a continuous physics or animation loop (e.g., `RobotEyes` loop):
- **DO NOT** clear, redraw, or `pushSprite` every frame unconditionally.
- **ALWAYS** implement a dirty flag (`needsRedraw`).
- Only execute rendering logic and SPI bus pushes when state has actually changed (e.g., coordinates updated, text changed).
