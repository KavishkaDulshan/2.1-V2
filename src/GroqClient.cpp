#include "GroqClient.h"
#include <ArduinoJson.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <Preferences.h>
#include <driver/i2s.h>
#include "ChatUI.h"
#include "RobotEyes.h"

extern Preferences preferences;
extern ChatUI chatUI;
extern RobotEyes eyes;

volatile bool GroqClient::is_tts_playing = false;

enum ApiPhase {
    PHASE_NONE,
    PHASE_STT,
    PHASE_LLM,
    PHASE_TTS
};
static ApiPhase current_api_phase = PHASE_NONE;
static esp_http_client_handle_t persistent_client = NULL;

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
esp_err_t _llm_stream_event_handler(esp_http_client_event_t *evt);
esp_err_t _tts_event_handler(esp_http_client_event_t *evt);

esp_err_t _unified_event_handler(esp_http_client_event_t *evt) {
    if (current_api_phase == PHASE_STT) return _http_event_handler(evt);
    if (current_api_phase == PHASE_LLM) return _llm_stream_event_handler(evt);
    if (current_api_phase == PHASE_TTS) return _tts_event_handler(evt);
    return ESP_OK;
}

void GroqClient::init() {
    if (persistent_client == NULL) {
        esp_http_client_config_t config = {
            .url = "https://api.groq.com",
            .cert_pem = NULL,
            .timeout_ms = 30000,
            .event_handler = _unified_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = true
        };
        persistent_client = esp_http_client_init(&config);
    }
}

void GroqClient::generateWavHeader(uint8_t* header, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize) {
    uint32_t fileSize = dataSize + 36;
    uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign = channels * (bitsPerSample / 8);

    // RIFF chunk descriptor
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    header[4] = (uint8_t)(fileSize & 0xFF);
    header[5] = (uint8_t)((fileSize >> 8) & 0xFF);
    header[6] = (uint8_t)((fileSize >> 16) & 0xFF);
    header[7] = (uint8_t)((fileSize >> 24) & 0xFF);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

    // fmt sub-chunk
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0; // Subchunk1Size is 16 for PCM
    header[20] = 1; header[21] = 0; // AudioFormat (1 = PCM)
    header[22] = (uint8_t)channels; header[23] = 0;
    
    header[24] = (uint8_t)(sampleRate & 0xFF);
    header[25] = (uint8_t)((sampleRate >> 8) & 0xFF);
    header[26] = (uint8_t)((sampleRate >> 16) & 0xFF);
    header[27] = (uint8_t)((sampleRate >> 24) & 0xFF);

    header[28] = (uint8_t)(byteRate & 0xFF);
    header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
    header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
    header[31] = (uint8_t)((byteRate >> 24) & 0xFF);

    header[32] = (uint8_t)blockAlign; header[33] = 0;
    header[34] = (uint8_t)bitsPerSample; header[35] = 0;

    // data sub-chunk
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    header[40] = (uint8_t)(dataSize & 0xFF);
    header[41] = (uint8_t)((dataSize >> 8) & 0xFF);
    header[42] = (uint8_t)((dataSize >> 16) & 0xFF);
    header[43] = (uint8_t)((dataSize >> 24) & 0xFF);
}

// Global buffer to hold HTTP response (JSON text)
static String http_response_buffer = "";

// --- TTS Binary Audio Buffer ---
#define TTS_STREAM_BUF_SIZE (256 * 1024) // 256KB streaming buffer in PSRAM
static uint8_t* tts_stream_buf = nullptr;
static volatile size_t tts_stream_write_ptr = 0;
static volatile size_t tts_stream_read_ptr = 0;
static volatile size_t tts_stream_count = 0;
static SemaphoreHandle_t tts_stream_mutex = NULL;
static volatile bool tts_is_downloading = false;
static volatile size_t tts_bytes_received = 0;

static bool tts_i2s_ready = false;

// Event handler for binary TTS WAV response
esp_err_t _tts_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && tts_stream_buf != nullptr) {
                uint8_t* data = (uint8_t*)evt->data;
                size_t len = evt->data_len;
                
                // Skip standard WAV header (44 bytes)
                if (tts_bytes_received < 44) {
                    size_t header_bytes = 44 - tts_bytes_received;
                    if (len <= header_bytes) {
                        tts_bytes_received += len;
                        break; 
                    } else {
                        data += header_bytes;
                        len -= header_bytes;
                        tts_bytes_received += header_bytes;
                    }
                }
                
                tts_bytes_received += len;

                if (tts_stream_mutex) {
                    while (len > 0) {
                        xSemaphoreTake(tts_stream_mutex, portMAX_DELAY);
                        size_t available_space = TTS_STREAM_BUF_SIZE - tts_stream_count;
                        if (available_space > 0) {
                            size_t write_len = min(len, available_space);
                            for (size_t i = 0; i < write_len; i++) {
                                tts_stream_buf[tts_stream_write_ptr] = data[i];
                                tts_stream_write_ptr = (tts_stream_write_ptr + 1) % TTS_STREAM_BUF_SIZE;
                            }
                            tts_stream_count += write_len;
                            len -= write_len;
                            data += write_len;
                        }
                        xSemaphoreGive(tts_stream_mutex);
                        
                        if (len > 0) {
                            vTaskDelay(pdMS_TO_TICKS(5)); // Backpressure: wait for playback task to consume
                        }
                    }
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
        case HTTP_EVENT_DISCONNECTED:
            tts_is_downloading = false;
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                // Ensure null termination safely or just append bytes
                char* temp = (char*)malloc(evt->data_len + 1);
                if (temp) {
                    memcpy(temp, evt->data, evt->data_len);
                    temp[evt->data_len] = '\0';
                    http_response_buffer += temp;
                    free(temp);
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static String llm_complete_response = "";
static String sse_buffer = "";
static bool is_parsing_tag = false;
static String tag_buffer = "";

esp_err_t _llm_stream_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                char* temp = (char*)malloc(evt->data_len + 1);
                if (temp) {
                    memcpy(temp, evt->data, evt->data_len);
                    temp[evt->data_len] = '\0';
                    sse_buffer += temp;
                    free(temp);
                }
                
                int newline_idx;
                while ((newline_idx = sse_buffer.indexOf('\n')) >= 0) {
                    String line = sse_buffer.substring(0, newline_idx);
                    sse_buffer = sse_buffer.substring(newline_idx + 1);
                    
                    line.trim();
                    if (line.startsWith("data: ")) {
                        String jsonStr = line.substring(6);
                        if (jsonStr == "[DONE]") continue;
                        
                        DynamicJsonDocument doc(1024);
                        DeserializationError err = deserializeJson(doc, jsonStr);
                        if (!err) {
                            if (doc["choices"][0]["delta"].containsKey("content")) {
                                String content = doc["choices"][0]["delta"]["content"].as<String>();
                                
                                if (is_parsing_tag) {
                                    for (size_t c = 0; c < content.length(); c++) {
                                        if (tag_buffer.length() == 0 && content[c] != '[') {
                                            if (isspace(content[c]) || content[c] == '\n' || content[c] == '\r') {
                                                continue; // skip leading whitespace
                                            }
                                            // Doesn't start with [, abort parsing and dump
                                            Serial.printf("\n[DBG] Tag Parser Aborted! Found char: '%c' (0x%02X)\n", content[c], content[c]);
                                            is_parsing_tag = false;
                                            chatUI.appendLastMessage(content.substring(c));
                                            llm_complete_response += content.substring(c);
                                            break;
                                        }
                                        tag_buffer += content[c];
                                        if (content[c] == ']') {
                                            is_parsing_tag = false;
                                            // Parse emotion
                                            String emStr = tag_buffer.substring(1, tag_buffer.length() - 1);
                                            emStr.toUpperCase();
                                            if (emStr == "HAPPY") eyes.setEmotion(HAPPY);
                                            else if (emStr == "ANGRY") eyes.setEmotion(ANGRY);
                                            else if (emStr == "SAD") eyes.setEmotion(SAD);
                                            else if (emStr == "SLEEPY") eyes.setEmotion(SLEEPY);
                                            else if (emStr == "INNOCENT") eyes.setEmotion(INNOCENT);
                                            else if (emStr == "DIZZY") eyes.setEmotion(DIZZY);
                                            else if (emStr == "PANIC") eyes.setEmotion(PANIC);
                                            else if (emStr == "LOVE") eyes.setEmotion(LOVE);
                                            else if (emStr == "EXCITED") eyes.setEmotion(EXCITED);
                                            else if (emStr == "CONFUSED") eyes.setEmotion(CONFUSED);
                                            else if (emStr == "NEUTRAL") eyes.setEmotion(NEUTRAL);
                                            
                                            // Stream the rest
                                            if (c + 1 < content.length()) {
                                                String remainder = content.substring(c + 1);
                                                if (remainder.startsWith(" ")) remainder = remainder.substring(1);
                                                chatUI.appendLastMessage(remainder);
                                                llm_complete_response += remainder;
                                            }
                                            break;
                                        }
                                    }
                                } else {
                                    chatUI.appendLastMessage(content);
                                    llm_complete_response += content;
                                }
                            }
                        }
                    }
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

String GroqClient::transcribeAudio(int16_t* pcm_data, size_t num_samples) {
    if (num_samples == 0) return "";

    Serial.println("Setting up secure connection for Groq Whisper (esp_http_client)...");
    
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String contentType = "multipart/form-data; boundary=" + boundary;

    uint32_t pcm_bytes = num_samples * sizeof(int16_t);
    uint8_t wav_header[44];
    generateWavHeader(wav_header, 16000, 16, 1, pcm_bytes);

    String head = "--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                  "Content-Type: audio/wav\r\n\r\n";
    
    String tail = "\r\n--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                  "whisper-large-v3\r\n"
                  "--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
                  "en\r\n"
                  "--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
                  "json\r\n"
                  "--" + boundary + "--\r\n";

    uint32_t total_len = head.length() + sizeof(wav_header) + pcm_bytes + tail.length();
    Serial.printf("Uploading Audio... Total Payload Size: %d bytes\n", total_len);
    
    uint8_t* payload = (uint8_t*)heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM);
    if (!payload) {
        Serial.println("ERR: Not enough PSRAM for payload buffer!");
        return "";
    }

    size_t offset = 0;
    memcpy(payload + offset, head.c_str(), head.length());
    offset += head.length();
    
    memcpy(payload + offset, wav_header, 44);
    offset += 44;
    
    memcpy(payload + offset, pcm_data, pcm_bytes);
    offset += pcm_bytes;
    
    memcpy(payload + offset, tail.c_str(), tail.length());
    offset += tail.length();

    http_response_buffer = "";
    current_api_phase = PHASE_STT;
    
    if (persistent_client == NULL) GroqClient::init();
    
    esp_http_client_set_url(persistent_client, "https://api.groq.com/openai/v1/audio/transcriptions");
    esp_http_client_set_method(persistent_client, HTTP_METHOD_POST);
    
    String apiKey = preferences.getString("groq_key", "");
    String authHeader = String("Bearer ") + apiKey;
    esp_http_client_set_header(persistent_client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(persistent_client, "Content-Type", contentType.c_str());
    esp_http_client_set_post_field(persistent_client, (const char*)payload, total_len);
    
    esp_err_t err = esp_http_client_perform(persistent_client);
    int statusCode = esp_http_client_get_status_code(persistent_client);
    
    heap_caps_free(payload);

    Serial.printf("[STT] HTTP status: %d\n", statusCode);

    String transcribedText = "";
    if (err == ESP_OK && statusCode == 200) {
        // Parse the compact JSON response on the HEAP to avoid stack overflow
        DynamicJsonDocument* doc = new DynamicJsonDocument(2048);
        if (!doc) {
            Serial.println("[STT] ERR: heap alloc for JSON doc failed!");
            return "";
        }
        DeserializationError error = deserializeJson(*doc, http_response_buffer);
        if (!error) {
            // With format=json, Whisper returns {"text": "..."} only
            transcribedText = (*doc)["text"].as<String>();
            transcribedText.trim();
            Serial.println("[STT] Transcription: '" + transcribedText + "'");
        } else {
            Serial.println("[STT] JSON parse error: " + String(error.c_str()));
            Serial.println("[STT] Raw (first 200): " + http_response_buffer.substring(0, 200));
        }
        delete doc;
    } else if (statusCode == 429) {
        Serial.println("\xF0\x9F\x9A\xAB [STT] RATE LIMIT HIT (HTTP 429)!");
        transcribedText = "[RATE_LIMITED]";
    } else {
        Serial.printf("\xE2\x9D\x8C [STT] Request failed. esp_err=%s, HTTP=%d\n", esp_err_to_name(err), statusCode);
        if (http_response_buffer.length() > 0) {
            Serial.println("[STT] Error (first 200): " + http_response_buffer.substring(0, 200));
        }
    }

    return transcribedText;
}

String GroqClient::chatCompletion(const std::vector<ChatMessage>& history, const String& systemContext) {
    if (history.empty()) return "";

    Serial.println("Sending conversational history to Llama 3 (esp_http_client)...");
    // Build JSON payload on the HEAP to avoid stack overflow from 8KB DynamicJsonDocument
    DynamicJsonDocument* doc = new DynamicJsonDocument(8192);
    if (!doc) {
        Serial.println("[LLM] ERR: heap alloc for JSON doc failed!");
        return "[API_ERROR]";
    }
    (*doc)["model"] = "llama-3.1-8b-instant";
    
    JsonArray messages = doc->createNestedArray("messages");
    
    JsonObject sysMsg = messages.createNestedObject();
    sysMsg["role"] = "system";
    sysMsg["content"] = systemContext;
    
    // Pass up to the last 10 turns of conversation history
    int maxHistory = 10;
    int startIdx = (history.size() > (size_t)maxHistory) ? (int)history.size() - maxHistory : 0;
    for (size_t i = startIdx; i < history.size(); i++) {
        JsonObject msg = messages.createNestedObject();
        msg["role"] = history[i].isUser ? "user" : "assistant";
        msg["content"] = history[i].text;
    }

    (*doc)["stream"] = true;

    String jsonPayload;
    serializeJson(*doc, jsonPayload);
    delete doc; // Free heap immediately after serialization

    llm_complete_response = "";
    sse_buffer = "";
    is_parsing_tag = true;
    tag_buffer = "";
    current_api_phase = PHASE_LLM;
    
    if (persistent_client == NULL) GroqClient::init();
    
    // Seed the UI with an empty robot message that we will append to
    chatUI.addMessage("", false);

    esp_http_client_set_url(persistent_client, "https://api.groq.com/openai/v1/chat/completions");
    esp_http_client_set_method(persistent_client, HTTP_METHOD_POST);
    
    String apiKey = preferences.getString("groq_key", "");
    String authHeader = String("Bearer ") + apiKey;
    esp_http_client_set_header(persistent_client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(persistent_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(persistent_client, jsonPayload.c_str(), jsonPayload.length());
    
    esp_err_t err = esp_http_client_perform(persistent_client);
    int statusCode = esp_http_client_get_status_code(persistent_client);
    
    Serial.printf("[LLM] HTTP status: %d\n", statusCode);

    String answerText = "";
    if (err == ESP_OK && statusCode == 200) {
        answerText = llm_complete_response;
    } else if (statusCode == 429) {
        Serial.println("\xF0\x9F\x9A\xAB [LLM] RATE LIMIT HIT (HTTP 429)!");
        answerText = "[RATE_LIMITED]";
    } else {
        Serial.printf("\xE2\x9D\x8C [LLM] Request failed. esp_err=%s, HTTP=%d\n", esp_err_to_name(err), statusCode);
        if (http_response_buffer.length() > 0) {
            Serial.println("[LLM] Error (first 200): " + http_response_buffer.substring(0, 200));
        }
        answerText = "[API_ERROR]";
    }
    
    // Clean up quotes and newlines for the display
    answerText.trim();
    answerText.replace("\"", "");
    
    return answerText;
}

// =========================================================================
// --- TEXT-TO-SPEECH (Groq Orpheus via I2S_NUM_1 / MAX98357) ---
// =========================================================================

void GroqClient::initSpeakerI2S() {
    static bool speaker_initialized = false;
    if (speaker_initialized) return;

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 24000,
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
    
    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = 40,
        .ws_io_num = 41,
        .data_out_num = 42,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);
    speaker_initialized = true;
    tts_i2s_ready = true;
}

void GroqClient::playTestBeep() {
    if (is_tts_playing) return;
    is_tts_playing = true;
    initSpeakerI2S();

    uint8_t volPercent = preferences.getUInt("vol", 100);
    float volScalar = (volPercent / 100.0f) * 4.0f;

    int sample_rate = 24000;
    int duration_ms = 30;
    int num_samples = (sample_rate * duration_ms) / 1000;
    
    int16_t* sine_buf = (int16_t*)malloc(num_samples * sizeof(int16_t));
    if (sine_buf) {
        for (int i = 0; i < num_samples; i++) {
            float t = (float)i / sample_rate;
            int16_t val = (int16_t)(sin(2 * PI * 1000 * t) * 8000); 
            
            int32_t scaled = (int32_t)(val * volScalar);
            if (scaled > 32767) scaled = 32767;
            else if (scaled < -32768) scaled = -32768;
            
            sine_buf[i] = (int16_t)scaled;
        }
        
        size_t bytes_written = 0;
        i2s_write(I2S_NUM_1, (uint8_t*)sine_buf, num_samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        free(sine_buf);
    }
    is_tts_playing = false;
}

void ttsPlaybackTask(void *pvParameters) {
    uint8_t volPercent = preferences.getUInt("vol", 100);
    float volScalar = (volPercent / 100.0f) * 4.0f;
    
    int16_t local_pcm[1024]; // 2048 bytes
    size_t chunk_size_bytes = 2048;

    while (tts_is_downloading || tts_stream_count > 0) {
        bool has_data = false;
        size_t bytes_to_read = 0;
        
        if (tts_stream_mutex) {
            xSemaphoreTake(tts_stream_mutex, portMAX_DELAY);
            if (tts_stream_count >= chunk_size_bytes || (!tts_is_downloading && tts_stream_count > 0)) {
                bytes_to_read = min(chunk_size_bytes, (size_t)tts_stream_count);
                bytes_to_read = bytes_to_read & ~1; // Ensure even number of bytes (complete 16-bit samples)
                
                if (bytes_to_read > 0) {
                    uint8_t* dest = (uint8_t*)local_pcm;
                    for(size_t i = 0; i < bytes_to_read; i++) {
                        dest[i] = tts_stream_buf[tts_stream_read_ptr];
                        tts_stream_read_ptr = (tts_stream_read_ptr + 1) % TTS_STREAM_BUF_SIZE;
                    }
                    tts_stream_count -= bytes_to_read;
                    has_data = true;
                }
            }
            xSemaphoreGive(tts_stream_mutex);
        }
        
        if (has_data) {
            size_t num_samples = bytes_to_read / 2;
            for (size_t i = 0; i < num_samples; i++) {
                int32_t val = (int32_t)(local_pcm[i] * volScalar);
                if (val > 32767) val = 32767;
                else if (val < -32768) val = -32768;
                local_pcm[i] = (int16_t)val;
            }
            
            size_t bytes_written = 0;
            i2s_write(I2S_NUM_1, (uint8_t*)local_pcm, bytes_to_read, &bytes_written, portMAX_DELAY);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    i2s_zero_dma_buffer(I2S_NUM_1);
    GroqClient::is_tts_playing = false;
    vTaskDelete(NULL);
}

void GroqClient::playTTS(const String& text) {
    if (text.length() == 0) return;

    initSpeakerI2S();

    String apiKey = preferences.getString("groq_key", "");
    if (apiKey.length() == 0) {
        Serial.println("TTS: No API key found in Preferences.");
        return;
    }
    Serial.println("TTS: Starting Groq Orpheus TTS for: " + text.substring(0, 40) + "...");

    if (!tts_stream_mutex) {
        tts_stream_mutex = xSemaphoreCreateMutex();
    }

    tts_stream_buf = (uint8_t*)heap_caps_malloc(TTS_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!tts_stream_buf) {
        Serial.println("TTS: FATAL — cannot allocate PSRAM stream buffer!");
        return;
    }
    
    tts_stream_write_ptr = 0;
    tts_stream_read_ptr = 0;
    tts_stream_count = 0;
    tts_bytes_received = 0;
    tts_is_downloading = true;
    is_tts_playing = true;

    // Start background playback task!
    xTaskCreatePinnedToCore(ttsPlaybackTask, "TTS_Play", 4096, NULL, 5, NULL, 1);

    String safeText = text;
    safeText.replace("\\", "\\\\");
    safeText.replace("\"", "\\\"");
    safeText.replace("\n", " ");
    safeText.replace("\r", "");

    String payload = "{\"model\":\"canopylabs/orpheus-v1-english\","
                     "\"input\":\"" + safeText + "\","
                     "\"voice\":\"hannah\","
                     "\"response_format\":\"wav\"}";

    current_api_phase = PHASE_TTS;
    if (persistent_client == NULL) GroqClient::init();

    esp_http_client_set_url(persistent_client, "https://api.groq.com/openai/v1/audio/speech");
    esp_http_client_set_method(persistent_client, HTTP_METHOD_POST);

    String authHeader = String("Bearer ") + apiKey;
    esp_http_client_set_header(persistent_client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(persistent_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(persistent_client, payload.c_str(), payload.length());

    esp_err_t err = esp_http_client_perform(persistent_client);
    int statusCode = esp_http_client_get_status_code(persistent_client);

    Serial.printf("[TTS] HTTP status: %d\n", statusCode);

    tts_is_downloading = false; // Signal playback task to drain and exit

    if (err != ESP_OK || statusCode != 200) {
        // Flush any partial garbage data so the playback task exits immediately.
        // Do NOT force-clear is_tts_playing here, otherwise we free the buffer
        // while the background task is still running!
        if (tts_stream_mutex) {
            xSemaphoreTake(tts_stream_mutex, portMAX_DELAY);
            tts_stream_count = 0;
            xSemaphoreGive(tts_stream_mutex);
        }
        
        if (statusCode == 429) {
            Serial.println("🚫 [TTS] RATE LIMIT HIT (HTTP 429)!");
        } else {
            Serial.printf("❌ [TTS] Request failed. esp_err=%s, HTTP=%d\n", esp_err_to_name(err), statusCode);
        }
    }

    // Block here until the playback task finishes playing the remainder of the buffer
    unsigned long tts_timeout = millis();
    while (is_tts_playing) {
        if (millis() - tts_timeout > 30000) {
            Serial.println("❌ [TTS] Playback timeout! Force-releasing.");
            is_tts_playing = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    heap_caps_free(tts_stream_buf);
    tts_stream_buf = nullptr;
    Serial.println("[TTS] Finished.");
}
