#include "GroqClient.h"
#include <ArduinoJson.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <Preferences.h>

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

// Global buffer to hold HTTP response
static String http_response_buffer = "";

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

String GroqClient::chatCompletion(const String& prompt) {
    if (prompt.length() == 0) return "";

    Serial.println("Sending to Llama 3 (esp_http_client)...");

    DynamicJsonDocument doc(1024);
    doc["model"] = "llama-3.1-8b-instant";
    
    JsonArray messages = doc.createNestedArray("messages");
    
    JsonObject sysMsg = messages.createNestedObject();
    sysMsg["role"] = "system";
    sysMsg["content"] = "You are an intelligent, friendly robot companion named Two Point One. Keep your answers extremely short, compact, and under 15 words, as they will be displayed on a tiny 160x128 pixel screen.";
    
    JsonObject userMsg = messages.createNestedObject();
    userMsg["role"] = "user";
    userMsg["content"] = prompt;

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
        DynamicJsonDocument resDoc(2048);
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
