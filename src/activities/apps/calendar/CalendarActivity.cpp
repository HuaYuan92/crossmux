#include "CalendarActivity.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "I18nKeys.h"
#include "NetworkStartup.h"
#include "WifiCredentialStore.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MonthGrid.h"
#include "util/TimeUtils.h"

namespace {

constexpr uint32_t kWifiConnectTimeoutMs = 20000u;
constexpr int kSideMargin = 12;
constexpr int kHeaderTop = 8;

}  // namespace

void CalendarActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  state_ = State::Show;
  ownsWifi_ = false;
  errorNoWifi_ = false;
  errorClock_ = false;
  // Barrier: the Confirm/Back press that launched us must not immediately
  // trigger a refresh or exit on its release.
  ignoreConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);

  deriveToday();
  loadHolidaysForYear(viewYear_);
  requestUpdate();
}

void CalendarActivity::onExit() {
  teardownWifi();
  // This is the only landscape app; restore portrait so the next activity
  // (which leaves orientation untouched) does not inherit the sideways view.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Activity::onExit();
}

void CalendarActivity::deriveToday() {
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  std::tm tm{};
  if (now != 0 && TimeUtils::getLocalDateTime(now, tm)) {
    haveToday_ = true;
    todayY_ = tm.tm_year + 1900;
    todayM_ = tm.tm_mon + 1;
    todayD_ = tm.tm_mday;
    viewYear_ = todayY_;
    viewMonth_ = todayM_;
  } else {
    haveToday_ = false;  // cold boot with no SNTP yet: show a navigable month
  }
}

void CalendarActivity::loadHolidaysForYear(const int year) {
  if (!HolidayService::loadYearFromCache(year, holiday_)) {
    holiday_ = HolidayService::YearData{};
  }
}

void CalendarActivity::changeMonth(const int delta) {
  int m = viewMonth_ + delta;
  int y = viewYear_;
  if (m < 1) {
    m = 12;
    --y;
  } else if (m > 12) {
    m = 1;
    ++y;
  }
  if (y < 1900 || y > 2100) return;
  if (y != viewYear_) {
    viewYear_ = y;
    loadHolidaysForYear(y);
  }
  viewMonth_ = m;
  requestUpdate();
}

void CalendarActivity::changeYear(const int delta) {
  const int y = viewYear_ + delta;
  if (y < 1900 || y > 2100) return;
  viewYear_ = y;
  loadHolidaysForYear(y);
  requestUpdate();
}

bool CalendarActivity::bringUpSavedWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  std::string ssid;
  std::string pass;
  if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) {
    const auto credential = WIFI_STORE.findCredential(last);
    if (credential) {
      ssid = credential->ssid;
      pass = credential->password;
    }
  }
  if (ssid.empty()) {
    LOG_ERR("CAL", "No saved WiFi credential");
    return false;
  }
  WiFi.persistent(false);
  NetworkStartup::setMode(renderer, WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  if (pass.empty()) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  ownsWifi_ = true;
  return true;
}

void CalendarActivity::teardownWifi() {
  if (!ownsWifi_) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
  ownsWifi_ = false;
}

void CalendarActivity::startRefresh() {
  if (state_ == State::Connecting || state_ == State::Fetching) return;
  if (!bringUpSavedWifi()) {
    errorNoWifi_ = true;
    state_ = State::Error;
    requestUpdate();
    return;
  }
  errorNoWifi_ = false;
  errorClock_ = false;
  state_ = State::Connecting;
  wifiStartedMs_ = millis();
  requestUpdate();
}

void CalendarActivity::runFetch() {
  // holiday-cn is served over HTTPS: the TLS peer certificate is only accepted
  // once the system clock is trustworthy. This board's RTC has no backup
  // battery, so a cold start can reach here with an invalid clock; sync over the
  // Wi-Fi just brought up and persist it back into the RTC.
  if (!TimeUtils::isClockValid() && !halClock.syncNow()) {
    LOG_ERR("CAL", "clock sync failed; HTTPS would not validate");
    teardownWifi();
    errorClock_ = true;
    state_ = State::Error;
    requestUpdate();
    return;
  }
  HolidayService::YearData out;
  const bool ok = HolidayService::refreshYear(viewYear_, out);
  teardownWifi();
  if (ok) {
    holiday_ = out;
    // A successful sync also fixes an unknown "today"; jump to it once.
    if (!haveToday_) deriveToday();
    state_ = State::Show;
  } else {
    LOG_ERR("CAL", "holiday refresh failed");
    state_ = State::Error;
  }
  requestUpdate();
}

void CalendarActivity::loop() {
  if (ignoreConfirmRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) ignoreConfirmRelease_ = false;
  }
  if (ignoreBackRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) ignoreBackRelease_ = false;
  }

  if (state_ == State::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state_ = State::Fetching;
      requestUpdateAndWait();  // paint "fetching" before the blocking HTTP call
      runFetch();
    } else if (static_cast<uint32_t>(millis() - wifiStartedMs_) >= kWifiConnectTimeoutMs) {
      LOG_ERR("CAL", "WiFi connect timeout");
      teardownWifi();
      state_ = State::Error;
      requestUpdate();
    }
    return;
  }

  const bool confirmReleased =
      !ignoreConfirmRelease_ && mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool backReleased = !ignoreBackRelease_ && mappedInput.wasReleased(MappedInputManager::Button::Back);

  if (backReleased) {
    activityManager.goToApps();
    return;
  }
  if (state_ == State::Error) {
    if (confirmReleased) startRefresh();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    changeMonth(-1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    changeMonth(+1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    changeYear(-1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    changeYear(+1);
  } else if (confirmReleased) {
    startRefresh();
  }
}

void CalendarActivity::drawMessage(const char* text) const {
  const int sh = renderer.getScreenHeight();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawCenteredText(UI_12_FONT_ID, sh / 2 - lh / 2, text);
  renderer.drawCenteredText(SMALL_FONT_ID, sh / 2 + lh, tr(STR_CAL_UPDATE));
}

void CalendarActivity::drawGrid() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  using namespace calendar;
  Cell cells[kCellCount];
  buildMonthGrid(viewYear_, viewMonth_, haveToday_ ? todayY_ : 0, todayM_, todayD_, cells,
                 holiday_.loaded() ? &HolidayService::lookupBadge : nullptr, &holiday_);

  // --- Header: "YYYY年 M月" (left), holiday updated time (right) ---
  const int lh18 = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  char title[48];
  std::snprintf(title, sizeof(title), "%d%s %d%s", viewYear_, tr(STR_CAL_YEAR_SUFFIX), viewMonth_,
                tr(STR_CAL_MONTH_SUFFIX));
  renderer.drawText(NOTOSANS_18_FONT_ID, kSideMargin, kHeaderTop, title, true, EpdFontFamily::BOLD);
  if (holiday_.loaded() && holiday_.fetchedEpoch != 0) {
    std::tm upTm{};
    if (TimeUtils::getLocalDateTime(static_cast<uint32_t>(holiday_.fetchedEpoch), upTm)) {
      char up[40];
      std::snprintf(up, sizeof(up), "%s %02d-%02d", tr(STR_UPDATED_AT), upTm.tm_mon + 1, upTm.tm_mday);
      const int uw = renderer.getTextWidth(SMALL_FONT_ID, up);
      renderer.drawText(SMALL_FONT_ID, sw - kSideMargin - uw, kHeaderTop + 6, up, true);
    }
  }

  // --- Weekday header row (Monday-first) ---
  const int footH = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int wdY = kHeaderTop + lh18 + 2;
  const int gridTop = wdY + renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int gridBottom = sh - footH - 2;
  const int colW = sw / kCols;
  const int rowH = (gridBottom - gridTop) / kRows;

  static const StrId kWd[7] = {StrId::STR_CAL_WD_MON, StrId::STR_CAL_WD_TUE, StrId::STR_CAL_WD_WED,
                               StrId::STR_CAL_WD_THU, StrId::STR_CAL_WD_FRI, StrId::STR_CAL_WD_SAT,
                               StrId::STR_CAL_WD_SUN};
  const int smallLh = renderer.getLineHeight(SMALL_FONT_ID);
  for (int col = 0; col < kCols; ++col) {
    const bool weekend = (col >= 5);
    const EpdFontFamily::Style style = weekend ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const char* wd = I18N.get(kWd[col]);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, wd, style);
    renderer.drawText(SMALL_FONT_ID, col * colW + (colW - w) / 2, wdY, wd, true, style);
  }
  renderer.drawLine(0, gridTop - 2, sw, gridTop - 2, true);

  // --- Cells ---
  const int numLh = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  for (int i = 0; i < kCellCount; ++i) {
    const Cell& c = cells[i];
    const int col = i % kCols;
    const int row = i / kCols;
    const int cellX = col * colW;
    const int cellY = gridTop + row * rowH;

    if (!c.inMonth) {
      renderer.fillRectDither(cellX + 1, cellY + 1, colW - 2, rowH - 2, Color::LightGray);
    }
    if (c.isToday) {
      renderer.drawRoundedRect(cellX + 1, cellY + 1, colW - 2, rowH - 2, 2, 6, true);
    }

    char num[8];
    std::snprintf(num, sizeof(num), "%d", c.day);
    renderer.drawText(NOTOSANS_18_FONT_ID, cellX + 6, cellY + 3, num, true,
                      c.isWeekend ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (c.label && c.label[0]) {
      renderer.drawText(SMALL_FONT_ID, cellX + 6, cellY + 3 + numLh, c.label, true);
    }

    if (c.badge != Badge::None) {
      const char* bt = (c.badge == Badge::Off) ? tr(STR_CAL_BADGE_OFF) : tr(STR_CAL_BADGE_WORK);
      const int bw = renderer.getTextWidth(SMALL_FONT_ID, bt) + 6;
      const int bh = smallLh + 2;
      const int bx = cellX + colW - bw - 2;
      const int by = cellY + 2;
      renderer.fillRect(bx, by, bw, bh, true);
      renderer.drawText(SMALL_FONT_ID, bx + 3, by + 1, bt, /*black=*/false);
    }
  }

  // --- Footer hints ---
  const int fy = sh - smallLh - 4;
  renderer.drawText(SMALL_FONT_ID, kSideMargin, fy, tr(STR_BACK), true);
  const char* upd = tr(STR_CAL_UPDATE);
  renderer.drawText(SMALL_FONT_ID, sw - kSideMargin - renderer.getTextWidth(SMALL_FONT_ID, upd), fy, upd, true);
}

void CalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state_) {
    case State::Connecting:
      drawMessage(tr(STR_CONNECTING));
      break;
    case State::Fetching:
      drawMessage(tr(STR_CAL_UPDATE));
      break;
    case State::Error: {
      const char* msg = errorClock_ ? tr(STR_CLOCK_SYNC_FAIL)
                        : errorNoWifi_ ? tr(STR_WEATHER_NO_WIFI)
                                       : tr(STR_CAL_UPDATE_FAILED);
      drawMessage(msg);
      break;
    }
    case State::Show:
    default:
      drawGrid();
      break;
  }
  renderer.displayBuffer();
}

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
