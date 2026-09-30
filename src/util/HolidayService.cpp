#include "HolidayService.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "network/HttpDownloader.h"
#include "util/TimeUtils.h"

namespace {

constexpr int kCacheVersion = 1;

// Key-free, HTTPS dataset (verified against live responses): one JSON per year
// listing every statutory rest day and weekend makeup-workday.
constexpr const char* kUrlFormat = "https://raw.githubusercontent.com/NateScarlet/holiday-cn/master/%d.json";

void cachePath(int year, char* out, size_t outSz) { std::snprintf(out, outSz, "/.crosspoint/holidays_%d.json", year); }

// holiday-cn dates are "YYYY-MM-DD"; pull the month and day fields directly.
bool parseMonthDay(const char* date, uint8_t& month, uint8_t& day) {
  if (!date || std::strlen(date) < 10) return false;
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!digit(date[5]) || !digit(date[6]) || !digit(date[8]) || !digit(date[9])) return false;
  month = static_cast<uint8_t>((date[5] - '0') * 10 + (date[6] - '0'));
  day = static_cast<uint8_t>((date[8] - '0') * 10 + (date[9] - '0'));
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

}  // namespace

namespace HolidayService {

bool loadYearFromCache(int year, YearData& out) {
  char path[48];
  cachePath(year, path, sizeof(path));
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path, doc)) return false;
  if ((doc["v"] | 0) != kCacheVersion) {
    LOG_INF("HOL", "cache version mismatch, ignoring");
    return false;
  }
  if ((doc["year"] | 0) != year) return false;
  out = YearData{};
  out.year = year;
  out.fetchedEpoch = doc["fetched"] | static_cast<int64_t>(0);
  JsonArray days = doc["days"].as<JsonArray>();
  for (JsonVariant v : days) {
    if (out.count >= kMaxEntries) break;
    const uint8_t m = v[0].as<uint8_t>();
    const uint8_t d = v[1].as<uint8_t>();
    out.entries[out.count] = {m, d, v[2].as<bool>()};
    out.count = static_cast<uint8_t>(out.count + 1);
  }
  return true;
}

bool refreshYear(int year, YearData& out) {
  char url[128];
  const int urlLen = std::snprintf(url, sizeof(url), kUrlFormat, year);
  if (urlLen < 0 || static_cast<size_t>(urlLen) >= sizeof(url)) {
    LOG_ERR("HOL", "URL truncated");
    return false;
  }
  std::string body;
  if (!HttpDownloader::fetchUrl(std::string(url), body) || body.empty()) {
    LOG_ERR("HOL", "fetch failed: %s", url);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("HOL", "JSON parse error");
    return false;
  }
  if ((doc["year"] | 0) != year) {
    LOG_ERR("HOL", "year mismatch in payload");
    return false;
  }
  JsonArray days = doc["days"].as<JsonArray>();
  if (days.isNull()) {
    LOG_ERR("HOL", "missing 'days'");
    return false;
  }

  YearData fresh;
  fresh.year = year;
  fresh.fetchedEpoch = static_cast<int64_t>(TimeUtils::getCurrentValidTimestamp());
  for (JsonObject entry : days) {
    if (fresh.count >= kMaxEntries) break;
    uint8_t m = 0;
    uint8_t d = 0;
    if (!parseMonthDay(entry["date"] | "", m, d)) continue;
    fresh.entries[fresh.count] = {m, d, entry["isOffDay"] | false};
    fresh.count = static_cast<uint8_t>(fresh.count + 1);
  }

  char path[48];
  cachePath(year, path, sizeof(path));
  JsonDocument cache;
  cache["v"] = kCacheVersion;
  cache["year"] = year;
  cache["fetched"] = fresh.fetchedEpoch;
  JsonArray arr = cache["days"].to<JsonArray>();
  for (uint8_t i = 0; i < fresh.count; ++i) {
    JsonArray row = arr.add<JsonArray>();
    row.add(fresh.entries[i].month);
    row.add(fresh.entries[i].day);
    row.add(fresh.entries[i].off);
  }
  if (!PersistableStoreBase::writeDocToFile(path, cache)) {
    LOG_ERR("HOL", "cache write failed (still returning fresh data)");
  }
  out = fresh;
  return true;
}

calendar::Badge lookupBadge(int year, int month, int day, void* ctx) {
  if (!ctx) return calendar::Badge::None;
  const auto* data = static_cast<const YearData*>(ctx);
  if (data->year != year) return calendar::Badge::None;
  for (uint8_t i = 0; i < data->count; ++i) {
    if (data->entries[i].month == month && data->entries[i].day == day) {
      return data->entries[i].off ? calendar::Badge::Off : calendar::Badge::Workday;
    }
  }
  return calendar::Badge::None;
}

}  // namespace HolidayService
