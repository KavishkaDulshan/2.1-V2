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

## PlatformIO Project Setup

This project uses **PlatformIO** inside VS Code for build compilation and dependency management, departing from the classic single-file Arduino IDE layout.

### 1. Dual-Framework Setup (ESP-IDF + Arduino)
To support the advanced audio hardware processing pipelines alongside Arduino libraries, the project is configured with a hybrid framework setup:
```ini
framework = espidf, arduino
```
This builds the core system using Espressif's native **ESP-IDF (v4.4.7)** while compiling the Arduino core as an ESP-IDF component. It permits access to low-level FreeRTOS tasks and ESP-IDF APIs directly inside an Arduino `.cpp` file.

### 2. PSRAM Configuration
For heavy calculations (such as audio inference and vector graphics rendering), the ESP32-S3's internal 320KB RAM is supplemented by its **8MB Octal PSRAM**:
- `board_build.arduino.memory_type = qio_opi`: Instructs the linker to use Quad SPI Flash and Octal SPI PSRAM (`opi`).
- `-D BOARD_HAS_PSRAM`: Passes the compiler define ensuring the Arduino allocator is aware of the external RAM.

---

## Translation from Arduino IDE (Original) to PlatformIO

Converting the original Arduino sketch in the `original/` directory to this compiled PlatformIO project required several codebase adaptations:

### 1. File Formats & Compilers
- **Strict C++ Rules**: The main file was renamed from `main.ino` to `main.cpp`. C++ rules require all functions to be declared before they are called. Thus, forward declarations (like `bool processCameraData();`) were added.
- **Includes**: Explicitly included `#include <Arduino.h>` which the Arduino IDE usually adds automatically in the background.

### 2. Library Isolation
- **Global vs Local**: Instead of installing libraries globally via the Arduino IDE Library Manager, libraries are strictly version-controlled inside `platformio.ini` under `lib_deps`.
- **Custom Local Library**: The Edge Impulse machine learning library (`a2.1-KWS_inferencing`) is located locally inside the `lib/` directory so PlatformIO bundles it automatically during builds.

### 3. Task Management (FreeRTOS)
- **Multithreading**: In Arduino, everything is typically sequential. In PlatformIO, the audio monitoring is separated into a dedicated FreeRTOS thread (`audioInferenceTask`) pinned to Core 0 using `xTaskCreatePinnedToCore()`. 
- This ensures the UI rendering (which runs on Core 1's main execution loop) remains completely stutter-free and unaffected by heavy microsecond audio sampling processes.

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

---

## Future Keyword Spotting (KWS) Options

The ESP32-S3 is highly capable of running real-time voice keyword spotting. If you plan to implement this in the future under PlatformIO, here are the two primary paths:

### Path A: Edge Impulse (TensorFlow Lite Micro)
* **Status**: Ready. The required library `a2.1-KWS_inferencing` is already in your `lib/` directory and compiles successfully under PlatformIO.
* **Requirements**: Runs completely inside internal SRAM (does not require PSRAM to work).
* **Implementation Plan**:
  1. Re-add the header `#include <a2.1-KWS_inferencing.h>` in `src/main.cpp`.
  2. Implement an audio signal reader callback:
     ```cpp
     int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
         for (size_t i = 0; i < length; i++) {
             int idx = (ring_position + offset + i) % INFER_WINDOW;
             out_ptr[i] = (float)audio_buffer[idx];
         }
         return 0;
     }
     ```
  3. Inside `audioInferenceTask`, invoke the classifier regularly:
     ```cpp
     signal_t signal;
     signal.total_length = INFER_WINDOW;
     signal.get_data = &microphone_audio_signal_get_data;
     ei_impulse_result_t result = { 0 };
     EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
     if (err == EI_IMPULSE_OK) {
         // Check result.classification[i] confidence scores (e.g. "wake" score > 0.6)
     }
     ```

### Path B: Espressif ESP-SR (WakeNet & MultiNet)
* **Status**: Requires hardware configuration.
* **Requirements**: Requires working Octal PSRAM at the hardware boot level.
* **Implementation Plan**:
  1. Run `pio run -t menuconfig` in the VS Code terminal to open Espressif's SDK configuration menu.
  2. Under `Component Config -> ESP32S3-Specific Settings`, enable PSRAM support (`CONFIG_SPIRAM=y`) so that the ESP-SR Acoustic Front End (AFE) can allocate its internal memory frames.
  3. Flash the custom voice models (such as `model.bin`) to the partition labeled `model` at offset `0x10000` using `esptool.py` or custom flash commands.
  4. Include `esp_afe_sr_iface.h` and use the default `ESP_AFE_SR_HANDLE` interface to feed and fetch audio chunks.

Kavishka Dulshan