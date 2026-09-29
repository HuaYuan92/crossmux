#pragma once

#include <SloppyDigits.h>

#include <cstdint>
#include <memory>

#include "ChineseAlmanac.h"
#include "StandbyFace.h"

// "Chinese Traditional Calendar" — paper-黄历 standby face.
//
// Portrait-only (StandbyActivity hides it in landscape) and CN-build-only
// (`ENABLE_CHINESE_VERSION` gates registration; non-CN bitmap fonts have no
// CJK glyphs). All date values come from `computeAlmanac()` (real lunar /
// ganzhi / solar-term routine). The hero day digit reuses SloppyDigits
// (zero-jitter Geometric style).
//
// Up/Down navigates `dayOffset_` (relative to today); Left/Right (handled by
// StandbyActivity) switches between Faces.
class ChineseCalendarFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  TickResult tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override;
  uint32_t secondsUntilNextWake() const override;
  bool wantsGrayscale() const override { return true; }

  // Up → previous day; Down → next day. Clamped to 1900-01-01 / 2100-12-31.
  void onPagePrev() override;
  void onPageNext() override;

 private:
  std::unique_ptr<sloppy::Style> heroStyle_;
  std::unique_ptr<sloppy::Seeds> heroSeeds_;

  // Day navigation: relative to today in the configured fixed offset.
  int32_t dayOffset_ = 0;
  AlmanacDay cachedDay_{};
  bool cacheValid_ = false;
  // Cached "what day was today the last time we refreshed?" so `tick()` can
  // detect midnight crossover when the user is parked on offset 0.
  int32_t cachedBaseDayKey_ = -1;

  // Recompute `cachedDay_` from current local time + dayOffset_. Returns
  // false if the resulting date is outside 1900-2100 or system time isn't
  // available; callers should restore `dayOffset_` to a known-good value.
  bool refreshCachedDay();

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  // On-board SHTC3 ambient reading, shown compactly in the page footer.
  // Present only on the Waveshare 3.97 (the sole board with an SHTC3), so the
  // shared X3/X4 calendar image stays byte-for-byte unchanged.
  uint32_t lastEnvReadMs_ = 0;
  bool haveEnv_ = false;
  float tempC_ = 0.0f;
  float humidityPct_ = 0.0f;
  void refreshEnv();

  // Offline fallback: the almanac is a pure function of the Gregorian date, so
  // we persist the last "today" computed while the clock was valid. On a cold
  // boot before SNTP the page renders that cached day (flagged "更新于 …")
  // instead of going blank. All SD I/O is confined to render() (which holds the
  // activity RenderLock). Waveshare-only: the shared X3/X4 image is unchanged.
  bool usingCache_ = false;         // displayed day came from the offline cache
  bool cacheLoaded_ = false;        // cache-read already attempted this session
  bool cacheWritePending_ = false;  // flush the live "today" to SD in render()
  int64_t shownEpoch_ = 0;          // "更新于" timestamp of the cached day
  bool cacheBaseValid_ = false;     // a cached base date has been loaded
  int cacheBaseYear_ = 0;
  int cacheBaseMonth_ = 0;
  int cacheBaseDay_ = 0;
#endif
};
