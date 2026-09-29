#include "WeatherActivity.h"

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

#include "NetworkStartup.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"
#include "util/WeatherGlyph.h"

namespace {

constexpr uint32_t kWifiConnectTimeoutMs = 20000u;
constexpr int kSideMargin = 24;

const char* weekdayStrId(const int tmWday) {
  static const StrId kIds[7] = {StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE,
                                StrId::STR_CAL_WEEKDAY_WED, StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI,
                                StrId::STR_CAL_WEEKDAY_SAT};
  if (tmWday < 0 || tmWday > 6) return "";
  return I18N.get(kIds[tmWday]);
}

// Draw `label: value` with the label dimmed and the value in black, left at x.
void drawLabelValue(GfxRenderer& r, const int fontId, const int x, const int y, const char* label, const char* value) {
  r.drawText(fontId, x, y, label, /*black=*/true);
  const int lw = r.getTextWidth(fontId, label);
  r.drawText(fontId, x + lw + 4, y, value, /*black=*/true);
}

}  // namespace

void WeatherActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  state_ = State::Show;
  ownsWifi_ = false;
  errorNoWifi_ = false;
  errorClock_ = false;
  // Barrier: the Confirm/Back press that launched us must not immediately
  // trigger a refresh or exit on its release.
  ignoreConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
  if (!WeatherService::loadCache(data_)) {
    data_ = WeatherData{};
  }
  requestUpdate();
}

void WeatherActivity::onExit() {
  teardownWifi();
  Activity::onExit();
}

bool WeatherActivity::bringUpSavedWifi() {
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
    LOG_ERR("WXAPP", "No saved WiFi credential");
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

void WeatherActivity::teardownWifi() {
  if (!ownsWifi_) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
  ownsWifi_ = false;
}

void WeatherActivity::startRefresh() {
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

void WeatherActivity::runFetch() {
  // Open-Meteo and the IP lookup are HTTPS: the TLS peer certificate is only
  // accepted once the system clock is trustworthy. This board's PCF85063A has
  // no backup battery, so a cold start can reach here with an invalid clock;
  // sync over the Wi-Fi we just brought up and persist it back into the RTC.
  // Matches the existing WeRead progress-sync precedent.
  if (!TimeUtils::isClockValid() && !halClock.syncNow()) {
    LOG_ERR("WXAPP", "clock sync failed; HTTPS would not validate");
    teardownWifi();
    errorClock_ = true;
    state_ = State::Error;
    requestUpdate();
    return;
  }
  WeatherData out;
  const bool ok = WeatherService::refresh(out, false, 0.0, 0.0, "");
  teardownWifi();
  if (ok) {
    data_ = out;
    state_ = State::Show;
  } else {
    LOG_ERR("WXAPP", "refresh failed");
    state_ = State::Error;
  }
  requestUpdate();
}

void WeatherActivity::loop() {
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
      LOG_ERR("WXAPP", "WiFi connect timeout");
      teardownWifi();
      errorNoWifi_ = false;
      errorClock_ = false;
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
  if (confirmReleased) {
    startRefresh();
  }
}

void WeatherActivity::drawMessage(const char* text) const {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawCenteredText(UI_12_FONT_ID, sh / 2 - lh / 2, text);
  renderer.drawCenteredText(SMALL_FONT_ID, sh / 2 + lh, tr(STR_WEATHER_REFRESH));
  (void)sw;
}

void WeatherActivity::drawWeather() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int right = sw - kSideMargin;

  // --- Header: today's date + weekday (left), "updated" time (right) ---
  int y = 24;
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  std::tm nowTm{};
  if (now != 0 && TimeUtils::getLocalDateTime(now, nowTm)) {
    char date[32];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d", nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday);
    char line[64];
    std::snprintf(line, sizeof(line), "%s %s", date, weekdayStrId(nowTm.tm_wday));
    renderer.drawText(UI_12_FONT_ID, kSideMargin, y, line, true);
  }
  if (data_.fetchedEpoch != 0) {
    std::tm upTm{};
    if (TimeUtils::getLocalDateTime(static_cast<uint32_t>(data_.fetchedEpoch), upTm)) {
      char up[32];
      std::snprintf(up, sizeof(up), "%s %02d:%02d", tr(STR_UPDATED_AT), upTm.tm_hour, upTm.tm_min);
      const int uw = renderer.getTextWidth(SMALL_FONT_ID, up);
      renderer.drawText(SMALL_FONT_ID, right - uw, y + 4, up, true);
    }
  }

  // --- City ---
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  const char* city = data_.city[0] != '\0' ? data_.city : tr(STR_FACE_WEATHER);
  renderer.drawText(NOTOSANS_18_FONT_ID, kSideMargin, y, city, true);

  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 8;
  renderer.drawLine(kSideMargin, y, right, y, true);

  // --- Hero: glyph + big temperature + description ---
  y += 24;
  const int heroCx = kSideMargin + 40;
  const int heroCy = y + 30;
  WeatherGlyph::draw(renderer, data_.weatherCode, heroCx, heroCy, 16);

  char temp[24];
  std::snprintf(temp, sizeof(temp), "%.1f\xC2\xB0" "C", data_.temperatureC);
  renderer.drawText(NOTOSANS_18_FONT_ID, heroCx + 56, heroCy - 18, temp, true);
  renderer.drawText(NOTOSANS_14_FONT_ID, heroCx + 56, heroCy + 8, WeatherService::describe(data_.weatherCode), true);

  y = heroCy + 44;
  renderer.drawLine(kSideMargin, y, right, y, true);

  // --- Details grid ---
  y += 16;
  const int col2 = sw / 2 + 8;
  const int rowStep = renderer.getLineHeight(NOTOSANS_12_FONT_ID) + 12;
  char val[40];

  std::snprintf(val, sizeof(val), "%d%%", static_cast<int>(data_.humidityPct + 0.5f));
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, kSideMargin, y, tr(STR_ENV_HUMIDITY), val);

  std::snprintf(val, sizeof(val), "%.1f\xC2\xB0" "C", data_.apparentC);
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, col2, y, tr(STR_APPARENT_TEMP), val);

  y += rowStep;
  const char* wind = WeatherService::windDirLabel(data_.windDirDeg);
  std::snprintf(val, sizeof(val), "%s %dkm/h", wind && wind[0] ? wind : "",
                static_cast<int>(data_.windSpeedKmh + 0.5f));
  renderer.drawText(NOTOSANS_12_FONT_ID, kSideMargin, y, val, true);

  std::snprintf(val, sizeof(val), "%s", data_.sunrise);
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, col2, y, tr(STR_SUNRISE), val);

  y += rowStep;
  std::snprintf(val, sizeof(val), "%s", data_.sunset);
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, kSideMargin, y, tr(STR_SUNSET), val);

  y += rowStep;
  renderer.drawLine(kSideMargin, y, right, y, true);

  // --- Forecast: vertical list, one row per day, filling down to the footer ---
  y += 14;
  const int days = data_.dayCount > 4 ? 4 : data_.dayCount;
  if (days > 0) {
    const int footerY = sh - renderer.getLineHeight(SMALL_FONT_ID) - 12;
    const int listTop = y;
    const int rowH = (footerY - 10 - listTop) / days;
    const int glyphR = 10;
    const int weekdayW = renderer.getTextWidth(SMALL_FONT_ID, "星期三");
    const int glyphCx = kSideMargin + weekdayW + 30 + glyphR;
    const int descX = glyphCx + glyphR + 14;
    const int smallLh = renderer.getLineHeight(SMALL_FONT_ID);
    const int bodyLh = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
    const bool clockOk = (now != 0);
    for (int i = 0; i < days; ++i) {
      const int rowTop = listTop + i * rowH;
      const int rowMid = rowTop + rowH / 2;
      char dlabel[24];
      if (clockOk) {
        std::snprintf(dlabel, sizeof(dlabel), "%s", weekdayStrId((nowTm.tm_wday + i) % 7));
      } else {
        std::snprintf(dlabel, sizeof(dlabel), "+%d", i);
      }
      renderer.drawText(SMALL_FONT_ID, kSideMargin, rowMid - smallLh / 2, dlabel, true);
      WeatherGlyph::draw(renderer, data_.days[i].code, glyphCx, rowMid, glyphR);
      const char* desc = WeatherService::describe(data_.days[i].code);
      renderer.drawText(NOTOSANS_12_FONT_ID, descX, rowMid - bodyLh / 2, desc, true);
      // Forecast range reads low~high with a unit, matching everyday habit.
      char range[32];
      std::snprintf(range, sizeof(range), "%d~%d\xC2\xB0" "C", static_cast<int>(data_.days[i].minC + 0.5f),
                    static_cast<int>(data_.days[i].maxC + 0.5f));
      const int rangeW = renderer.getTextWidth(NOTOSANS_12_FONT_ID, range);
      renderer.drawText(NOTOSANS_12_FONT_ID, right - rangeW, rowMid - bodyLh / 2, range, true);
      if (i + 1 < days) renderer.drawLine(kSideMargin, rowTop + rowH, right, rowTop + rowH, true);
    }
  }

  // --- Footer hints ---
  const int fy = sh - renderer.getLineHeight(SMALL_FONT_ID) - 12;
  renderer.drawText(SMALL_FONT_ID, kSideMargin, fy, tr(STR_BACK), true);
  const char* refresh = tr(STR_WEATHER_REFRESH);
  renderer.drawText(SMALL_FONT_ID, right - renderer.getTextWidth(SMALL_FONT_ID, refresh), fy, refresh, true);
}

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state_) {
    case State::Connecting:
      drawMessage(tr(STR_CONNECTING));
      break;
    case State::Fetching:
      drawMessage(tr(STR_WEATHER_FETCHING));
      break;
    case State::Error: {
      const char* msg = errorClock_ ? tr(STR_CLOCK_SYNC_FAIL)
                        : errorNoWifi_ ? tr(STR_WEATHER_NO_WIFI)
                                       : tr(STR_WEATHER_FETCH_FAILED);
      drawMessage(msg);
      break;
    }
    case State::Show:
    default:
      if (data_.valid) {
        drawWeather();
      } else {
        drawMessage(tr(STR_WEATHER_NO_DATA));
      }
      break;
  }
  renderer.displayBuffer();
}

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
