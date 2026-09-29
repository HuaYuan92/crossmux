#pragma once

#include <cstdint>

// Shared weather data model + cache for the Waveshare build's weather surfaces
// (standby face and the standalone Weather app). Network access is assumed to be
// already established by the caller: this service never touches WiFi itself, it
// only speaks HTTP over a connected interface and reads/writes the SD cache.
//
// A single fixed-layout struct (no std::string / std::vector members) keeps the
// footprint predictable and heap-free, honouring the C3 shared-code baseline.
struct WeatherData {
  bool valid = false;
  int64_t fetchedEpoch = 0;  // UTC seconds at last successful refresh; 0 = unknown

  double latitude = 0.0;
  double longitude = 0.0;
  char city[32] = {0};  // resolved location label (city from IP geolocation or manual)

  // Current conditions.
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
  float apparentC = 0.0f;
  int weatherCode = -1;  // WMO weather interpretation code (see describe())
  float windSpeedKmh = 0.0f;
  int windDirDeg = -1;  // meteorological wind direction (degrees from north)
  char sunrise[8] = {0};  // "HH:MM" local, today
  char sunset[8] = {0};   // "HH:MM" local, today

  // Up to 4 daily forecast entries.
  struct Day {
    int code = -1;
    float maxC = 0.0f;
    float minC = 0.0f;
  };
  Day days[4];
  uint8_t dayCount = 0;
};

namespace WeatherService {

// Read the SD cache (/.crosspoint/weather.json). Returns false if absent or
// unreadable; never performs network I/O. Safe to call from a render path.
bool loadCache(WeatherData& out);

// Write the SD cache. Ensures /.crosspoint exists. Returns true on success.
bool writeCache(const WeatherData& w);

// Fetch fresh data over an already-connected network: optionally resolve the
// location via IP geolocation, then query Open-Meteo, then persist the cache.
// If useManual is true, the supplied manualLat/Lon/City are used and no IP
// lookup is performed. On any network/parse failure returns false and leaves
// `out` and the on-disk cache untouched.
bool refresh(WeatherData& out, bool useManual, double manualLat, double manualLon, const char* manualCity);

// Human-readable Chinese label for a WMO weather code (via tr()).
const char* describe(int wmoCode);

// Compass wind direction (degrees from north) as an 8-point Chinese label
// (北/东北/…), rendered as "东北风" style via tr(). Returns "" for invalid input.
const char* windDirLabel(int degrees);

// True when the cached reading should be considered out of date given maxAgeSec.
// Treats an unknown fetch time (fetchedEpoch == 0) as stale so a live clock or
// network will re-fetch.
bool isStale(const WeatherData& w, uint32_t maxAgeSec);

}  // namespace WeatherService
