# Voice Activity Detection (VAD) Integration

This document outlines the architecture and implementation of the Voice Activity Detection (VAD) system used in the 2.1 Robot's audio pipeline.

## Overview
Keyword Spotting (KWS) models on microcontrollers are prone to false positives when exposed to continuous background noise (fans, air conditioners, keyboard typing). To solve this, we implemented a **Two-Stage Audio Pipeline**:

1. **Stage 1 (VAD Gatekeeper):** A highly optimized WebRTC Voice Activity Detector (`libfvad`) mathematically analyzes incoming audio for the acoustic signatures of a human vocal tract.
2. **Stage 2 (Edge Impulse AI):** If (and only if) the VAD detects human speech, the full audio buffer is passed to the Edge Impulse neural network to check for specific keywords.

By utilizing this pipeline, we save massive amounts of CPU cycles and virtually eliminate false wake-ups caused by mechanical room noise.

---

## The Library: `libfvad`
We are using `libfvad`, an open-source, standalone C port of the VAD engine originally developed by Google for the WebRTC project (used in Chrome, Google Meet, etc.).

### Why `libfvad`?
- **Highly Accurate:** Uses a Gaussian Mixture Model (GMM) optimized specifically for human formants.
- **Lightweight:** Pure fixed-point C math. Requires no dynamic memory allocation after initialization.
- **Integration:** The source files have been completely flattened into the `lib/webrtc_vad` directory in this project to ensure seamless compilation with PlatformIO's Library Dependency Finder (LDF) without complex CMake configurations.

---

## Implementation Details

### 1. Initialization (`main.cpp`)
The VAD engine is instantiated during `setup()`:
```cpp
Fvad *vad_inst = fvad_new();
fvad_set_sample_rate(vad_inst, 16000); // Must match Edge Impulse sample rate
fvad_set_mode(vad_inst, 2);            // Mode 2: Aggressive filtering
```
*Note: `fvad_set_mode` accepts values from 0 (High Quality / Loose) to 3 (Very Aggressive). Mode 2 is the recommended balance for smart speakers.*

### 2. Audio Slicing & Processing (`audioInferenceTask`)
Edge Impulse operates on a 1-second rolling ring buffer (`16000` samples at 16kHz). However, WebRTC VAD requires exact frame sizes of `10ms`, `20ms`, or `30ms`.

To bridge this gap, the `audioInferenceTask` slices the 1-second buffer into **thirty-three 30-millisecond frames** (480 samples each):

```cpp
int frame_length = 480; // 30ms at 16kHz
int num_frames = 16000 / 480; // 33 frames
int speech_frames = 0;

int16_t vad_chunk[480]; 

for (int i = 0; i < num_frames; i++) {
    // Extract 480 samples from the circular buffer
    int start_idx = i * frame_length;
    for (int j = 0; j < frame_length; j++) {
        vad_chunk[j] = audio_buffer[(ring_position + start_idx + j) % 16000];
    }
    
    // Process chunk through VAD
    if (fvad_process(vad_inst, vad_chunk, frame_length) == 1) {
        speech_frames++;
    }
}
```
*Memory Optimization Note: By extracting the audio into a tiny `960-byte` stack array (`vad_chunk`) iteratively, we avoided the FreeRTOS stack-overflow crashes that occur when allocating large 32KB contiguous buffers on the task stack.*

### 3. The Threshold Gate
Once the 33 frames are processed, we evaluate the `speech_frames` count.
```cpp
if (speech_frames < 3) {
    // ❌ VAD: No speech detected. Skipping AI.
    continue; 
}
```
If fewer than **3 frames** (90 milliseconds total) contain human speech, the chunk is classified as noise, and the task immediately skips the Edge Impulse `run_classifier()` block.

---

## Understanding Real-World Behavior

- **Continuous Noise (Fans/AC/Typing):** The VAD will consistently return `0/33` speech frames. The AI will never run.
- **Sudden Sharp Noises (Desk Taps/Claps):** Sharp acoustic shockwaves share a similar Zero-Crossing Rate (ZCR) with human plosive consonants (like 'P' or 'T'). The VAD may flag 2 to 4 frames of a desk tap as speech. In this scenario, the audio *is* passed to Edge Impulse. Edge Impulse will then correctly classify the sound as `noise` and reject it. **This is expected behavior and demonstrates the two-stage pipeline working correctly.**
- **Human Voice:** The VAD will easily flag 15+ frames as speech, passing the buffer to Edge Impulse, which then triggers the corresponding action.
