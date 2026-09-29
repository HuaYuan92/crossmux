#include "WeatherFace.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "I18nKeys.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"
#include "util/WeatherGlyph.h"

namespace {

constexpr int kSideMargin = 24;
// Keep the top clear so the Normal-mode title overlay drawn by StandbyActivity
// does not collide with the face content.
constexpr int kTopInset = 44;

const char* weekdayStrId(const int tmWday) {
  static const StrId kIds[7] = {StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE,
                                StrId::STR_CAL_WEEKDAY_WED, StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI,
                                StrId::STR_CAL_WEEKDAY_SAT};
  if (tmWday < 0 || tmWday > 6) return "";
  return I18N.get(kIds[tmWday]);
}

void drawLabelValue(GfxRenderer& r, const int fontId, const int x, const int y, const char* label, const char* value) {
  r.drawText(fontId, x, y, label, /*black=*/true);
  const int lw = r.getTextWidth(fontId, label);
  r.drawText(fontId, x + lw + 4, y, value, /*black=*/true);
}

}  // namespace

void WeatherFace::onEnter() {
  loaded_ = WeatherService::loadCache(data_);
  if (!loaded_) data_ = WeatherData{};
}

void WeatherFace::onExit() {
  loaded_ = false;
}

StandbyFace::TickResult WeatherFace::tick() {
  // Passive: the picture only changes when the Weather app rewrites the cache,
  // which forces a fresh onEnter(). Nothing time-driven to repaint here.
  return TickResult::None;
}

uint32_t WeatherFace::secondsUntilNextWake() const {
  // No timer-driven content; StandbyActivity bounds this for USB polling.
  return 3600u;
}

void WeatherFace::render(GfxRenderer& renderer, const Rect& viewport) {
  if (!loaded_) {
    loaded_ = WeatherService::loadCache(data_);
    if (!loaded_) data_ = WeatherData{};
  }

  const int vw = viewport.width;
  const int vh = viewport.height;
  if (vw <= 0 || vh <= 0) return;

  if (!data_.valid) {
    UITheme::drawCenteredText(renderer, viewport, NOTOSANS_14_FONT_ID, viewport.y + vh / 2 - 10,
                              tr(STR_WEATHER_NO_DATA));
    return;
  }

  const int left = viewport.x + kSideMargin;
  const int right = viewport.x + vw - kSideMargin;

  // --- Header: today's date + weekday (left), "updated" time (right) ---
  int y = viewport.y + kTopInset;
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  std::tm nowTm{};
  if (now != 0 && TimeUtils::getLocalDateTime(now, nowTm)) {
    char date[32];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d", nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday);
    char line[64];
    std::snprintf(line, sizeof(line), "%s %s", date, weekdayStrId(nowTm.tm_wday));
    renderer.drawText(UI_12_FONT_ID, left, y, line, true);
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
  renderer.drawText(NOTOSANS_18_FONT_ID, left, y, city, true);

  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 8;
  renderer.drawLine(left, y, right, y, true);

  // --- Hero: glyph + big temperature + description ---
  y += 24;
  const int heroCx = left + 40;
  const int heroCy = y + 30;
  WeatherGlyph::draw(renderer, data_.weatherCode, heroCx, heroCy, 16);

  char temp[24];
  std::snprintf(temp, sizeof(temp), "%.1f\xC2\xB0" "C", data_.temperatureC);
  renderer.drawText(NOTOSANS_18_FONT_ID, heroCx + 56, heroCy - 18, temp, true);
  renderer.drawText(NOTOSANS_14_FONT_ID, heroCx + 56, heroCy + 8, WeatherService::describe(data_.weatherCode), true);

  y = heroCy + 44;
  renderer.drawLine(left, y, right, y, true);

  // --- Details grid ---
  y += 16;
  const int col2 = viewport.x + vw / 2 + 8;
  const int rowStep = renderer.getLineHeight(NOTOSANS_12_FONT_ID) + 12;
  char val[40];

  std::snprintf(val, sizeof(val), "%d%%", static_cast<int>(data_.humidityPct + 0.5f));
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, left, y, tr(STR_ENV_HUMIDITY), val);

  std::snprintf(val, sizeof(val), "%.1f\xC2\xB0" "C", data_.apparentC);
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, col2, y, tr(STR_APPARENT_TEMP), val);

  y += rowStep;
  const char* wind = WeatherService::windDirLabel(data_.windDirDeg);
  std::snprintf(val, sizeof(val), "%s %dkm/h", wind && wind[0] ? wind : "",
                static_cast<int>(data_.windSpeedKmh + 0.5f));
  renderer.drawText(NOTOSANS_12_FONT_ID, left, y, val, true);

  std::snprintf(val, sizeof(val), "%s", data_.sunrise);
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, col2, y, tr(STR_SUNRISE), val);

  y += rowStep;
  std::snprintf(val, sizeof(val), "%s", data_.sunset);
  drawLabelValue(renderer, NOTOSANS_12_FONT_ID, left, y, tr(STR_SUNSET), val);

  y += rowStep;
  renderer.drawLine(left, y, right, y, true);

  // --- Forecast: vertical list, one row per day, filling down to the bottom ---
  y += 14;
  const int days = data_.dayCount > 4 ? 4 : data_.dayCount;
  if (days > 0) {
    const int listTop = y;
    const int listBottom = viewport.y + vh - 12;
    const int rowH = (listBottom - listTop) / days;
    const int glyphR = 10;
    const int weekdayW = renderer.getTextWidth(SMALL_FONT_ID, "星期三");
    const int glyphCx = left + weekdayW + 30 + glyphR;
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
      renderer.drawText(SMALL_FONT_ID, left, rowMid - smallLh / 2, dlabel, true);
      WeatherGlyph::draw(renderer, data_.days[i].code, glyphCx, rowMid, glyphR);
      const char* desc = WeatherService::describe(data_.days[i].code);
      renderer.drawText(NOTOSANS_12_FONT_ID, descX, rowMid - bodyLh / 2, desc, true);
      char range[32];
      std::snprintf(range, sizeof(range), "%d~%d\xC2\xB0" "C", static_cast<int>(data_.days[i].minC + 0.5f),
                    static_cast<int>(data_.days[i].maxC + 0.5f));
      const int rangeW = renderer.getTextWidth(NOTOSANS_12_FONT_ID, range);
      renderer.drawText(NOTOSANS_12_FONT_ID, right - rangeW, rowMid - bodyLh / 2, range, true);
      if (i + 1 < days) renderer.drawLine(left, rowTop + rowH, right, rowTop + rowH, true);
    }
  }
}

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
