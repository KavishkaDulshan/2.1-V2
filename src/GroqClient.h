#ifndef GROQ_CLIENT_H
#define GROQ_CLIENT_H

#include <Arduino.h>

class GroqClient {
public:
    static void init();
    
    // Transcribe a 16kHz 16-bit Mono PCM buffer to text
    static String transcribeAudio(int16_t* pcm_data, size_t num_samples);
    
    // Send text to Llama 3 and get a short response
    static String chatCompletion(const String& prompt);

private:
    static void generateWavHeader(uint8_t* header, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize);
};

#endif
