#pragma once

#include <cstdint>

// Pure month-grid computation for the calendar app and its standby face. Turns
// a (year, month) into a 6x7 grid of cells (week-major, Monday-first), each
// carrying the Gregorian day, a sub-label and its badge state.
//
// Sub-label priority matches everyday Chinese calendars: festival name > solar
// term name > lunar day name. The lunar/solar-term data comes from
// ChineseAlmanac, so the body is gated by ENABLE_CHINESE_VERSION; non-CN builds
// compile this TU into an empty object.
//
// No GfxRenderer / I18n / heap dependency: `buildMonthGrid` fills a caller-
// owned array, so it is directly unit-testable on the host.

namespace calendar {

enum class Badge : uint8_t {
  None,    // ordinary day
  Off,     // statutory holiday (休)
  Workday  // makeup workday on a weekend (班)
};

struct Cell {
  int16_t year;
  uint8_t month;         // 1..12 (the cell's own Gregorian month)
  uint8_t day;           // 1..31
  bool inMonth;          // belongs to the requested month (vs adjacent filler)
  bool isWeekend;        // Saturday or Sunday
  bool isToday;
  const char* label;     // festival / solar-term / lunar text; "" when none
  bool labelIsSpecial;   // true for festival or solar-term (render emphasized)
  Badge badge;
};

constexpr int kCols = 7;
constexpr int kRows = 6;
constexpr int kCellCount = kCols * kRows;

// Caller-supplied holiday lookup. Returns the badge for a Gregorian date; the
// opaque `ctx` lets the caller close over its own data (e.g. a cached year
// table). Pass nullptr to leave every badge at None.
using HolidayLookup = Badge (*)(int year, int month, int day, void* ctx);

// Fill `cells` (kCellCount entries) for the given month. todayY/M/D marks
// "today" (pass 0 for year to disable). Rows are weeks; column 0 is Monday.
void buildMonthGrid(int year, int month, int todayY, int todayM, int todayD, Cell cells[kCellCount],
                    HolidayLookup lookup = nullptr, void* ctx = nullptr);

}  // namespace calendar
