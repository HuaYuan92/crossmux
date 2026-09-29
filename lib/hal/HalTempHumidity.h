#pragma once

#include <cstdint>

// Fork-local SHTC3 (temperature + humidity) reader for the Waveshare ESP32-S3
// ePaper 3.97.
//
// The upstream SDK's EnvironmentSensor is written for the Sensirion SHT40
// (8-bit commands, SHT40 RH formula, addr 0x44) and is gated off for this
// board, so it cannot be reused for the SHTC3 (16-bit commands, addr 0x70).
// This wrapper talks to the SHTC3 directly through Wire, mirroring
// Waveshare397Power on the same shared I2C bus (SDA/SCL come from
// BoardConfig::ACTIVE.sensors; the bus also hosts the RTC, audio codec and the
// AXP2101 power IC). On devices without the sensor the .cpp provides stub
// bodies that report unavailable, so the shared X3/X4 image links unchanged.
namespace HalTempHumidity {

// Initialize the shared Wire bus and probe the SHTC3. Idempotent.
// Returns true once a device has acknowledged on the bus.
bool begin();

// True after begin() has confirmed a present SHTC3.
bool available();

// One-shot wakeup -> measure (T+RH, polling mode) -> read -> sleep. Returns
// false on an I2C or CRC error. tempC is degrees Celsius; humidityPct is
// relative humidity clamped to 0..100.
bool read(float& tempC, float& humidityPct);

}  // namespace HalTempHumidity
