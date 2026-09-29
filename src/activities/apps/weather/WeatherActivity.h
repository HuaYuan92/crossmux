#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/WeatherService.h"

// Standalone weather app for the Waveshare 3.97 build. It owns the network
// side of weather: connect to the last saved Wi-Fi, refresh via WeatherService
// (IP geolocation + Open-Meteo), and cache to SD. The standby weather face only
// reads that cache, so this activity is the single place that talks to the
// network. Entirely device-gated: the class is only referenced from the
// Waveshare build environment.
class WeatherActivity final : public Activity {
 public:
  WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    Show,       // render cached data (or the empty prompt)
    Connecting, // Wi-Fi association in progress, polled each loop()
    Fetching,   // connected; blocking HTTP refresh runs once, then Show/Error
    Error,      // last attempt failed; offer retry
  };

  void startRefresh();
  bool bringUpSavedWifi();
  void teardownWifi();
  void runFetch();

  void drawMessage(const char* text) const;
  void drawWeather();

  State state_ = State::Show;
  WeatherData data_;
  bool ownsWifi_ = false;
  bool errorNoWifi_ = false;
  bool errorClock_ = false;  // HTTPS needs a trustworthy clock; SNTP failed
  uint32_t wifiStartedMs_ = 0;
  bool ignoreConfirmRelease_ = false;
  bool ignoreBackRelease_ = false;
};
