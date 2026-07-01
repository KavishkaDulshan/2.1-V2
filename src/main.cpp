#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// --- ESP-DSP AND TFLITE MICRO HEADERS ---
#include "esp_dsp.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Your custom trained model asset file
#include "model_data.h"
#include "RobotEyes.h"

// --- HARDWARE CONFIGURATION ---
#define RX_PIN 18     // Camera RX
#define TX_PIN 17     // Camera TX
#define TOUCH_PIN 14  // Capacitive Touch Sensor
#define VIBE_PIN 13   // Vibrator Motor Module

#define I2S_WS 15
#define I2S_SD 7
#define I2S_SCK 16
#define I2S_PORT I2S_NUM_0

// --- AUDIO DSP CONSTANTS ---
#define SAMPLE_RATE 16000
#define FFT_SIZE 512
#define FRAME_LEN 480     // 30ms frame window
#define HOP_LEN 160       // 10ms frame stride
#define N_MELS 40
#define TIME_FRAMES 151   // 1.5 seconds total window matrix

// Hardware objects
Adafruit_MPU6050 mpu;
RobotEyes eyes;

// Display Setup
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

// Shared Global App Flags
volatile bool keywordDetected = false;
unsigned long lastInteractionTime = 0;
unsigned long emotionOverrideTimer = 0;
bool hasEmotionOverride = false;
bool wakeupFromTouch = false;
unsigned long wakeupTouchTime = 0;
bool innocentOverride = false;
unsigned long innocentReleaseTime = 0;

TaskHandle_t audioTaskHandle;
bool processCameraData();

// --- TFLITE MICRO INFERENCE ENGINE GLOBAL FIELDS ---
namespace {
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;
  
  // 128KB memory arena to run matrix math efficiently inside internal RAM/PSRAM
  constexpr int kTensorArenaSize = 160 * 1024;
  alignas(16) uint8_t tensor_arena[kTensorArenaSize];
  
  // Real-time feature extraction rolling buffer matrix
  float spectrogram_matrix[TIME_FRAMES][N_MELS];
  int spectrogram_write_index = 0;
  
  // Mel-Filterbank weights array generated dynamically at boot
  float mel_filters[N_MELS][FFT_SIZE / 2 + 1];
  float window_coefficients[FRAME_LEN];
}

// Programmatic conversion helper to map linear frequencies to the Mel-scale
float hzToMel(float hz) {
  return 2595.0f * log10f(1.0f + hz / 700.0f);
}

float melToHz(float mel) {
  return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

// Pre-calculates triangular filter shapes at boot to save CPU cycles during runtime execution
void initMelFilterbank() {
  float min_mel = hzToMel(0.0f);
  float max_mel = hzToMel(SAMPLE_RATE / 2.0f);
  float mel_step = (max_mel - min_mel) / (N_MELS + 1);
  
  float mel_points[N_MELS + 2];
  for (int i = 0; i < N_MELS + 2; ++i) {
    mel_points[i] = min_mel + i * mel_step;
  }
  
  int bin_points[N_MELS + 2];
  for (int i = 0; i < N_MELS + 2; ++i) {
    bin_points[i] = (int)floorf((FFT_SIZE + 1) * melToHz(mel_points[i]) / SAMPLE_RATE);
  }
  
  memset(mel_filters, 0, sizeof(mel_filters));
  for (int m = 1; m <= N_MELS; ++m) {
    int start_bin = bin_points[m - 1];
    int center_bin = bin_points[m];
    int end_bin = bin_points[m + 1];
    
    for (int k = start_bin; k < center_bin; ++k) {
      if ((center_bin - start_bin) > 0)
        mel_filters[m - 1][k] = (float)(k - start_bin) / (center_bin - start_bin);
    }
    for (int k = center_bin; k < end_bin; ++k) {
      if ((end_bin - center_bin) > 0)
        mel_filters[m - 1][k] = (float)(end_bin - k) / (end_bin - center_bin);
    }
  }
  
  // Populate standard windowing coefficients to flatten raw frame transitions
  dsps_wind_hann_f32(window_coefficients, FRAME_LEN);
}

// --- THE VECTOR-ACCELERATED AI TASK CORE 0 ---
void audioInferenceTask(void *pvParameters) {
  // 1. Load your compiled INT8 flatbuffer model schema
  model = tflite::GetModel(g_model_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("❌ ERROR: TFLite Schema Mismatch!");
    vTaskDelete(NULL);
  }

  // 2. THE TRUE FIX: Exact capacity for standard and hidden quantization operations
  static tflite::MicroMutableOpResolver<10> op_resolver; 
  op_resolver.AddConv2D();
  op_resolver.AddMaxPool2D();
  op_resolver.AddReshape();
  op_resolver.AddFullyConnected();
  op_resolver.AddSoftmax();
  op_resolver.AddShape();
  op_resolver.AddStridedSlice();
  op_resolver.AddPack();
  op_resolver.AddQuantize();
  op_resolver.AddDequantize();

  // 3. Instantiate the runtime interpreter pipeline
  static tflite::MicroInterpreter static_interpreter(
    model, op_resolver, tensor_arena, kTensorArenaSize
  );
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("❌ ERROR: Failed to allocate Tensor Arena memory!");
    vTaskDelete(NULL);
  }

  input = interpreter->input(0);
  output = interpreter->output(0);

  // Initialize your local DSP coefficients
  initMelFilterbank();
  esp_err_t dsp_status = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
  if (dsp_status != ESP_OK) {
    Serial.println("❌ ERROR: Failed to initialize esp-dsp subsystem.");
  }

  // Rolling frame sample history arrays
  float audio_frame_buffer[FRAME_LEN] = {0};
  float fft_input_buffer[FFT_SIZE * 2] = {0}; // Complex interleaved layout
  int16_t mic_dma_chunk[HOP_LEN];             // 10ms raw sample slice

  Serial.println("✅ Custom TFLite Vector Engine Locked & Active on Core 0!");

  while (true) {
    size_t bytes_read = 0;
    // Read a clean 10ms slice from the physical microphone stream
    esp_err_t res = i2s_read(I2S_PORT, mic_dma_chunk, HOP_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    
    if (res == ESP_OK && bytes_read > 0) {
            static unsigned long lastLog = 0;
      int16_t max_peak = 0;
      for(int i = 0; i < HOP_LEN; i++) {
        if(abs(mic_dma_chunk[i]) > max_peak) max_peak = abs(mic_dma_chunk[i]);
      }
      if(millis() - lastLog > 1000) {
        Serial.printf("🔊 MIC PEAK VOLUME: %d\n", max_peak);
        lastLog = millis();
      }
      // Cascade the sliding window buffer: push older entries left, drop the head
      memmove(audio_frame_buffer, audio_frame_buffer + HOP_LEN, (FRAME_LEN - HOP_LEN) * sizeof(float));
      for (int i = 0; i < HOP_LEN; ++i) {
        audio_frame_buffer[FRAME_LEN - HOP_LEN + i] = (float)mic_dma_chunk[i];
      }
      
      // Execute window calculations and fill complex float spectrum structures
      memset(fft_input_buffer, 0, sizeof(fft_input_buffer));
      for (int i = 0; i < FRAME_LEN; ++i) {
        fft_input_buffer[i * 2] = audio_frame_buffer[i] * window_coefficients[i];
      }
      
      // Execute hardware-accelerated radix-2 Fast Fourier Transform
      dsps_fft2r_fc32(fft_input_buffer, FFT_SIZE);
      dsps_bit_rev_fc32(fft_input_buffer, FFT_SIZE);
      
      // Map spectral energy magnitudes down into the 40 Mel filter coefficients
      float current_mel_frame[N_MELS] = {0};
      float peak_reference = 1e-5f;
      
      for (int m = 0; m < N_MELS; ++m) {
        float energy_sum = 0;
        for (int k = 0; k <= FFT_SIZE / 2; ++k) {
          float real = fft_input_buffer[k * 2];
          float imag = fft_input_buffer[k * 2 + 1];
          float power = (real * real) + (imag * imag);
          energy_sum += power * mel_filters[m][k];
        }
        current_mel_frame[m] = energy_sum;
        if (energy_sum > peak_reference) peak_reference = energy_sum;
      }
      
      // Scale logarithmic decibel ratios directly into the active tracking slot
      for (int m = 0; m < N_MELS; ++m) {
        float log_db = 10.0f * log10f(current_mel_frame[m] / peak_reference + 1e-5f);
        spectrogram_matrix[spectrogram_write_index][m] = log_db;
      }
      
      spectrogram_write_index = (spectrogram_write_index + 1) % TIME_FRAMES;
      
      // --- PACK INFRASTRUCTURE MATRIX & EXECUTE VECTOR INFERENCE ---
      int8_t* tensor_input_ptr = input->data.int8;
      float input_scale = input->params.scale;
      int input_zero_point = input->params.zero_point;
      
      int read_cursor = spectrogram_write_index;
      for (int t = 0; t < TIME_FRAMES; ++t) {
        for (int m = 0; m < N_MELS; ++m) {
          float raw_val = spectrogram_matrix[read_cursor][m];
          // Dynamically quantize raw float features into localized model INT8 bounds
          int quantized_val = (int)roundf(raw_val / input_scale) + input_zero_point;
          if (quantized_val > 127)  quantized_val = 127;
          if (quantized_val < -128) quantized_val = -128;
          
          // Model matrix assignment mapping: [Mel Bands x Time Frames]
          tensor_input_ptr[m * TIME_FRAMES + t] = (int8_t)quantized_val;
        }
        read_cursor = (read_cursor + 1) % TIME_FRAMES;
      }
      
      // Invoke the model execution loops across the S3 arithmetic units
// Invoke the model execution
      if (interpreter->Invoke() == kTfLiteOk) {
        int8_t negative_score = output->data.int8[0];
        int8_t positive_score = output->data.int8[1];
        
        // Debug: Print scores every 500ms to avoid flooding the terminal
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 500) {
            Serial.printf("AI THOUGHTS -> Pos: %d | Neg: %d\n", positive_score, negative_score);
            lastPrint = millis();
        }
        
        // Trigger verification (Lowered threshold temporarily to 50 for testing)
        if (positive_score > negative_score && positive_score > 50) { 
          keywordDetected = true;
          Serial.printf("🎯 KEYWORD MATCH FOUND! Score: %d\n", positive_score);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n--- ROBOT BOOTING ---");
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); 

  pinMode(TOUCH_PIN, INPUT);
  pinMode(VIBE_PIN, OUTPUT);
  analogWrite(VIBE_PIN, 0);

  display.init(); 
  display.setBrightness(128); 
  display.setRotation(2);
  sprite.setColorDepth(1); 
  sprite.createSprite(128, 64);
  eyes.init();
  
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // --- ARRANGE NATIVE I2S DMA CONTROLLER CHANNELS ---
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, 
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

  // Run the vector accelerated voice inference task on Core 0
  xTaskCreatePinnedToCore(
    audioInferenceTask, "AudioAI", 16384, NULL, 5, &audioTaskHandle, 0
  );
  
  lastInteractionTime = millis();
}

void loop() {
  bool cameraDetected = processCameraData();
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float totalAccel = sqrt(pow(a.acceleration.x,2) + pow(a.acceleration.y,2) + pow(a.acceleration.z,2));
  float totalGyro  = sqrt(pow(g.gyro.x,2)  + pow(g.gyro.y,2)  + pow(g.gyro.z,2));
  eyes.setEyeOffset(-a.acceleration.x * 1.5, a.acceleration.y * 1.5);
  
  bool physicallyMoved = false;
  bool strongPhysical  = false;

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

  // --- CONNECT CUSTOM AI FLAG DIRECTLY TO EMOTION STATE CONTROLLER ---
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