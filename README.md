# ESP32-S3 Companion Robot (v2.1)

This project runs a companion robot control system on an **ESP32-S3-N16R8** development board. It features dynamic robot eye animations displayed on an OLED monitor, motion sensing (MPU6050), physical/haptic feedback (vibration motor and touch sensor), background I2S audio monitoring, and seamless integration with a custom Flutter mobile application over Bluetooth Low Energy (BLE) and MQTT.

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
├── 2.1App/                      # Flutter Mobile Application Source Code
├── .gitignore                   # Optimized Git exclude configurations
├── platformio.ini               # PlatformIO project environment definitions
├── partitions.csv               # Flash partition layout for ESP32-S3 (16MB)
├── src/                         # Active PlatformIO Source
│   ├── main.cpp                 # Main setup, loop, and background tasks
│   ├── RobotEyes.cpp            # Eye physics, UI overlays, & rendering logic
│   ├── RobotEyes.h              # Eye structures and declarations
│   └── MqttManager.cpp          # MQTT communication and command parsing
├── lib/                         # Project Libraries
└── README.md                    # Project documentation
```

---

## Features & Capabilities

### 1. Dynamic Eye Physics & Emotion Engine
The core of the robot is an advanced physics-based eye rendering engine powered by **LovyanGFX**. The eyes exhibit fluid, spring-physics-based movements (`spring`, `damping`, `mass`) to simulate natural saccades and emotion. 
Supported states include: `NEUTRAL`, `HAPPY`, `SAD`, `ANGRY`, `SLEEPY`, `ASLEEP`, `DIZZY`, `PANIC`, and `INNOCENT`.

### 2. Flutter Mobile Application Integration
The robot is controlled via a custom cross-platform Flutter application (`2.1App`). 
- **BLE Provisioning:** The app uses Bluetooth Low Energy (`flutter_blue_plus`) to discover the robot and securely transmit WiFi credentials, setting up the robot without hardcoded network profiles.
- **MQTT Command & Control:** Once on the network, the app and robot communicate bidirectionally using an MQTT broker, allowing low-latency remote control over the robot's states and settings.

### 3. Dynamic Weather & Clock Mode
The robot features a responsive, animated clock mode with a real-time weather widget.
- **Live Weather API:** The Flutter app fetches real-time weather data via the **OpenWeatherMap API** and pushes updates (`weather_temp`, `weather_desc`, `weather_icon`) directly to the robot over MQTT. 
- **City Search & Caching:** The app includes a user-friendly city search dropdown and persistently caches the selected location (e.g., "London, UK") using `shared_preferences` so weather fetches automatically on boot.
- **Time-based Environments:** The clock's background animation fluidly shifts between daytime and nighttime themes depending on the hour (e.g., #085893 to #1576ab gradient with twinkles at night vs #99D4FF daylight).

### 4. Phone Notification Mirroring ("Look Here!")
The robot acts as an intelligent notification extension for your Android smartphone.
- **Background Isolate Listener:** The Flutter app runs a headless background Dart isolate using `flutter_notification_listener`. It intercepts incoming notifications even when the app is swiped away or the phone is locked.
- **App Filtering:** It specifically monitors 9 targeted applications: `WhatsApp`, `SMS`, `Phone Calls`, `Telegram`, `Gmail`, `YouTube`, `Facebook`, `Instagram`, and `TikTok`.
- **Interruptive Animations:** When a notification arrives, it pushes a lightning-fast MQTT payload. The robot instantly snaps its eyes downward to "look" at the screen, and beautifully rendered, pixel-perfect, color-accurate vector logos (drawn purely with LovyanGFX primitives) bounce onto the screen with the sender's name for 4.5 seconds.
- **Context-Aware Safety:** The robot intelligently ignores notifications if it is currently engaged in a focus task (like the Pomodoro timer) or an alarm mode, preventing unwanted distractions.

### 5. Productivity & Utility Overlays
- **Pomodoro Timer:** The user can start a focus session (e.g., 25 minutes) from the app. The robot displays a sleek, unobtrusive countdown timer overlay beneath its eyes.
- **Color Customization:** The clock font and UI elements can be dynamically color-shifted (e.g., Red, Green, Blue) via the app.

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

## ESP-SR (Keyword Spotting & Voice Commands) Architecture Plan

The ESP32-S3 is highly capable of running real-time voice keyword spotting and speech command recognition natively. This project is configured to use Espressif's native **ESP-SR** framework to leverage:

1. **Hardware Vector Acceleration**: 
   ESP-SR hooks directly into the ESP32-S3's native 128-bit SIMD vector engine. This accelerates matrix math and audio processing algorithms (like FFT and AFE filtering) up to 10x faster than standard scalar CPU instructions used in software emulation.
   
2. **Decoupled Memory Architecture**: 
   Instead of compiling large neural network weights directly into the firmware binary, ESP-SR stores voice models as an independent `model.bin` file inside a dedicated 4MB flash partition (`model`, configured in `partitions.csv`). At boot time, the ESP-SR memory manager dynamically maps this memory space.

3. **True Asynchronous Core Balancing**: 
   The resource-heavy I2S microphone processing, Acoustic Front End (AFE), Automatic Gain Control (AGC), and acoustic model inference run in a background FreeRTOS thread (`audioInferenceTask`) pinned to **Core 0**. This completely isolates voice processing and prevents it from blocking or stuttering the real-time LovyanGFX display animations and physics engines running concurrently on **Core 1**.

### Implementation Steps

#### 1. Hardware Boot PSRAM Configuration
ESP-SR requires working Octal PSRAM at the hardware level.
* Run `pio run -t menuconfig` inside the project directory to open Espressif's SDK configuration.
* Under `Component Config -> ESP32S3-Specific Settings`, verify that PSRAM support (`CONFIG_SPIRAM=y`) is enabled and configured to match the N16R8 module.

#### 2. Partition & Model Flashing
* The voice recognition acoustic model (`model.bin`) must be flashed into the dedicated `model` partition at its corresponding flash offset (defined dynamically or using Espressif's command line flash utilities).

#### 3. Core 0 Inference Loop
* Include `<esp_afe_sr_iface.h>` and `<esp_afe_sr_models.h>`.
* Initialize the Acoustic Front End (AFE) and set up the interface instance:
  ```cpp
  esp_afe_sr_iface_t *afe_handle = &ESP_AFE_SR_HANDLE;
  esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);
  ```
* Feed audio frames fetched from the I2S microphone into the AFE processor:
  ```cpp
  afe_handle->feed(afe_data, mic_data_buffer);
  ```
* Fetch processed AFE frames and run WakeNet (wake word detection) and MultiNet (command phrase recognition):
  ```cpp
  afe_fetch_result_t* result = afe_handle->fetch(afe_data);
  if (result && result->trigger_rv == trigger_wakeup) {
      // Handle voice triggers and pass command flags to Core 1
  }
  ```

Kavishka Dulshan...