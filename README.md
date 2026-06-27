# ESP32-S3 Companion Robot (v2.1)

This project runs a companion robot control system on an **ESP32-S3-N16R8** development board. It features dynamic robot eye animations displayed on an OLED monitor, motion sensing (MPU6050), physical/haptic feedback (vibration motor and touch sensor), and a background I2S audio monitoring task.

---

## Hardware Configuration & Pin Mappings

| Module | Interface | Pin / Port | Notes |
| :--- | :--- | :--- | :--- |
| **OLED Display** (SH110x) | I2C | `SDA = 4`, `SCL = 5` | Driven by `LovyanGFX` |
| **MPU6050 Accelerometer** | I2C | `SDA = 8`, `SCL = 9` | Default ESP32-S3 I2C pins |
| **Capacitive Touch** | GPIO | `GPIO 14` | Input trigger |
| **Vibration Motor** | GPIO | `GPIO 13` | Analog PWM output |
| **I2S Microphone** | I2S | `WS = 15`, `SD = 7`, `SCK = 16` | Port: `I2S_NUM_0` |
| **Serial Camera Interface** | UART | `RX = 18`, `TX = 17` | Connected to UART1 |

---

## Directory Structure

```
2.1/
├── .gitignore                   # Optimized Git exclude configurations
├── platformio.ini               # PlatformIO project environment definitions
├── partitions.csv               # Flash partition layout for ESP32-S3 (16MB)
├── src/                         # Active PlatformIO Source
│   ├── main.cpp                 # Main setup, loop, and background tasks
│   ├── RobotEyes.cpp            # Eye physics & rendering logic
│   └── RobotEyes.h              # Eye structures and declarations
├── lib/                         # Project Libraries
│   └── a2.1-KWS_inferencing/    # Local Edge Impulse keyword spotting library
├── original/                    # Original Arduino IDE sketches (Reference)
│   ├── main.ino
│   ├── RobotEyes.cpp
│   └── RobotEyes.h
│   └── sketch.yaml
└── README.md                    # Project documentation
```

---

## Bug Fixes & Refactoring

The following critical issues were resolved to get the firmware running cleanly on the **ESP32-S3-N16R8**:

### 1. Board & PSRAM Configuration Fix
* **Issue**: The original configurations targeted an incorrect board (`4d_systems_esp32s3_gen4_r8n16`), causing boot errors and memory alignment issues.
* **Fix**: Updated `platformio.ini` to compile for the generic `esp32-s3-devkitc-1` dev board. Configured the flash size to `16MB` and the memory type to `qio_opi` (Octal SPI PSRAM) which matches the N16R8 hardware specification. Added the `-D BOARD_HAS_PSRAM` compiler flag.

### 2. I2C Bus Conflict & OLED Lockup Fix
* **Issue**: In `src/main.cpp`, a `Wire.begin(4, 5)` call was initialized right before the screen setup. The `LovyanGFX` display engine manages its own I2C bus driver on pins 4/5. Conflicting configurations on the same I2C port locked the bus and left the OLED completely blank.
* **Fix**: Removed the conflicting `Wire.begin(4, 5)` call. The OLED now initializes properly, and the MPU6050 successfully initializes on default I2C pins (`8`/`9`).

### 3. Core 0 Panic (`StoreProhibited`) Fix
* **Issue**: The `audioInferenceTask` running on Core 0 utilized an ESP-SR framework configuration. Without the proper model partition flashed or valid PSRAM settings at boot (the Boya flash chip initializes in `DIO` mode), the ESP-SR setup returned a `NULL` pointer, crashing the processor and causing an infinite reboot loop.
* **Fix**: Replaced the ESP-SR audio framework with a crash-safe, raw 32-bit I2S audio monitoring task (matching the original circular buffer logic from `main.ino`). The task runs stably on Core 0, allocating buffers safely from internal RAM without PSRAM dependency.

### 4. Watchdog Timer (WDT) Trigger Prevention
* **Issue**: Core 1's main execution path had no yield delays, risking watchdog triggers during heavy rendering loops.
* **Fix**: Inserted `vTaskDelay(pdMS_TO_TICKS(10))` at the end of the `loop()` function to yield CPU cycles to lower priority background tasks.

### 5. Repository Bloat & Tracking Cleanup
* **Issue**: The Git repository was tracking 2,000+ files because of the auto-generated dependency directories `managed_components/` and C++ compiler telemetry files `compile_commands.json`.
* **Fix**: Configured `.gitignore` to properly exclude `managed_components/` and `compile_commands.json`, reducing the repository tracking footprint to source-only files.
