#pragma once

#include <cstddef>
#include <cstdint>

// Input counterpart to HalAudioOutput. The 3.97 mic is an analog capsule into
// the ES8311 codec's built-in ADC, sharing the playback I2S bus; capture and
// playback are half-duplex and never run at once, so begin() releases the
// output path first. Boards without a mic (FREEINK_CAP_MIC off) link the stub
// bodies and begin() returns false.
namespace HalAudioInput {

// Powers the codec rail and starts mic capture at sampleRate (16 kHz on the
// 3.97). Returns false if the board has no mic or bring-up fails.
bool begin(uint32_t sampleRate = 16000);

// True once begin() succeeded and capture is live.
bool present();

// Read up to maxSamples 16-bit mono samples. Returns samples read
// (0 = timeout/no data, <0 = error/not begun).
int read(int16_t* dst, size_t maxSamples, uint32_t timeoutMs = 100);

// Stops capture and powers the rail back down.
void end();

}  // namespace HalAudioInput
