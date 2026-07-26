#include "GroqClient.h"
#include <ArduinoJson.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <Preferences.h>
#include <driver/i2s.h>

extern Preferences preferences;

void GroqClient::init() {
    // No specific initialization required right now
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
#define TTS_BUFFER_MAX (1500 * 1024) // 1.5MB max WAV response in PSRAM
static uint8_t* tts_audio_buffer = nullptr;
static size_t   tts_audio_len    = 0;
static bool     tts_i2s_ready    = false;

// Event handler for binary TTS WAV response
esp_err_t _tts_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && tts_audio_buffer != nullptr) {
                if ((tts_audio_len + (size_t)evt->data_len) < TTS_BUFFER_MAX) {
                    memcpy(tts_audio_buffer + tts_audio_len, evt->data, evt->data_len);
                    tts_audio_len += evt->data_len;
                } else {
                    Serial.println("TTS: WARNING — buffer full, truncating!");
                }
            }
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
    
    esp_http_client_config_t config = {
        .url = "https://api.groq.com/openai/v1/audio/transcriptions",
        .cert_pem = NULL,
        .timeout_ms = 10000,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    
    String apiKey = preferences.getString("groq_key", "");
    String authHeader = String("Bearer ") + apiKey;
    esp_http_client_set_header(client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(client, "Content-Type", contentType.c_str());
    esp_http_client_set_post_field(client, (const char*)payload, total_len);
    
    esp_err_t err = esp_http_client_perform(client);
    int statusCode = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    heap_caps_free(payload);

    String transcribedText = "";
    if (err == ESP_OK && statusCode == 200) {
        Serial.println("Whisper Response: " + http_response_buffer);
        
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, http_response_buffer);
        if (!error) {
            transcribedText = doc["text"].as<String>();
        }
    } else {
        Serial.printf("ERR: HTTP POST failed, esp_err: %s, code: %d\n", esp_err_to_name(err), statusCode);
        if (http_response_buffer.length() > 0) Serial.println("Response: " + http_response_buffer);
    }

    return transcribedText;
}

String GroqClient::chatCompletion(const std::vector<ChatMessage>& history, const String& systemContext) {
    if (history.empty()) return "";

    Serial.println("Sending conversational history to Llama 3 (esp_http_client)...");

    DynamicJsonDocument doc(8192);
    doc["model"] = "llama-3.1-8b-instant";
    
    JsonArray messages = doc.createNestedArray("messages");
    
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

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    http_response_buffer = "";

    esp_http_client_config_t config = {
        .url = "https://api.groq.com/openai/v1/chat/completions",
        .cert_pem = NULL,
        .timeout_ms = 10000,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    
    String apiKey = preferences.getString("groq_key", "");
    String authHeader = String("Bearer ") + apiKey;
    esp_http_client_set_header(client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, jsonPayload.c_str(), jsonPayload.length());
    
    esp_err_t err = esp_http_client_perform(client);
    int statusCode = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    
    String answerText = "Error";
    if (err == ESP_OK && statusCode == 200) {
        DynamicJsonDocument resDoc(4096);
        DeserializationError error = deserializeJson(resDoc, http_response_buffer);
        if (!error) {
            answerText = resDoc["choices"][0]["message"]["content"].as<String>();
        }
    } else {
        Serial.printf("ERR: Llama 3 POST failed, esp_err: %s, code: %d\n", esp_err_to_name(err), statusCode);
        if (http_response_buffer.length() > 0) Serial.println("Response: " + http_response_buffer);
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
    if (tts_i2s_ready) return;

    i2s_config_t i2s_cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = 24000,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 1024,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
    };
    i2s_driver_install(I2S_NUM_1, &i2s_cfg, 0, NULL);

    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = 40,   // BCLK
        .ws_io_num    = 41,   // LRC
        .data_out_num = 42,   // DIN
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    i2s_set_pin(I2S_NUM_1, &pin_cfg);
    i2s_zero_dma_buffer(I2S_NUM_1);

    Serial.println("TTS: I2S_NUM_1 speaker initialized (GPIO 40/41/42).");
    tts_i2s_ready = true;
}

void GroqClient::playTTS(const String& text) {
    if (text.length() == 0) return;

    // --- Step 1: Ensure I2S speaker is initialized ---
    initSpeakerI2S();

    // --- Step 2: Get API Key ---
    String apiKey = preferences.getString("groq_key", "");
    if (apiKey.length() == 0) {
        Serial.println("TTS: No API key found in Preferences.");
        return;
    }
    Serial.println("TTS: Starting Groq Orpheus TTS for: " + text.substring(0, 40) + "...");

    // --- Step 3: Allocate PSRAM buffer ---
    tts_audio_buffer = (uint8_t*)heap_caps_malloc(TTS_BUFFER_MAX, MALLOC_CAP_SPIRAM);
    if (!tts_audio_buffer) {
        Serial.println("TTS: FATAL — cannot allocate PSRAM buffer!");
        return;
    }
    tts_audio_len = 0;

    // --- Step 4: Build JSON payload ---
    // Sanitize text: escape quotes and remove newlines
    String safeText = text;
    safeText.replace("\\", "\\\\");
    safeText.replace("\"", "\\\"");
    safeText.replace("\n", " ");
    safeText.replace("\r", "");

    String payload = "{\"model\":\"canopylabs/orpheus-v1-english\","
                     "\"input\":\"" + safeText + "\","
                     "\"voice\":\"hannah\","
                     "\"response_format\":\"wav\"}";

    // --- Step 5: HTTP POST via esp_http_client (same as Whisper/Llama) ---
    esp_http_client_config_t config = {
        .url              = "https://api.groq.com/openai/v1/audio/speech",
        .cert_pem         = NULL,
        .timeout_ms       = 30000, // Generous timeout for TTS generation
        .event_handler    = _tts_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    String authHeader = String("Bearer ") + apiKey;
    esp_http_client_set_header(client, "Authorization", authHeader.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload.c_str(), payload.length());

    esp_err_t err = esp_http_client_perform(client);
    int statusCode = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    Serial.printf("TTS: HTTP %d | WAV bytes received: %u\n", statusCode, tts_audio_len);

    // --- Step 6: Play raw PCM from WAV buffer ---
    if (err == ESP_OK && statusCode == 200 && tts_audio_len > 44) {
        // Parse WAV: find the 'data' chunk to get exact PCM start offset
        size_t pcm_start = 44; // Standard RIFF WAV header is 44 bytes
        // Search for 'data' marker in case of non-standard header
        for (size_t i = 0; i < tts_audio_len - 8; i++) {
            if (tts_audio_buffer[i]   == 'd' &&
                tts_audio_buffer[i+1] == 'a' &&
                tts_audio_buffer[i+2] == 't' &&
                tts_audio_buffer[i+3] == 'a') {
                pcm_start = i + 8; // skip 'd','a','t','a' + 4-byte chunk size
                break;
            }
        }

        Serial.printf("TTS: PCM data starts at byte %u. Playing...\n", pcm_start);

        uint8_t* pcm_ptr = tts_audio_buffer + pcm_start;
        size_t   pcm_len = tts_audio_len - pcm_start;

        // Stream in 2048-byte chunks to avoid I2S write latency spikes
        const size_t CHUNK = 2048;
        size_t offset = 0;
        while (offset < pcm_len) {
            size_t write_len = min(CHUNK, pcm_len - offset);
            size_t bytes_written = 0;
            i2s_write(I2S_NUM_1, pcm_ptr + offset, write_len, &bytes_written, portMAX_DELAY);
            offset += write_len;
        }

        // Drain DMA buffers cleanly
        i2s_zero_dma_buffer(I2S_NUM_1);
        Serial.println("TTS: Playback complete.");
    } else {
        Serial.printf("TTS: ERROR — esp_err=%s, HTTP=%d, bytes=%u\n",
                      esp_err_to_name(err), statusCode, tts_audio_len);
        // Print response body if it's small enough (likely an error JSON)
        if (tts_audio_len > 0 && tts_audio_len < 512) {
            Serial.write(tts_audio_buffer, tts_audio_len);
            Serial.println();
        }
    }

    // --- Step 7: Free PSRAM buffer ---
    heap_caps_free(tts_audio_buffer);
    tts_audio_buffer = nullptr;
    tts_audio_len    = 0;
}
