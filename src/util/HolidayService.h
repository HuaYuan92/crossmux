#pragma once

#include <cstdint>

#include "util/MonthGrid.h"

// Statutory holiday (休) / makeup-workday (班) data for the Chinese calendar,
// sourced from the key-free holiday-cn dataset (one small JSON per year) and
// cached to SD. Like WeatherService, this never touches WiFi itself: refreshYear
// speaks HTTP over an already-connected interface, while the calendar app and
// its standby face only ever read the cache. Fixed-layout struct (no
// std::string / std::vector members) keeps it heap-free.
namespace HolidayService {

constexpr int kMaxEntries = 64;  // holiday-cn lists ~40 special days per year

struct YearData {
  int year = 0;  // 0 = not loaded
  int64_t fetchedEpoch = 0;
  struct Entry {
    uint8_t month;  // 1..12
    uint8_t day;    // 1..31
    bool off;       // true = 休 (statutory rest), false = 班 (makeup workday)
  };
  Entry entries[kMaxEntries];
  uint8_t count = 0;

  bool loaded() const { return year != 0; }
};

// Read the SD cache for `year` (/.crosspoint/holidays_<year>.json). No network;
// safe from a render path. false when absent or unreadable.
bool loadYearFromCache(int year, YearData& out);

// Fetch `year` over an already-connected network and persist the cache. On any
// network/parse failure returns false and leaves `out` and the cache untouched.
bool refreshYear(int year, YearData& out);

// calendar::HolidayLookup-compatible adapter; `ctx` points to a YearData. Days
// outside the loaded year return None.
calendar::Badge lookupBadge(int year, int month, int day, void* ctx);

}  // namespace HolidayService
