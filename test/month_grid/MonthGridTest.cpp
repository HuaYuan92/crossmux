#include "util/MonthGrid.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace calendar;

namespace {

// Index of a Gregorian day within the 2026-09 grid (Monday-first, lead = 1
// because Sep 1 2026 is a Tuesday).
int indexOf(int day) { return (day - 1) + 1 /*lead*/; }

Badge fakeHoliday(int year, int month, int day, void* ctx) {
  (void)ctx;
  if (year == 2026 && month == 10 && day >= 1 && day <= 3) return Badge::Off;    // 国庆休
  if (year == 2026 && month == 9 && day == 20) return Badge::Workday;            // 补班
  return Badge::None;
}

}  // namespace

TEST(MonthGrid, LeadingCellIsPreviousMonthMonday) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells);
  EXPECT_EQ(cells[0].year, 2026);
  EXPECT_EQ(cells[0].month, 8);
  EXPECT_EQ(cells[0].day, 31);
  EXPECT_FALSE(cells[0].inMonth);
  EXPECT_FALSE(cells[0].isWeekend);  // Monday
}

TEST(MonthGrid, FirstOfMonthLandsOnTuesdayColumn) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells);
  EXPECT_EQ(cells[1].day, 1);
  EXPECT_TRUE(cells[1].inMonth);
  // Column of index 1 is (1 % kCols) == 1, i.e. the second column = Tuesday.
  EXPECT_EQ(1 % kCols, 1);
}

TEST(MonthGrid, WeekendFlags) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells);
  EXPECT_TRUE(cells[indexOf(12)].isWeekend);   // Sat
  EXPECT_TRUE(cells[indexOf(13)].isWeekend);   // Sun
  EXPECT_FALSE(cells[indexOf(7)].isWeekend);   // Mon
  EXPECT_FALSE(cells[indexOf(30)].isWeekend);  // Wed
}

TEST(MonthGrid, LabelPriorityFestivalTermThenLunar) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells);
  EXPECT_STREQ(cells[indexOf(7)].label, "白露");      // solar term
  EXPECT_TRUE(cells[indexOf(7)].labelIsSpecial);
  EXPECT_STREQ(cells[indexOf(10)].label, "教师节");   // gregorian festival
  EXPECT_TRUE(cells[indexOf(10)].labelIsSpecial);
  EXPECT_STREQ(cells[indexOf(23)].label, "秋分");     // solar term
  EXPECT_STREQ(cells[indexOf(25)].label, "中秋节");   // lunar festival 八月十五
  EXPECT_TRUE(cells[indexOf(25)].labelIsSpecial);
  EXPECT_FALSE(cells[indexOf(2)].labelIsSpecial);    // a plain lunar day
  EXPECT_NE(cells[indexOf(2)].label[0], '\0');       // non-empty
}

TEST(MonthGrid, TodayHighlightIsUnique) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 2026, 9, 29, cells);
  int todayCount = 0;
  for (int i = 0; i < kCellCount; ++i) {
    if (cells[i].isToday) {
      ++todayCount;
      EXPECT_EQ(cells[i].day, 29);
    }
  }
  EXPECT_EQ(todayCount, 1);
}

TEST(MonthGrid, TrailingCellIsNextMonthWithFestival) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells);
  EXPECT_EQ(cells[31].month, 10);
  EXPECT_EQ(cells[31].day, 1);
  EXPECT_FALSE(cells[31].inMonth);
  EXPECT_STREQ(cells[31].label, "国庆节");
}

TEST(MonthGrid, EveryCellHasALabel) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells);
  for (int i = 0; i < kCellCount; ++i) {
    EXPECT_NE(cells[i].label[0], '\0') << "cell " << i;
  }
}

TEST(MonthGrid, HolidayBadgesPropagateFromLookup) {
  Cell cells[kCellCount];
  buildMonthGrid(2026, 9, 0, 0, 0, cells, &fakeHoliday, nullptr);
  EXPECT_EQ(cells[indexOf(20)].badge, Badge::Workday);
  EXPECT_EQ(cells[31].badge, Badge::Off);  // Oct 1
  EXPECT_EQ(cells[indexOf(15)].badge, Badge::None);
}

TEST(MonthGrid, FebruaryLeapYearGrid) {
  Cell cells[kCellCount];
  buildMonthGrid(2024, 2, 0, 0, 0, cells);  // 2024 leap: Feb has 29 days
  bool saw29 = false;
  for (int i = 0; i < kCellCount; ++i) {
    if (cells[i].month == 2 && cells[i].day == 29 && cells[i].inMonth) saw29 = true;
  }
  EXPECT_TRUE(saw29);
}
