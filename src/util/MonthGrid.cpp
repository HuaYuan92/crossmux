#include "MonthGrid.h"

#ifdef ENABLE_CHINESE_VERSION

#include "activities/apps/standby/ChineseAlmanac.h"

namespace calendar {
namespace {

struct FixedFestival {
  uint8_t month;  // 1..12
  uint8_t day;    // 1..31
  const char* name;
};

// Fixed Gregorian-date festivals. Every name's glyphs are present in the small
// CN font subset (cn_common_chars.txt).
constexpr FixedFestival kGregorianFestivals[] = {
    {1, 1, "元旦"},   {2, 14, "情人节"}, {3, 8, "妇女节"},  {3, 12, "植树节"}, {5, 1, "劳动节"},
    {5, 4, "青年节"}, {6, 1, "儿童节"},  {7, 1, "建党节"},  {8, 1, "建军节"},  {9, 10, "教师节"},
    {10, 1, "国庆节"}, {11, 8, "记者节"}, {12, 25, "圣诞节"},
};

// Fixed lunar-date festivals, keyed by (lunar month, lunar day).
constexpr FixedFestival kLunarFestivals[] = {
    {1, 1, "春节"},   {1, 15, "元宵节"}, {2, 2, "龙抬头"},  {5, 5, "端午节"},
    {7, 7, "七夕"},   {7, 15, "中元节"}, {8, 15, "中秋节"}, {9, 9, "重阳节"},
    {12, 8, "腊八节"},
};

const char* findFestival(const FixedFestival* table, size_t count, int month, int day) {
  for (size_t i = 0; i < count; ++i) {
    if (table[i].month == month && table[i].day == day) return table[i].name;
  }
  return nullptr;
}

// Resolve the sub-label for a date and whether it is "special" (festival or
// solar term, as opposed to the plain lunar day). Priority: gregorian festival
// > lunar festival > solar term > lunar day.
void resolveLabel(int y, int m, int d, const char*& label, bool& special) {
  if (const char* f = findFestival(kGregorianFestivals, sizeof(kGregorianFestivals) / sizeof(kGregorianFestivals[0]), m, d)) {
    label = f;
    special = true;
    return;
  }
  chinese_almanac::LunarDate ld;
  if (chinese_almanac::lunarOfDate(y, m, d, ld) && !ld.leap) {
    if (const char* f = findFestival(kLunarFestivals, sizeof(kLunarFestivals) / sizeof(kLunarFestivals[0]), ld.month, ld.day)) {
      label = f;
      special = true;
      return;
    }
  }
  const int term = chinese_almanac::solarTermOnDate(y, m, d);
  if (term >= 0) {
    label = chinese_almanac::kSolarTermNames[term];
    special = true;
    return;
  }
  label = chinese_almanac::lunarDayLabel(y, m, d);
  special = false;
}

}  // namespace

void buildMonthGrid(int year, int month, int todayY, int todayM, int todayD, Cell cells[kCellCount],
                    HolidayLookup lookup, void* ctx) {
  const int firstWeekday = chinese_almanac::weekdayOf(year, month, 1);  // 0=Sun..6=Sat
  const int lead = (firstWeekday + 6) % 7;                              // Monday-first column of the 1st

  for (int i = 0; i < kCellCount; ++i) {
    int y = year;
    int m = month;
    int d = 1 + (i - lead);
    while (d < 1) {
      m -= 1;
      if (m < 1) {
        m = 12;
        y -= 1;
      }
      d += chinese_almanac::daysInMonth(y, m);
    }
    while (d > chinese_almanac::daysInMonth(y, m)) {
      d -= chinese_almanac::daysInMonth(y, m);
      m += 1;
      if (m > 12) {
        m = 1;
        y += 1;
      }
    }

    Cell& c = cells[i];
    c.year = static_cast<int16_t>(y);
    c.month = static_cast<uint8_t>(m);
    c.day = static_cast<uint8_t>(d);
    c.inMonth = (y == year && m == month);
    const int weekday = chinese_almanac::weekdayOf(y, m, d);
    c.isWeekend = (weekday == 0 || weekday == 6);
    c.isToday = (todayY != 0 && y == todayY && m == todayM && d == todayD);
    resolveLabel(y, m, d, c.label, c.labelIsSpecial);
    c.badge = lookup ? lookup(y, m, d, ctx) : Badge::None;
  }
}

}  // namespace calendar

#else  // !ENABLE_CHINESE_VERSION

namespace calendar {
void buildMonthGrid(int, int, int, int, int, Cell cells[kCellCount], HolidayLookup, void*) {
  for (int i = 0; i < kCellCount; ++i) cells[i] = Cell{};
}
}  // namespace calendar

#endif  // ENABLE_CHINESE_VERSION
