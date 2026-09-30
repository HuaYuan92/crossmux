#pragma once

#include <cstdint>

#include "StandbyFace.h"
#include "util/HolidayService.h"

// Standby "month grid" face for the Waveshare 3.97 build. Renders a
// Monday-first 6x7 month calendar in the portrait viewport, matching the
// standalone Calendar app's visual language: bolded weekends, a rounded frame on
// today, dithered adjacent-month cells, and lunar / solar-term / festival
// sub-labels from MonthGrid. Statutory 休/班 badges come from the SD cache that
// the Calendar app writes — this face is read-only and never touches the
// network. All SD I/O is confined to render() (which runs under the activity's
// RenderLock, since the e-ink panel shares the SPI bus with the card).
//
// Up/Down (dispatched by StandbyActivity to onPagePrev/onPageNext) page the
// month; Left/Right switch between faces. Waveshare-only: the shared X3/X4 image
// is unaffected.
class MonthCalendarFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  TickResult tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override;
  uint32_t secondsUntilNextWake() const override;

  void onPagePrev() override;  // previous month
  void onPageNext() override;  // next month

 private:
  // Read the wall clock; when valid set haveToday_ + todayY/M/D. Returns false
  // (leaving the last-known view) when the clock is not yet trustworthy.
  bool deriveToday();
  // Recompute viewYear_/viewMonth_ from today + monthOffset_. No-op when the
  // clock is invalid and the user has not navigated.
  void recomputeView();
  // Change monthOffset_ by delta, clamped to a navigable range, and mark the
  // holiday cache for a (lazy) re-read when the displayed year changes.
  void changeMonth(int32_t delta);

  int32_t monthOffset_ = 0;  // months relative to the current month
  int viewYear_ = 0;         // displayed month (1..12 in viewMonth_)
  int viewMonth_ = 0;

  int todayY_ = 0;
  int todayM_ = 0;
  int todayD_ = 0;
  bool haveToday_ = false;

  bool navigated_ = false;  // user paged away from the current month
  // Tracks the date the view was last rebased on, so tick() can detect a
  // midnight crossover while parked on the current month.
  int32_t cachedBaseDayKey_ = -1;

  HolidayService::YearData holiday_;
  int holidayLoadedYear_ = 0;  // year currently reflected in holiday_ (0 = none)
};
