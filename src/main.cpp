#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "RobotEyes.h"

// --- HARDWARE PINS ---
#define RX_PIN 18     // Camera RX
#define TX_PIN 17     // Camera TX
#define TOUCH_PIN 14  // Capacitive Touch Sensor
#define VIBE_PIN 13   // Vibrator Motor Module

// I2S Microphone Pins
#define I2S_WS 15
#define I2S_SD 7
#define I2S_SCK 16
#define I2S_PORT I2S_NUM_0

// Hardware Objects
Adafruit_MPU6050 mpu;
RobotEyes eyes;

// Display Driver Setup
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_SH110x _panel_instance;
  lgfx::Bus_I2C      _bus_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 0; cfg.freq_write = 400000;
      cfg.pin_sda = 4; cfg.pin_scl = 5; cfg.i2c_addr = 0x3C;
      _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.panel_width = 128; cfg.panel_height = 64;
      cfg.offset_x = 2;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX display;
LGFX_Sprite sprite(&display);

// Interaction Logic
unsigned long lastInteractionTime = 0;
unsigned long emotionOverrideTimer = 0;
bool hasEmotionOverride = false;

// Touch-to-Happy transition state
bool wakeupFromTouch = false;
unsigned long wakeupTouchTime = 0;
bool innocentOverride = false;
unsigned long innocentReleaseTime = 0;

// --- VOICE AUDIO ARCHITECTURE ---
#define SAMPLE_RATE 16000
volatile bool keywordDetected = false; 
TaskHandle_t audioTaskHandle;

// Forward declaration of camera function
bool processCameraData();

// --- THE AI THREAD RUNNING ON CORE 0 (ESP-SR) ---
// --- SIMPLE I2S AUDIO TASK (crash-safe, no PSRAM required) ---
// Reads raw I2S mic data into a circular buffer.
// ESP-SR is skipped because the Boya flash chip on this N16R8 board
// boots in DIO mode and PSRAM is not available at runtime.
// When a model partition is properly flashed and PSRAM works, replace this
// with the full ESP-SR pipeline.

#define INFER_WINDOW 24000  // 1.5s @ 16kHz
static int16_t audio_buffer[INFER_WINDOW];
static int ring_position = 0;

#define GAIN_FACTOR 3
#define INFERENCE_EVERY_SAMPLES 5333  // Run check every ~0.33s

void audioInferenceTask(void *pvParameters) {
  memset(audio_buffer, 0, sizeof(audio_buffer));
  ring_position = 0;

  int samples_since_last_inference = 0;
  int16_t max_amplitude = 0;

  Serial.println("🎙️ Raw I2S Audio Monitor Active (ESP-SR bypassed - no PSRAM)");

  while (true) {
    // Read 32-bit I2S samples (microphone sends 32-bit padded data)
    #define BUF_LEN 256
    int32_t sBuffer[BUF_LEN];
    size_t bytesIn = 0;

    esp_err_t res = i2s_read(I2S_PORT, &sBuffer, sizeof(sBuffer), &bytesIn, portMAX_DELAY);

    if (res == ESP_OK && bytesIn > 0) {
      int chunk_samples = bytesIn / 4;

      for (int i = 0; i < chunk_samples; i++) {
        // Shift down from 32-bit to 16-bit and apply gain
        int32_t amplified = (sBuffer[i] >> 14) * GAIN_FACTOR;
        if (amplified > 32767)  amplified = 32767;
        else if (amplified < -32768) amplified = -32768;

        int16_t sample16 = (int16_t)amplified;
        audio_buffer[ring_position] = sample16;
        ring_position = (ring_position + 1) % INFER_WINDOW;

        if (abs(sample16) > max_amplitude) max_amplitude = abs(sample16);
        samples_since_last_inference++;
      }
    }

    if (samples_since_last_inference >= INFERENCE_EVERY_SAMPLES) {
      samples_since_last_inference = 0;

      if (max_amplitude > 1500) {
        Serial.printf("🔊 Peak Volume: %d (no KWS model loaded)\n", max_amplitude);
      }
      max_amplitude = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}


// Flag to track MPU6050 availability
bool mpuAvailable = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- ESP32 ROBOT BOOTING ---");

  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); 

  pinMode(TOUCH_PIN, INPUT);
  pinMode(VIBE_PIN, OUTPUT);
  analogWrite(VIBE_PIN, 0);

  if (!display.init()) {
    Serial.println("❌ OLED display init FAILED!");
  } else {
    Serial.println("✅ OLED display initialized");
  }
  display.setBrightness(128); 
  display.setRotation(2);
  sprite.setColorDepth(1); 
  sprite.createSprite(128, 64);
  eyes.init();
  
  mpuAvailable = mpu.begin();
  if (!mpuAvailable) {
    Serial.println("⚠️ Failed to find MPU6050 - continuing without it");
  } else {
    Serial.println("✅ MPU6050 found");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // Match original: mic sends 32-bit padded data
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };
  
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_start(I2S_PORT);

  // START AUDIO TASK ON CORE 0 (large stack for audio buffer)
  xTaskCreatePinnedToCore(
    audioInferenceTask, "AudioAI", 32768, NULL, 1, &audioTaskHandle, 0
  );
  
  lastInteractionTime = millis();
}

void loop() {
  bool cameraDetected = processCameraData();
  
  bool physicallyMoved = false;
  bool strongPhysical  = false;

  // Only read MPU6050 if it was successfully initialized
  if (mpuAvailable) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float totalAccel = sqrt(pow(a.acceleration.x,2) + pow(a.acceleration.y,2) + pow(a.acceleration.z,2));
    float totalGyro  = sqrt(pow(g.gyro.x,2)  + pow(g.gyro.y,2)  + pow(g.gyro.z,2));
    eyes.setEyeOffset(-a.acceleration.x * 1.5, a.acceleration.y * 1.5);

    if (totalAccel > 22.0) {                                        
        if (!hasEmotionOverride) eyes.setEmotion(ANGRY);
        emotionOverrideTimer = millis();
        hasEmotionOverride   = true;
        physicallyMoved = strongPhysical = true;
    } else if (totalGyro > 5.0) {                                   
        if (!hasEmotionOverride) eyes.setEmotion(DIZZY);
        emotionOverrideTimer = millis();
        hasEmotionOverride   = true;
        physicallyMoved = strongPhysical = true;
    } else if (abs(a.acceleration.x) > 2.0 || abs(a.acceleration.y) > 2.0) {
        physicallyMoved = true;
    }
  }

  bool isTouched = (digitalRead(TOUCH_PIN) == HIGH);
  static bool wasTouched = false;
  
  static unsigned long lastTapTime = 0;
  static int tapCount = 0;
  bool risingEdge = isTouched && !wasTouched;
  
  if (risingEdge) {
      unsigned long tapNow = millis();
      if (tapCount > 0 && (tapNow - lastTapTime < 500)) {
          tapCount++;
      } else {
          tapCount = 1;
      }
      lastTapTime = tapNow;
  }
  if (tapCount > 0 && (millis() - lastTapTime > 500)) {
      tapCount = 0;
  }

  Emotion curEmotion = eyes.getEmotion();

  bool isSleeping = (curEmotion == SLEEPY || curEmotion == ASLEEP);
  
  if (!isTouched && wasTouched) {
      if (innocentOverride) innocentReleaseTime = millis();
      wakeupFromTouch = false;
  }

  if (tapCount >= 2 && !innocentOverride && !isSleeping && curEmotion != ANGRY && curEmotion != DIZZY) {
      eyes.setEmotion(INNOCENT);
      emotionOverrideTimer = millis();
      hasEmotionOverride   = true;
      innocentOverride     = true;
      innocentReleaseTime  = 0;
      tapCount = 0;
  }

  if (keywordDetected) {
      lastInteractionTime = millis(); 
      keywordDetected = false; 
      
      if (isSleeping) {
          eyes.setEmotion(WAKEUP);
          emotionOverrideTimer = millis();
          hasEmotionOverride  = true;
      } else if (curEmotion != ANGRY && curEmotion != DIZZY && !innocentOverride) {
          eyes.setEmotion(INNOCENT); 
          emotionOverrideTimer = millis();
          hasEmotionOverride   = true;
          innocentOverride     = true;
          innocentReleaseTime  = 0;
      }
  }

  if (isTouched) {
      lastInteractionTime = millis();
      if (isSleeping) {
          if (!wakeupFromTouch) {
              eyes.setEmotion(WAKEUP);
              emotionOverrideTimer = millis();
              hasEmotionOverride  = true;
              wakeupFromTouch     = true;
              wakeupTouchTime     = millis();
          }
          if (wakeupFromTouch && curEmotion == WAKEUP && (millis() - wakeupTouchTime > 700)) {
              eyes.setEmotion(HAPPY);
              emotionOverrideTimer = millis();
              hasEmotionOverride   = true;
              wakeupFromTouch      = false;
          }
      } else if (curEmotion == WAKEUP && wakeupFromTouch) {
          if (millis() - wakeupTouchTime > 700) {
              eyes.setEmotion(HAPPY);
              emotionOverrideTimer = millis();
              hasEmotionOverride   = true;
              wakeupFromTouch      = false;
          }
      } else if (curEmotion == INNOCENT && innocentOverride) {
          emotionOverrideTimer = millis();
      } else if (curEmotion != ANGRY && curEmotion != DIZZY && curEmotion != WAKEUP && !innocentOverride) {
          if (curEmotion != HAPPY) {
              eyes.setEmotion(HAPPY);
              emotionOverrideTimer = millis();
              hasEmotionOverride   = true;
          } else {
              emotionOverrideTimer = millis();
          }
      }
  }

  if (physicallyMoved) {
      lastInteractionTime = millis();
      if (!strongPhysical && (curEmotion == SLEEPY || curEmotion == ASLEEP)) {
          eyes.setEmotion(WAKEUP);
          emotionOverrideTimer = millis();
          hasEmotionOverride   = true;
      }
  }

  if (cameraDetected && !isSleeping) {
      lastInteractionTime = millis();
  }

  wasTouched = isTouched;

  curEmotion = eyes.getEmotion();
  
  if (innocentOverride && !isTouched) {
      if (millis() - innocentReleaseTime > 3000) {
          eyes.setEmotion(NEUTRAL);
          innocentOverride   = false;
          hasEmotionOverride = false;
      }
  }

  if (hasEmotionOverride && !isTouched && !innocentOverride && (millis() - emotionOverrideTimer > 3000)) {
      eyes.setEmotion(NEUTRAL);
      hasEmotionOverride = false;
  }

  if (!hasEmotionOverride && !isTouched && !innocentOverride) {
      unsigned long idleTime = millis() - lastInteractionTime;
      if      (idleTime > 20000 && eyes.getEmotion() != ASLEEP)  eyes.setEmotion(ASLEEP);
      else if (idleTime > 10000 && eyes.getEmotion() != SLEEPY && eyes.getEmotion() != ASLEEP)  eyes.setEmotion(SLEEPY);
  }

  curEmotion = eyes.getEmotion();
  
  if (isTouched && curEmotion == HAPPY) {
      int purr = 195 + (int)(sin(millis() / 30.0) * 60);
      analogWrite(VIBE_PIN, purr);
  } else if (curEmotion == INNOCENT) {
      unsigned long t = millis() % 1200;
      if      (t < 100) analogWrite(VIBE_PIN, 120);
      else if (t < 200) analogWrite(VIBE_PIN, 0);
      else if (t < 300) analogWrite(VIBE_PIN, 90);
      else              analogWrite(VIBE_PIN, 0);
  } else if (wakeupFromTouch) {
      unsigned long dt = millis() - wakeupTouchTime;
      if      (dt < 80)  analogWrite(VIBE_PIN, 255);
      else if (dt < 160) analogWrite(VIBE_PIN, 0);
      else if (dt < 250) analogWrite(VIBE_PIN, 210);
      else               analogWrite(VIBE_PIN, 0);
  } else if (curEmotion == ANGRY && hasEmotionOverride) {
      unsigned long dt = millis() - emotionOverrideTimer;
      if      (dt < 130) analogWrite(VIBE_PIN, 240);
      else if (dt < 230) analogWrite(VIBE_PIN, 0);
      else if (dt < 360) analogWrite(VIBE_PIN, 210);
      else               analogWrite(VIBE_PIN, 0);
  } else if (curEmotion == DIZZY && hasEmotionOverride) {
      unsigned long t = millis();
      int wave = 120 + (int)(abs(sin(t / 70.0)) * 110);
      analogWrite(VIBE_PIN, ((t / 110) % 3 == 0) ? wave : 0);
  } else if (curEmotion == SLEEPY) {
      unsigned long sp = millis() % 2000;
      if      (sp < 150) analogWrite(VIBE_PIN, (int)(sp / 150.0f * 90));
      else if (sp < 300) analogWrite(VIBE_PIN, (int)((300 - sp) / 150.0f * 90));
      else               analogWrite(VIBE_PIN, 0);
  } else if (curEmotion == ASLEEP) {
      unsigned long sp = millis() % 3500;
      if      (sp < 200) analogWrite(VIBE_PIN, (int)(sp / 200.0f * 65));
      else if (sp < 400) analogWrite(VIBE_PIN, (int)((400 - sp) / 200.0f * 65));
      else               analogWrite(VIBE_PIN, 0);
  } else {
      analogWrite(VIBE_PIN, 0);
  }

  eyes.update();
  eyes.draw(&sprite);
  sprite.pushSprite(0, 0);

  // Prevent Watchdog Crash on Core 1
  vTaskDelay(pdMS_TO_TICKS(10));
}

bool processCameraData() {
  bool detected = false;
  static String packetBuffer = "";
  
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      packetBuffer.trim();
      if (packetBuffer.startsWith("F:")) {
        int commaIndex = packetBuffer.indexOf(',');
        if (commaIndex > 0) {
          String xStr = packetBuffer.substring(2, commaIndex);
          String yStr = packetBuffer.substring(commaIndex + 1);
          float x = xStr.toInt() / 100.0; 
          float y = yStr.toInt() / 100.0;
          eyes.lookAt(x, y);
          detected = true;
        }
      }
      packetBuffer = "";
    } else {
      packetBuffer += c;
      if (packetBuffer.length() > 50) packetBuffer = "";
    }
  }
  
  if (!detected && millis() % 100 == 0) {
      eyes.lookAt(0, 0);
  }
  return detected;
}

