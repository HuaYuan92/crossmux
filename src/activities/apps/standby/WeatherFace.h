#pragma once

#include <cstdint>

#include "StandbyFace.h"
#include "util/WeatherService.h"

// Standby "weather" face (Waveshare 3.97 only). Read-only: it renders the SD
// cache written by the standalone Weather app and never touches the network or
// WiFi, so it is cheap enough to sit in the standby rotation. Portrait-only, to
// match the layout designed for the 480x800 logical screen. Registration is
// gated by FREEINK_DEVICE_WAVESHARE_EPAPER_397 in StandbyActivity.cpp, keeping
// the shared X3/X4 image unchanged.
class WeatherFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  TickResult tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override { return StrId::STR_FACE_WEATHER; }
  uint32_t secondsUntilNextWake() const override;

 private:
  WeatherData data_;
  bool loaded_ = false;
};
