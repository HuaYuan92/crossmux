#include "HalAudioInput.h"

#include <BoardConfig.h>

// Capability-gated like HalAudioOutput: only boards declaring a mic compile the
// capture path and pull in the Microphone lib; everyone else links the stubs.
#if FREEINK_CAP_MIC

#include <Logging.h>
#include <Microphone.h>

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
#include <HalAudioOutput.h>
#include "Waveshare397Power.h"
#endif

namespace HalAudioInput {
namespace {

constexpr unsigned long AUDIO_RAIL_SETTLE_MS = 10;

Microphone mic;
bool capturing = false;

bool setBoardPower(const bool enabled) {
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  return Waveshare397Power::setAudioPower(enabled);
#else
  (void)enabled;
  return true;
#endif
}

}  // namespace

bool begin(const uint32_t sampleRate) {
  if (capturing) return true;
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397 && CROSSPOINT_CAP_SOUND_FEEDBACK
  // Capture and playback share I2S_NUM_0 and the codec rail. The output path
  // must fully release the bus (delete the TX channel, drop the rail) before
  // the mic RX channel is built on top of it; the rail cycle also resets the
  // codec registers, so the ADC bring-up is self-contained.
  HalAudioOutput::shutdown();
#endif
  if (!setBoardPower(true)) {
    LOG_ERR("MIC", "Failed to enable board audio power");
    return false;
  }
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  delay(AUDIO_RAIL_SETTLE_MS);
#endif
  if (!mic.begin(sampleRate)) {
    LOG_ERR("MIC", "Failed to initialize microphone capture");
    setBoardPower(false);
    return false;
  }
  capturing = true;
  return true;
}

bool present() { return capturing; }

int read(int16_t* dst, const size_t maxSamples, const uint32_t timeoutMs) {
  if (!capturing) return -1;
  return mic.read(dst, maxSamples, timeoutMs);
}

void end() {
  if (!capturing) return;
  mic.end();
  if (!setBoardPower(false)) LOG_ERR("MIC", "Failed to disable board audio power");
  capturing = false;
}

}  // namespace HalAudioInput

#else  // FREEINK_CAP_MIC — no mic on this board: stub bodies, no Microphone linkage.

namespace HalAudioInput {
bool begin(uint32_t) { return false; }
bool present() { return false; }
int read(int16_t*, size_t, uint32_t) { return -1; }
void end() {}
}  // namespace HalAudioInput

#endif
