#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/HolidayService.h"

// Standalone month-calendar app for the Waveshare 3.97 build. Renders a
// Monday-first 6x7 month grid with lunar / solar-term / festival sub-labels
// (from MonthGrid) and statutory 休/班 badges (from HolidayService's per-year SD
// cache). It owns the network side of holidays: connect to the last saved
// Wi-Fi, refresh the displayed year from holiday-cn, and cache to SD. The
// standby calendar face (added later) only reads that cache. Entirely
// device-gated: the class is only referenced from the Waveshare build.
class CalendarActivity final : public Activity {
 public:
  CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Calendar", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t {
    Show,        // render the month grid (badges from cache, if any)
    Connecting,  // Wi-Fi association in progress, polled each loop()
    Fetching,    // connected; blocking HTTP holiday refresh runs once
    Error,       // last attempt failed; offer retry
  };

  void startRefresh();
  bool bringUpSavedWifi();
  void teardownWifi();
  void runFetch();

  void deriveToday();                 // read the clock; set haveToday_ + view
  void loadHolidaysForYear(int year); // offline cache read into holiday_
  void changeMonth(int delta);
  void changeYear(int delta);

  void drawMessage(const char* text) const;
  void drawGrid();

  State state_ = State::Show;
  int viewYear_ = 2026;
  int viewMonth_ = 1;  // 1..12
  int todayY_ = 0;
  int todayM_ = 0;
  int todayD_ = 0;
  bool haveToday_ = false;
  HolidayService::YearData holiday_;

  bool ownsWifi_ = false;
  bool errorNoWifi_ = false;
  bool errorClock_ = false;
  uint32_t wifiStartedMs_ = 0;
  bool ignoreConfirmRelease_ = false;
  bool ignoreBackRelease_ = false;
};
