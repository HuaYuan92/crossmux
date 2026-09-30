#include "MonthCalendarFace.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "I18nKeys.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MonthGrid.h"
#include "util/TimeUtils.h"

namespace {

// Keep clear of the chrome StandbyActivity draws in Normal mode: the centered
// title on top and the face pager-dots along the bottom.
constexpr int kTopInset = 44;
constexpr int kBottomInset = 30;
constexpr int kBadgePad = 6;

const char* weekdayText(const int col) {
  static const StrId kWd[7] = {StrId::STR_CAL_WD_MON, StrId::STR_CAL_WD_TUE, StrId::STR_CAL_WD_WED,
                               StrId::STR_CAL_WD_THU, StrId::STR_CAL_WD_FRI, StrId::STR_CAL_WD_SAT,
                               StrId::STR_CAL_WD_SUN};
  return I18N.get(kWd[col]);
}

// Lay out a Monday-first 6x7 month grid inside `viewport`, sharing the calendar
// app's visual language: bolded weekends, a rounded frame on today, dithered
// adjacent-month cells, festival / solar-term / lunar sub-labels, and 休/班 chips.
void drawMonthGrid(GfxRenderer& renderer, const Rect& viewport, int year, int month,
                   const calendar::Cell cells[calendar::kCellCount]) {
  const int vw = viewport.width;
  const int vh = viewport.height;
  if (vw <= 0 || vh <= 0) return;

  using namespace calendar;

  // --- Month title (centered), leaving the top band for the activity title ---
  const int lh18 = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int smallLh = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleY = viewport.y + kTopInset;
  char title[48];
  std::snprintf(title, sizeof(title), "%d%s%d%s", year, tr(STR_CAL_YEAR_SUFFIX), month,
                tr(STR_CAL_MONTH_SUFFIX));
  const int titleW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, title, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_18_FONT_ID, viewport.x + (vw - titleW) / 2, titleY, title, true, EpdFontFamily::BOLD);

  // --- Weekday header row (Monday-first) ---
  const int wdY = titleY + lh18 + 4;
  const int gridTop = wdY + smallLh + 4;
  const int gridBottom = viewport.y + vh - kBottomInset;
  const int colW = vw / kCols;
  const int rowH = (gridBottom - gridTop) / kRows;
  for (int col = 0; col < kCols; ++col) {
    const bool weekend = (col >= 5);
    const EpdFontFamily::Style style = weekend ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const char* wd = weekdayText(col);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, wd, style);
    renderer.drawText(SMALL_FONT_ID, viewport.x + col * colW + (colW - w) / 2, wdY, wd, true, style);
  }
  renderer.drawLine(viewport.x, gridTop - 3, viewport.x + vw, gridTop - 3, true);

  // --- Cells ---
  const int numLh = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  for (int i = 0; i < kCellCount; ++i) {
    const Cell& c = cells[i];
    const int col = i % kCols;
    const int row = i / kCols;
    const int cellX = viewport.x + col * colW;
    const int cellY = gridTop + row * rowH;

    if (!c.inMonth) {
      renderer.fillRectDither(cellX + 1, cellY + 1, colW - 2, rowH - 2, Color::LightGray);
    }
    if (c.isToday) {
      renderer.drawRoundedRect(cellX + 1, cellY + 1, colW - 2, rowH - 2, 2, 6, true);
    }

    char num[8];
    std::snprintf(num, sizeof(num), "%d", c.day);
    const int numW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, num,
                                           c.isWeekend ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(NOTOSANS_18_FONT_ID, cellX + (colW - numW) / 2, cellY + 4, num, true,
                      c.isWeekend ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (c.label && c.label[0]) {
      const int lw = renderer.getTextWidth(SMALL_FONT_ID, c.label);
      renderer.drawText(SMALL_FONT_ID, cellX + (colW - lw) / 2, cellY + 4 + numLh, c.label, true);
    }

    if (c.badge != Badge::None) {
      const char* bt = (c.badge == Badge::Off) ? tr(STR_CAL_BADGE_OFF) : tr(STR_CAL_BADGE_WORK);
      const int bw = renderer.getTextWidth(SMALL_FONT_ID, bt) + kBadgePad;
      const int bh = smallLh + 2;
      const int bx = cellX + colW - bw - 2;
      const int by = cellY + 2;
      renderer.fillRect(bx, by, bw, bh, true);
      renderer.drawText(SMALL_FONT_ID, bx + kBadgePad / 2, by + 1, bt, /*black=*/false);
    }
  }
}

}  // namespace

void MonthCalendarFace::onEnter() {
  monthOffset_ = 0;
  navigated_ = false;
  holiday_ = HolidayService::YearData{};
  holidayLoadedYear_ = 0;
  haveToday_ = deriveToday();
  recomputeView();
}

void MonthCalendarFace::onExit() {
  navigated_ = false;
  monthOffset_ = 0;
}

bool MonthCalendarFace::deriveToday() {
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  std::tm tm{};
  if (now != 0 && TimeUtils::getLocalDateTime(now, tm)) {
    haveToday_ = true;
    todayY_ = tm.tm_year + 1900;
    todayM_ = tm.tm_mon + 1;
    todayD_ = tm.tm_mday;
    cachedBaseDayKey_ = tm.tm_year * 512 + tm.tm_yday;
    return true;
  }
  haveToday_ = false;
  return false;
}

void MonthCalendarFace::recomputeView() {
  if (!haveToday_) return;  // keep the last known view until the clock is valid
  int year = todayY_;
  int month = todayM_ + static_cast<int>(monthOffset_);
  while (month < 1) {
    month += 12;
    --year;
  }
  while (month > 12) {
    month -= 12;
    ++year;
  }
  if (year < 1900) {
    year = 1900;
    month = 1;
  } else if (year > 2100) {
    year = 2100;
    month = 12;
  }
  viewYear_ = year;
  viewMonth_ = month;
}

void MonthCalendarFace::changeMonth(const int32_t delta) {
  const int32_t next = monthOffset_ + delta;
  // Clamp to ~±80 years of months around today; the rebase keeps it navigable
  // without risking an unbounded offset.
  if (next < -960 || next > 960) return;
  monthOffset_ = next;
  navigated_ = true;
  recomputeView();
}

void MonthCalendarFace::onPagePrev() { changeMonth(-1); }

void MonthCalendarFace::onPageNext() { changeMonth(+1); }

StandbyFace::TickResult MonthCalendarFace::tick() {
  // When parked on the current month, follow the wall clock: re-derive on
  // midnight crossover (and once the clock first becomes valid). A navigated
  // view is left alone so we don't yank the user back.
  if (navigated_) return TickResult::None;
  struct tm tm{};
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (now == 0 || !TimeUtils::getLocalDateTime(now, tm)) return TickResult::None;
  const int32_t key = tm.tm_year * 512 + tm.tm_yday;
  if (haveToday_ && key == cachedBaseDayKey_) return TickResult::None;
  deriveToday();
  recomputeView();
  return TickResult::Redraw;
}

StrId MonthCalendarFace::titleId() const { return StrId::STR_CALENDAR_TITLE; }

uint32_t MonthCalendarFace::secondsUntilNextWake() const {
  // Repaint at most once a day (midnight moves "today"); StandbyActivity
  // bounds this for USB polling anyway.
  return 3600u;
}

void MonthCalendarFace::render(GfxRenderer& renderer, const Rect& viewport) {
  // Refresh "today" each frame (cheap clock read, no SD) so the highlight and
  // month are current without waiting for a tick boundary.
  deriveToday();
  recomputeView();
  if (viewMonth_ == 0) return;  // clock not yet valid and never set: activity shows "syncing"

  // All SD I/O lives here: render() runs under the activity's RenderLock, and
  // this one read per displayed year keeps bus traffic minimal even across the
  // repeated grayscale passes (which are disabled for this face, but the guard
  // also covers re-renders on navigation).
  if (holidayLoadedYear_ != viewYear_) {
    holidayLoadedYear_ = viewYear_;
    if (!HolidayService::loadYearFromCache(viewYear_, holiday_)) {
      holiday_ = HolidayService::YearData{};
    }
  }

  calendar::Cell cells[calendar::kCellCount];
  calendar::buildMonthGrid(viewYear_, viewMonth_, haveToday_ ? todayY_ : 0, todayM_, todayD_, cells,
                           holiday_.loaded() ? &HolidayService::lookupBadge : nullptr, &holiday_);
  drawMonthGrid(renderer, viewport, viewYear_, viewMonth_, cells);
}

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
