#include "WeatherService.h"

#include <ArduinoJson.h>
#include <I18n.h>
#include <Logging.h>
#include <PersistableStore.h>

#include "network/HttpDownloader.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "I18nKeys.h"
#include "util/TimeUtils.h"

namespace {

// Versioned so a future layout change can invalidate old caches instead of
// silently mis-parsing them. Bump before changing any field meaning below.
constexpr int kCacheVersion = 2;
constexpr const char* kCachePath = "/.crosspoint/weather.json";

// Key-free, HTTPS endpoints (both verified against live responses).
constexpr const char* kGeoUrl = "https://ipwho.is/";
// Open-Meteo forecast: current + 4 daily entries (incl. wind & sun times), tz auto.
#define WEATHER_URL_FORMAT                                                          \
  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"             \
  "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code," \
  "wind_speed_10m,wind_direction_10m"                                               \
  "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"       \
  "&timezone=auto&forecast_days=4"

// Open-Meteo returns daily sunrise/sunset as ISO8601 local strings
// ("2026-01-22T07:05"); copy just the "HH:MM" part into a 8-byte buffer.
void copyTimeOfDay(const char* iso, char* out, size_t outSz) {
  out[0] = '\0';
  if (!iso) return;
  const char* t = std::strchr(iso, 'T');
  if (!t || std::strlen(t + 1) < 5) return;
  std::strncpy(out, t + 1, 5);
  out[5] = '\0';
}

// Fetch a URL body into `out`. Kept small (weather/geo payloads are <1KB), so a
// transient std::string buffer is acceptable on the C3 baseline.
bool fetchBody(const char* url, std::string& out) {
  out.clear();
  if (!HttpDownloader::fetchUrl(std::string(url), out)) {
    LOG_ERR("WX", "fetch failed: %s", url);
    return false;
  }
  return !out.empty();
}

// Resolve current location via IP geolocation. On success fills lat/lon/city.
bool resolveLocationByIp(double& lat, double& lon, char* city, size_t citySz) {
  std::string body;
  if (!fetchBody(kGeoUrl, body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("WX", "geo JSON parse error");
    return false;
  }
  if (!(doc["success"] | false)) {
    LOG_ERR("WX", "geo lookup not successful");
    return false;
  }
  lat = doc["latitude"] | 0.0;
  lon = doc["longitude"] | 0.0;
  const char* c = doc["city"] | "";
  std::strncpy(city, c, citySz - 1);
  city[citySz - 1] = '\0';
  return true;
}

// Query Open-Meteo for the given coordinates into `w`. Network only.
bool fetchWeather(double lat, double lon, WeatherData& w) {
  // Stack buffer, sized for the worst case: the format string is ~288 bytes with
  // "+/-ddd.dddd" coordinates. snprintf silently truncates, and a cut-over query
  // string makes Open-Meteo reject an incomplete variable name (HTTP 400), so the
  // written length is checked instead of trusted.
  char url[384];
  const int urlLen = std::snprintf(url, sizeof(url), WEATHER_URL_FORMAT, lat, lon);
  if (urlLen < 0 || static_cast<size_t>(urlLen) >= sizeof(url)) {
    LOG_ERR("WX", "forecast URL truncated (need %d, have %u)", urlLen, static_cast<unsigned>(sizeof(url)));
    return false;
  }
  std::string body;
  if (!fetchBody(url, body)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("WX", "weather JSON parse error");
    return false;
  }

  JsonObject cur = doc["current"];
  if (cur.isNull()) {
    LOG_ERR("WX", "weather JSON missing 'current'");
    return false;
  }
  w.latitude = lat;
  w.longitude = lon;
  w.temperatureC = cur["temperature_2m"] | 0.0f;
  w.humidityPct = cur["relative_humidity_2m"] | 0.0f;
  w.apparentC = cur["apparent_temperature"] | 0.0f;
  w.weatherCode = cur["weather_code"] | -1;
  w.windSpeedKmh = cur["wind_speed_10m"] | 0.0f;
  w.windDirDeg = cur["wind_direction_10m"] | -1;

  JsonObject daily = doc["daily"];
  w.dayCount = 0;
  if (!daily.isNull()) {
    JsonArray codes = daily["weather_code"].as<JsonArray>();
    JsonArray maxes = daily["temperature_2m_max"].as<JsonArray>();
    JsonArray mins = daily["temperature_2m_min"].as<JsonArray>();
    for (size_t i = 0; i < codes.size() && i < 4; ++i) {
      w.days[i].code = codes[i].as<int>();
      w.days[i].maxC = (i < maxes.size()) ? static_cast<float>(maxes[i]) : 0.0f;
      w.days[i].minC = (i < mins.size()) ? static_cast<float>(mins[i]) : 0.0f;
      w.dayCount = static_cast<uint8_t>(i + 1);
    }
    JsonArray sunrises = daily["sunrise"].as<JsonArray>();
    JsonArray sunsets = daily["sunset"].as<JsonArray>();
    if (sunrises.size() > 0) copyTimeOfDay(sunrises[0].as<const char*>(), w.sunrise, sizeof(w.sunrise));
    if (sunsets.size() > 0) copyTimeOfDay(sunsets[0].as<const char*>(), w.sunset, sizeof(w.sunset));
  }
  return true;
}

}  // namespace

namespace WeatherService {

bool loadCache(WeatherData& out) {
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(kCachePath, doc)) return false;
  if ((doc["v"] | 0) != kCacheVersion) {
    LOG_INF("WX", "cache version mismatch, ignoring");
    return false;
  }
  out = WeatherData{};
  out.valid = true;
  out.fetchedEpoch = doc["fetched"] | static_cast<int64_t>(0);
  out.latitude = doc["lat"] | 0.0;
  out.longitude = doc["lon"] | 0.0;
  const char* c = doc["city"] | "";
  std::strncpy(out.city, c, sizeof(out.city) - 1);
  out.city[sizeof(out.city) - 1] = '\0';
  out.temperatureC = doc["t"] | 0.0f;
  out.humidityPct = doc["rh"] | 0.0f;
  out.apparentC = doc["app"] | 0.0f;
  out.weatherCode = doc["code"] | -1;
  out.windSpeedKmh = doc["ws"] | 0.0f;
  out.windDirDeg = doc["wd"] | -1;
  const char* sr = doc["sr"] | "";
  const char* ss = doc["ss"] | "";
  std::strncpy(out.sunrise, sr, sizeof(out.sunrise) - 1);
  out.sunrise[sizeof(out.sunrise) - 1] = '\0';
  std::strncpy(out.sunset, ss, sizeof(out.sunset) - 1);
  out.sunset[sizeof(out.sunset) - 1] = '\0';

  JsonArray codes = doc["dc"].as<JsonArray>();
  JsonArray maxes = doc["dmax"].as<JsonArray>();
  JsonArray mins = doc["dmin"].as<JsonArray>();
  for (size_t i = 0; i < codes.size() && i < 4; ++i) {
    out.days[i].code = codes[i].as<int>();
    out.days[i].maxC = (i < maxes.size()) ? static_cast<float>(maxes[i]) : 0.0f;
    out.days[i].minC = (i < mins.size()) ? static_cast<float>(mins[i]) : 0.0f;
    out.dayCount = static_cast<uint8_t>(i + 1);
  }
  return true;
}

bool writeCache(const WeatherData& w) {
  JsonDocument doc;
  doc["v"] = kCacheVersion;
  doc["fetched"] = w.fetchedEpoch;
  doc["lat"] = w.latitude;
  doc["lon"] = w.longitude;
  doc["city"] = w.city;
  doc["t"] = w.temperatureC;
  doc["rh"] = w.humidityPct;
  doc["app"] = w.apparentC;
  doc["code"] = w.weatherCode;
  doc["ws"] = w.windSpeedKmh;
  doc["wd"] = w.windDirDeg;
  doc["sr"] = w.sunrise;
  doc["ss"] = w.sunset;
  JsonArray codes = doc["dc"].to<JsonArray>();
  JsonArray maxes = doc["dmax"].to<JsonArray>();
  JsonArray mins = doc["dmin"].to<JsonArray>();
  for (uint8_t i = 0; i < w.dayCount; ++i) {
    codes.add(w.days[i].code);
    maxes.add(w.days[i].maxC);
    mins.add(w.days[i].minC);
  }
  return PersistableStoreBase::writeDocToFile(kCachePath, doc);
}

bool refresh(WeatherData& out, bool useManual, double manualLat, double manualLon, const char* manualCity) {
  double lat = manualLat;
  double lon = manualLon;
  char city[32] = {0};

  if (useManual) {
    std::strncpy(city, manualCity ? manualCity : "", sizeof(city) - 1);
  } else {
    if (!resolveLocationByIp(lat, lon, city, sizeof(city))) {
      LOG_ERR("WX", "IP geolocation failed");
      return false;
    }
  }

  WeatherData fresh;
  if (!fetchWeather(lat, lon, fresh)) {
    LOG_ERR("WX", "weather fetch failed");
    return false;
  }
  std::memcpy(fresh.city, city, sizeof(city));
  fresh.fetchedEpoch = static_cast<int64_t>(TimeUtils::getCurrentValidTimestamp());
  // A completed parse is what makes this snapshot trustworthy; loadCache() sets the
  // same flag, so leaving it clear would render a valid fetch as "no data".
  fresh.valid = true;

  if (!writeCache(fresh)) {
    LOG_ERR("WX", "cache write failed (still returning fresh data)");
  }
  out = fresh;
  return true;
}

const char* describe(int wmoCode) {
  switch (wmoCode) {
    case 0:
      return tr(STR_WMO_CLEAR);
    case 1:
    case 2:
      return tr(STR_WMO_PARTLY_CLOUDY);
    case 3:
      return tr(STR_WMO_OVERCAST);
    case 45:
    case 48:
      return tr(STR_WMO_FOG);
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return tr(STR_WMO_DRIZZLE);
    case 61:
    case 63:
    case 66:
    case 67:
      return tr(STR_WMO_RAIN);
    case 65:
      return tr(STR_WMO_RAIN_HEAVY);
    case 71:
    case 73:
    case 77:
      return tr(STR_WMO_SNOW);
    case 75:
    case 85:
    case 86:
      return tr(STR_WMO_SNOW_HEAVY);
    case 80:
    case 81:
    case 82:
      return tr(STR_WMO_SHOWERS);
    case 95:
    case 96:
    case 99:
      return tr(STR_WMO_THUNDER);
    default:
      return tr(STR_WMO_UNKNOWN);
  }
}

const char* windDirLabel(int degrees) {
  if (degrees < 0 || degrees >= 360) return "";
  // Meteorological direction is where the wind comes FROM. Bucket the compass
  // into 8 sectors, each 45deg centred on a cardinal/intercardinal point.
  const int sector = ((degrees + 22) / 45) % 8;  // 0=N,1=NE,2=E,...,7=NW
  switch (sector) {
    case 0:
      return tr(STR_WIND_NORTH);
    case 1:
      return tr(STR_WIND_NORTHEAST);
    case 2:
      return tr(STR_WIND_EAST);
    case 3:
      return tr(STR_WIND_SOUTHEAST);
    case 4:
      return tr(STR_WIND_SOUTH);
    case 5:
      return tr(STR_WIND_SOUTHWEST);
    case 6:
      return tr(STR_WIND_WEST);
    default:
      return tr(STR_WIND_NORTHWEST);
  }
}

bool isStale(const WeatherData& w, uint32_t maxAgeSec) {
  if (!w.valid || w.fetchedEpoch == 0) return true;
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (now == 0) return true;  // no trustworthy clock: prefer re-fetch when online
  const int64_t age = static_cast<int64_t>(now) - w.fetchedEpoch;
  return age < 0 || static_cast<uint64_t>(age) > maxAgeSec;
}

}  // namespace WeatherService
