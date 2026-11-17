#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "esphome/components/climate/climate_mode.h"
#include "common_types.h"

namespace esphome {
namespace hisense_ac {

class HisenseAC;

// Builds the 30-byte command payload field by field, then sends it as one frame.
// Every field carries a "changed" control bit, so untouched fields are left alone by the AC.
class CommandSender {
 public:
  explicit CommandSender(HisenseAC *parent) : parent_(parent) { this->clear(); }

  // Fan values used in commands (status value / 2, auto = 0).
  enum class CmdFan : uint8_t { AUTO = 0, QUIET = 1, LOW = 5, MED_LOW = 6, MEDIUM = 7, MED_HIGH = 8, HIGH = 9 };

  void set_power(bool on);
  void set_mode(climate::ClimateMode mode);
  void set_temperature(int temperature);
  void set_fan(CmdFan fan);
  void set_swing(climate::ClimateSwingMode swing);
  void set_sleep(SleepMode mode);
  void set_eco(bool on);
  void set_boost(bool on);
  void set_quiet(bool on);
  void set_display(bool on);

  bool has_changes() const { return this->dirty_; }
  void clear();
  void send_command(bool beep);

  void send_init_1();
  void send_init_2();
  void send_init_3();
  void send_wifi_status();
  void send_status_request();

 private:
  struct Field {
    uint16_t offset;
    uint8_t size;
  };
  static constexpr Field FAN = {0, 8};
  static constexpr Field SLEEP = {8, 8};
  static constexpr Field MODE = {16, 4};
  static constexpr Field POWER = {20, 2};
  static constexpr Field TEMPERATURE = {24, 8};
  static constexpr uint16_t BEEP_BIT = 61;
  static constexpr Field SWING_UP_DOWN = {128, 2};
  static constexpr Field SWING_LEFT_RIGHT = {130, 2};
  static constexpr Field ECO = {138, 2};
  static constexpr Field BOOST = {140, 2};
  static constexpr Field QUIET = {154, 2};
  static constexpr Field DISPLAY = {160, 2};

  void set_bit_(uint16_t bit, bool value);
  void set_field_(Field f, uint32_t value);
  void send_raw_(const uint8_t *data, size_t len);

  HisenseAC *parent_;
  std::array<uint8_t, 30> payload_{};
  bool dirty_{false};
};

}  // namespace hisense_ac
}  // namespace esphome
