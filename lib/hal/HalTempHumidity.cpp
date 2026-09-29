#include "HalTempHumidity.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <Logging.h>
#include <Wire.h>

namespace HalTempHumidity {
namespace {

// SHTC3 sits on the shared sensor I2C bus at 0x70 (waveshare user_config.h).
constexpr uint8_t SHTC3_ADDR = 0x70;

// SHTC3 commands are 16-bit, sent MSB first (datasheet / waveshare example).
constexpr uint16_t CMD_WAKEUP = 0x3517;
constexpr uint16_t CMD_SLEEP = 0xB098;
// Measure T+RH, read T first, clock stretching DISABLED -> poll after the
// conversion time instead of relying on the master honouring clock stretch.
constexpr uint16_t CMD_MEAS_T_RH_POLLING = 0x7866;
constexpr uint16_t CMD_READ_ID = 0xEFC8;

constexpr uint32_t WAKEUP_DELAY_MS = 1;   // datasheet wakeup time ~0.3 ms
constexpr uint32_t MEASURE_DELAY_MS = 15; // > datasheet max ~7.5 ms

bool g_ready = false;

bool sendCommand(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(static_cast<uint8_t>(cmd >> 8));
  Wire.write(static_cast<uint8_t>(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

// Sensirion CRC-8: polynomial 0x31, init 0xFF, no final XOR (same as SHT4x).
uint8_t crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace

bool begin() {
  if (g_ready) return true;
  const auto& s = BoardConfig::ACTIVE.sensors;
  if (s.i2cSda < 0 || s.i2cScl < 0 || s.i2cHz == 0) return false;
  Wire.begin(s.i2cSda, s.i2cScl, s.i2cHz);

  // Probe: wake the part and read the 16-bit device ID. A missing sensor never
  // ACKs, so a failed transaction here cleanly reports "absent".
  if (!sendCommand(CMD_WAKEUP)) return false;
  delay(WAKEUP_DELAY_MS);
  if (!sendCommand(CMD_READ_ID)) return false;
  uint8_t id[3] = {0};
  if (Wire.requestFrom(SHTC3_ADDR, static_cast<uint8_t>(3), static_cast<uint8_t>(true)) != 3) return false;
  for (uint8_t i = 0; i < 3; ++i) id[i] = Wire.read();
  if (crc8(id, 2) != id[2]) return false;

  g_ready = true;
  LOG_INF("ENV", "SHTC3 ready (id=0x%02X%02X) on addr 0x%02X SDA=%d SCL=%d", id[0], id[1], SHTC3_ADDR, s.i2cSda,
          s.i2cScl);
  return true;
}

bool available() { return g_ready; }

bool read(float& tempC, float& humidityPct) {
  if (!g_ready && !begin()) return false;

  if (!sendCommand(CMD_WAKEUP)) return false;
  delay(WAKEUP_DELAY_MS);
  if (!sendCommand(CMD_MEAS_T_RH_POLLING)) return false;
  delay(MEASURE_DELAY_MS);

  uint8_t b[6] = {0};
  if (Wire.requestFrom(SHTC3_ADDR, static_cast<uint8_t>(6), static_cast<uint8_t>(true)) != 6) return false;
  for (uint8_t i = 0; i < 6; ++i) b[i] = Wire.read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;

  // Return to low-power sleep between (infrequent) standby refreshes.
  sendCommand(CMD_SLEEP);

  const uint16_t tRaw = static_cast<uint16_t>((b[0] << 8) | b[1]);
  const uint16_t rhRaw = static_cast<uint16_t>((b[3] << 8) | b[4]);
  tempC = -45.0f + 175.0f * (static_cast<float>(tRaw) / 65535.0f);
  float rh = 100.0f * (static_cast<float>(rhRaw) / 65535.0f);
  if (rh < 0.0f) rh = 0.0f;
  if (rh > 100.0f) rh = 100.0f;
  humidityPct = rh;
  return true;
}

}  // namespace HalTempHumidity

#else  // Device without an SHTC3: link-compatible stubs.

namespace HalTempHumidity {
bool begin() { return false; }
bool available() { return false; }
bool read(float&, float&) { return false; }
}  // namespace HalTempHumidity

#endif
