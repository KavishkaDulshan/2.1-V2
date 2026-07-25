#ifndef GROQ_CLIENT_H
#define GROQ_CLIENT_H

#include <Arduino.h>
#include "ChatUI.h"

class GroqClient {
public:
    static void init();
    
    // Transcribe a 16kHz 16-bit Mono PCM buffer to text
    static String transcribeAudio(int16_t* pcm_data, size_t num_samples);
    
    // Send conversation history and live system context to Llama 3 and get a conversational response
    static String chatCompletion(const std::vector<ChatMessage>& history, const String& systemContext);

private:
    static void generateWavHeader(uint8_t* header, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize);
};

#endif
