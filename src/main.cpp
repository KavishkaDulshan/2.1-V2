#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// --- EDGE IMPULSE HEADER ---
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

#include "RobotEyes.h"
#include <ArduinoJson.h>
#include "BleManager.h"
#include "MqttManager.h"
#include <time.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>

// --- UTILITY CONFIG ---
const char* ntpServer = "pool.ntp.org";
long  gmtOffset_sec = 19800; // Default +5:30 (India)
int   daylightOffset_sec = 0;
bool  timeConfigured = false;

String weatherCity = "London,UK";
#define OPENWEATHER_API_KEY "90b5371f426c8985201312f80bfe9eb4"

time_t targetAlarmTime = 0;
unsigned long pomodoroEndTime = 0;
bool alarmTriggered = false;

// =========================================================================
// 🛑 DEBUG CONTROL SWITCH
// false = Standard operation, prints AI Thoughts and KeyWord Triggers
// true  = Prints raw microphone wave for Serial Plotter visualizer
#define DEBUG_AUDIO_WAVE false 
// =========================================================================

// --- HARDWARE CONFIGURATION ---
#define RX_PIN 18     
#define TX_PIN 17     
#define TOUCH_PIN 14  
#define VIBE_PIN 13   

#define I2S_WS 15
#define I2S_SD 7
#define I2S_SCK 16
#define I2S_PORT I2S_NUM_0

// --- EDGE IMPULSE CONSTANTS ---
#define SAMPLE_RATE 16000
#define AUDIO_CHUNK_SIZE 160
#define AUDIO_RING_BUFFER_SIZE 12000

Adafruit_MPU6050 mpu;
RobotEyes eyes;

// Display Setup
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 27000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 9;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 8;
      cfg.pin_rst          = 10;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 128;
      cfg.panel_height     = 160;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0; // Removed offset, use display.setRotation() instead
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = false;
      cfg.rgb_order        = true;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX display;
LGFX_Sprite sprite(&display);

volatile bool keyword_wake_word = false;
volatile bool keyword_cmd_sleep = false;
volatile bool keyword_cmd_guard = false;
volatile bool keyword_loud_noise = false;
unsigned long lastInteractionTime = 0;
unsigned long emotionOverrideTimer = 0;
bool hasEmotionOverride = false;
bool wakeupFromTouch = false;
unsigned long wakeupTouchTime = 0;
bool innocentOverride = false;
unsigned long innocentReleaseTime = 0;
bool guardMode = false;

TaskHandle_t i2sTaskHandle;
TaskHandle_t audioTaskHandle;
TaskHandle_t weatherTaskHandle;
bool processCameraData();

int16_t audio_buffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
volatile int ring_position = 0;
volatile int samples_since_last_inference = 0;
volatile int16_t max_amplitude = 0;

int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        int buffer_index = (ring_position + offset + i) % EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        out_ptr[i] = (float)audio_buffer[buffer_index];
    }
    return 0;
}

void i2sReadTask(void *pvParameters) {
    int32_t mic_dma_chunk[AUDIO_CHUNK_SIZE];
    while(true) {
        size_t bytes_read = 0;
        esp_err_t res = i2s_read(I2S_PORT, mic_dma_chunk, AUDIO_CHUNK_SIZE * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        if (res == ESP_OK && bytes_read > 0) {
            int samples_read = bytes_read / sizeof(int32_t);
            for (int i = 0; i < samples_read; ++i) {
                int32_t amplified = (mic_dma_chunk[i] >> 14) * 3; // GAIN_FACTOR = 3
                if (amplified > 32767) amplified = 32767;
                else if (amplified < -32768) amplified = -32768;
                
                int16_t sample16 = (int16_t)amplified;
                audio_buffer[ring_position] = sample16;
                ring_position = (ring_position + 1) % EI_CLASSIFIER_RAW_SAMPLE_COUNT;
                
                if (abs(sample16) > max_amplitude) max_amplitude = abs(sample16);
                samples_since_last_inference++;
            }
        }
    }
}

// --- THE VECTOR-ACCELERATED AI TASK CORE 0 ---
void audioInferenceTask(void *pvParameters) {
  if (!DEBUG_AUDIO_WAVE) Serial.println("✅ Edge Impulse Engine Locked & Active!");

  #define INFERENCE_EVERY_SAMPLES (EI_CLASSIFIER_RAW_SAMPLE_COUNT / 3) // Run AI every ~0.33 seconds (if 1s window)

  while (true) {
      if (samples_since_last_inference >= INFERENCE_EVERY_SAMPLES) {
          samples_since_last_inference = 0;

          signal_t signal;
          signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
          signal.get_data = &microphone_audio_signal_get_data;

          ei_impulse_result_t result = { 0 };
          EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
          if (err != EI_IMPULSE_OK) {
              Serial.printf("ERR: run_classifier failed (%d)\n", err);
              continue;
          }

          float wake_word_score = 0.0f;
          float cmd_sleep_score = 0.0f;
          float cmd_guard_score = 0.0f;
          float unknown_score = 0.0f;

          for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
              if (strstr(result.classification[ix].label, "wake") != NULL) wake_word_score = result.classification[ix].value;
              else if (strstr(result.classification[ix].label, "sleep") != NULL) cmd_sleep_score = result.classification[ix].value;
              else if (strstr(result.classification[ix].label, "guard") != NULL) cmd_guard_score = result.classification[ix].value;
              else if (strstr(result.classification[ix].label, "unknown") != NULL) unknown_score = result.classification[ix].value;
          }

          if (max_amplitude > 800) {
              if (!DEBUG_AUDIO_WAVE) {
                  Serial.println("\n--- RAW AI DIAGNOSTICS ---");
                  Serial.printf("🔊 Peak Volume: %d\n", max_amplitude);
                  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                      Serial.printf("  -> Label '%s': %5.1f%%\n", result.classification[ix].label, result.classification[ix].value * 100);
                  }
              }

              if (wake_word_score >= 0.60f && wake_word_score > unknown_score) {
                  keyword_wake_word = true;
                  Serial.println("🎯 ACTION: WAKE WORD TRIGGERED!");
                  samples_since_last_inference = -(EI_CLASSIFIER_RAW_SAMPLE_COUNT); // Add dead-time
              } else if (cmd_sleep_score >= 0.60f && cmd_sleep_score > unknown_score) {
                  keyword_cmd_sleep = true;
                  Serial.println("💤 ACTION: SLEEP COMMAND TRIGGERED!");
                  samples_since_last_inference = -(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
              } else if (cmd_guard_score >= 0.60f && cmd_guard_score > unknown_score) {
                  keyword_cmd_guard = true;
                  Serial.println("🛡️ ACTION: GUARD COMMAND TRIGGERED!");
                  samples_since_last_inference = -(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
              } else if (max_amplitude > 30000) {
                  keyword_loud_noise = true;
                  Serial.println("💥 ACTION: LOUD NOISE DETECTED!");
                  samples_since_last_inference = -(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
              } else {
                  if (!DEBUG_AUDIO_WAVE) Serial.println("❌ ACTION: Ignored (Did not cross 60% confidence)");
              }
              if (!DEBUG_AUDIO_WAVE) Serial.println("--------------------------");
          }
          max_amplitude = 0;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void weatherTask(void *pvParameters) {
    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            WiFiClient client;
            client.setTimeout(10);
            if (client.connect("api.openweathermap.org", 80, 5000)) {
                // Build a fully URL-encoded query string character by character
                String safeCity = "";
                for (unsigned int i = 0; i < weatherCity.length(); i++) {
                    char c = weatherCity[i];
                    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == ',') {
                        safeCity += c;
                    } else {
                        safeCity += '%';
                        safeCity += String(c, HEX);
                    }
                }

                String request = "GET /data/2.5/weather?q=" + safeCity + "&units=metric&appid=" + String(OPENWEATHER_API_KEY) + " HTTP/1.1\r\n";
                request += "Host: api.openweathermap.org\r\n";
                request += "Connection: close\r\n";
                request += "\r\n";
                client.print(request);

                // Wait for data
                unsigned long timeout = millis();
                while (client.available() == 0) {
                    if (millis() - timeout > 5000) {
                        Serial.println("Weather client timeout");
                        client.stop();
                        break;
                    }
                }

                // Skip HTTP headers — read until blank line
                while (client.available()) {
                    String line = client.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) break; // blank line = end of headers
                }

                // Read the JSON body
                String payload = "";
                while (client.available()) {
                    payload += (char)client.read();
                }
                client.stop();
                payload.trim();

                Serial.println("Weather payload: " + payload);

                if (payload.length() > 0 && payload.startsWith("{")) {
                    StaticJsonDocument<2048> doc;
                    DeserializationError error = deserializeJson(doc, payload);
                    if (!error) {
                        eyes.weatherTemp = doc["main"]["temp"].as<float>();
                        String mainWeather = doc["weather"][0]["main"].as<String>();
                        Serial.println("Parsed Weather: " + mainWeather + ", Temp: " + String(eyes.weatherTemp));
                        if (mainWeather == "Clear") eyes.weatherIcon = "sun";
                        else if (mainWeather == "Rain" || mainWeather == "Drizzle" || mainWeather == "Thunderstorm") eyes.weatherIcon = "rain";
                        else eyes.weatherIcon = "cloud";
                        eyes.weatherCondition = mainWeather;
                    } else {
                        Serial.println("Weather JSON parse error: " + String(error.c_str()));
                    }
                } else {
                    Serial.println("Weather: bad payload or not JSON");
                }
            } else {
                Serial.println("Weather: failed to connect");
            }
        }

        // Retry quickly until data is fetched, then wait 10 minutes
        uint32_t waitTime = 15000;
        if (eyes.weatherIcon != "" && eyes.weatherIcon != "loading") {
            waitTime = 600000;
        }
        
        // Wait for next fetch or a notification to fetch immediately
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitTime));
    }
}

void setup() {
  Serial.begin(115200);
  delay(3000); 

  // Initialize BLE Server for Provisioning FIRST to guarantee memory
  BleManager::init();

  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); 

  pinMode(TOUCH_PIN, INPUT);
  pinMode(VIBE_PIN, OUTPUT);
  analogWrite(VIBE_PIN, 0);

  display.init(); 
  display.setBrightness(128); 
  display.setRotation(1); // KEEP THIS AT 1 (Hardware is happy here)
  sprite.setColorDepth(16); 
  
  // FIX: Swap RGB order so RED shows as Red, not Blue
  auto cfg = display.getPanel()->config();
  cfg.rgb_order = true; 
  display.getPanel()->config(cfg);
  
  sprite.createSprite(160, 128);
  eyes.init();
  
  Wire.begin(4, 5); // Initialize I2C for MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, 
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT
  };
  
  const i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_start(I2S_PORT);

  xTaskCreatePinnedToCore(i2sReadTask, "I2SRead", 4096, NULL, 5, &i2sTaskHandle, 0);
  xTaskCreatePinnedToCore(audioInferenceTask, "AudioAI", 16384, NULL, 4, &audioTaskHandle, 0);
  
  // Initialize MQTT Manager
  MqttManager::init(&eyes);
  
  // Start Weather Task
  xTaskCreatePinnedToCore(weatherTask, "WeatherTask", 8192, NULL, 1, &weatherTaskHandle, 1);
  
  lastInteractionTime = millis();
}

void loop() {
  // Check if we received new Wi-Fi credentials via BLE
  if (BleManager::hasNewCredentials()) {
      Serial.println("Attempting Wi-Fi Connection...");
      
      // CRITICAL: Shut down BLE completely before attempting Wi-Fi.
      // The ESP32 shares a single antenna. Having BLE active during Wi-Fi scan often causes NO_AP_FOUND.
      BleManager::stop();
      delay(500); // Give the radio time to switch modes

      WiFi.mode(WIFI_STA); // Explicitly set station mode
      WiFi.disconnect();   // Clear any old connections
      delay(100);

      WiFi.begin(BleManager::getSsid().c_str(), BleManager::getPassword().c_str());
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
          delay(500);
          Serial.print(".");
          attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\nWi-Fi Connected successfully!");
          Serial.print("IP Address: ");
          Serial.println(WiFi.localIP());
          
          // Initialize time
          configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
          timeConfigured = true;
          Serial.println("NTP Time Sync requested.");
      } else {
          Serial.println("\nFailed to connect to Wi-Fi. Please restart the robot to turn BLE back on and try again.");
      }
      BleManager::clearCredentials();
  }
  
  // Process MQTT Messages
  MqttManager::loop();

  // --- TIME & UTILITY LOGIC ---
  if (timeConfigured) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 10)) {
          char timeStringBuff[10];
          strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
          eyes.timeString = String(timeStringBuff);
          
          // Check Alarm
          if (targetAlarmTime != 0) {
              time_t now;
              time(&now);
              if (now >= targetAlarmTime && !alarmTriggered) {
                  Serial.println("ALARM TRIGGERED!");
                  eyes.setEmotion(ALARM_RINGING);
                  eyes.baseEmotion = ALARM_RINGING; // Temporarily force base
                  hasEmotionOverride = true;
                  emotionOverrideTimer = millis() + 60000; // Ring for 1 minute
                  alarmTriggered = true;
                  targetAlarmTime = 0; // Clear it
              }
          }
      }
  }

  // Handle Alarm Reset
  if (eyes.getEmotion() == ALARM_RINGING) {
      if (millis() > emotionOverrideTimer) {
          // Timeout finished
          eyes.baseEmotion = NEUTRAL;
          hasEmotionOverride = false;
      }
  }

  // Check Pomodoro Timer
  if (pomodoroEndTime != 0) {
      unsigned long nowMs = millis();
      if (nowMs < pomodoroEndTime) {
          eyes.timerActive = true;
          // Calculate progress (Assuming 25 min max for now, or we need to store start time. 
          // For simplicity, just make it a generic active state, or track total duration)
          // Let's just track it generically or we can set timerProgress from MqttManager
          eyes.timerProgress = 1.0f - ((float)(pomodoroEndTime - nowMs) / (25 * 60 * 1000.0f)); 
          if (eyes.timerProgress < 0.0f) eyes.timerProgress = 0.0f;
          if (eyes.timerProgress > 1.0f) eyes.timerProgress = 1.0f;
      } else {
          Serial.println("TIMER FINISHED!");
          eyes.timerActive = false;
          eyes.timerProgress = 0.0f;
          pomodoroEndTime = 0;
          eyes.setEmotion(HAPPY);
          hasEmotionOverride = true;
          emotionOverrideTimer = millis();
      }
  } else {
      eyes.timerActive = false;
  }

  bool cameraDetected = processCameraData();
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float totalAccel = sqrt(pow(a.acceleration.x,2) + pow(a.acceleration.y,2) + pow(a.acceleration.z,2));
  float totalGyro  = sqrt(pow(g.gyro.x,2)  + pow(g.gyro.y,2)  + pow(g.gyro.z,2));
  eyes.setEyeOffset(-a.acceleration.x * 1.5, a.acceleration.y * 1.5);

  // Tell the eye system whether the MPU is actively providing tilt data
  // so idle gaze drift doesn't fight with real physical motion
  bool mpuMoving = (abs(a.acceleration.x) > 0.8f || abs(a.acceleration.y) > 0.8f);
  eyes.setMpuActive(mpuMoving);

  bool physicallyMoved = false;
  bool strongPhysical  = false;

  if (totalAccel > 22.0) {                                        
      if (!hasEmotionOverride || eyes.getEmotion() != ANGRY) {
          eyes.setEmotion(ANGRY);
      }
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

  if (keyword_wake_word || keyword_cmd_sleep || keyword_cmd_guard || keyword_loud_noise) {
      lastInteractionTime = millis();
      
      if (keyword_loud_noise) {
          if (guardMode) {
              eyes.setEmotion(PANIC);
              emotionOverrideTimer = millis();
              hasEmotionOverride = true;
          }
      } else if (keyword_cmd_sleep) {
          if (!guardMode) { // Ignore sleep command if guarding
              // Trick the idle timer into thinking 120 seconds have passed so it plays SLEEPY naturally then falls ASLEEP 5 seconds later
              lastInteractionTime = millis() - 120001;
              hasEmotionOverride = false; 
          }
      } else if (keyword_cmd_guard) {
          eyes.setEmotion(GUARDING);
          guardMode = true;
          hasEmotionOverride = false; // Persistent state, no timeout
      } else if (keyword_wake_word) {
          guardMode = false; // Only wake_word drops guard mode
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

      keyword_wake_word = false;
      keyword_cmd_sleep = false;
      keyword_cmd_guard = false;
      keyword_loud_noise = false;
  }

  if (isTouched) {
      lastInteractionTime = millis();
      if (guardMode) {
          // Warning reaction in Guard mode
          eyes.setEmotion(WARNING_ANIM);
          emotionOverrideTimer = millis();
          hasEmotionOverride = true;
      } else if (isSleeping) {
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
      if (guardMode) {
          // Physical disturbance in Guard mode triggers Angry warning
          if (eyes.getEmotion() != ANGRY) {
              eyes.setEmotion(ANGRY);
          }
          emotionOverrideTimer = millis();
          hasEmotionOverride = true;
      } else if (!strongPhysical && (curEmotion == SLEEPY || curEmotion == ASLEEP)) {
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
          eyes.setEmotion(eyes.baseEmotion);
          innocentOverride   = false;
          hasEmotionOverride = false;
      }
  }

  // 3-second emotion override timeout (e.g., recovering from Startled/Angry)
  if (hasEmotionOverride && !isTouched && !innocentOverride && (millis() - emotionOverrideTimer > 3000)) {
      if (guardMode) {
          eyes.setEmotion(GUARDING); // Return to Guarding!
      } else {
          eyes.setEmotion(eyes.baseEmotion);
      }
      hasEmotionOverride = false;
  }

  // Idle fallback logic (only if not guarding and no overrides)
  if (!hasEmotionOverride && !isTouched && !innocentOverride && !guardMode && eyes.baseEmotion != CLOCK_MODE) {
      unsigned long idleTime = millis() - lastInteractionTime;
      if      (idleTime > 125000 && eyes.getEmotion() != ASLEEP)  eyes.setEmotion(ASLEEP); // Sleepy for 5s, then ASLEEP
      else if (idleTime > 120000 && eyes.getEmotion() != SLEEPY && eyes.getEmotion() != ASLEEP)  eyes.setEmotion(SLEEPY);
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
      else if (sp < 300) analogWrite(VIBE_PIN, (int)((300 - sp) / 150.0f * 90));
      else               analogWrite(VIBE_PIN, 0);
  } else if (curEmotion == ALARM_RINGING) {
      unsigned long t = millis() % 1000;
      if      (t < 200) analogWrite(VIBE_PIN, 255);
      else if (t < 300) analogWrite(VIBE_PIN, 0);
      else if (t < 500) analogWrite(VIBE_PIN, 255);
      else              analogWrite(VIBE_PIN, 0);
  } else {
      analogWrite(VIBE_PIN, 0);
  }

  eyes.update();
  eyes.draw(&sprite);
  
  // SOFTWARE ROTATION (180 degrees)
  sprite.setPivot(80, 64); // Center of 160x128 sprite
  sprite.pushRotateZoom(&display, 80, 64, 180, 1.0, 1.0);
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