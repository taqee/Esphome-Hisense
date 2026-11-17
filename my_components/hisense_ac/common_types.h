#pragma once

#include <cstdint>

namespace esphome {
namespace hisense_ac {

// Sleep values as reported in the status payload. Command value = status / 2.
enum class SleepMode : uint8_t {
  SLEEP_OFF = 0,
  SLEEP_GENERAL = 2,
  SLEEP_OLD = 4,
  SLEEP_YOUNG = 6,
  SLEEP_KIDS = 8,
};

inline bool is_valid_sleep(uint8_t raw) { return raw <= 8 && (raw % 2) == 0; }

// Fan levels as reported in the status payload.
enum class FanLevel : uint8_t {
  LEVEL_OFF = 0,
  LEVEL_AUTO = 1,
  LEVEL_QUIET = 2,
  LEVEL_LOW = 10,
  LEVEL_MED_LOW = 12,
  LEVEL_MEDIUM = 14,
  LEVEL_MED_HIGH = 16,
  LEVEL_HIGH = 18,
};

// Everything we read back from the AC that matters for control.
struct AcStatus {
  bool power{false};
  uint8_t mode_raw{0};     // 0 fan, 1 heat, 2 cool, 3 dry, 4..7 auto
  uint8_t fan_raw{1};      // FanLevel
  SleepMode sleep{SleepMode::SLEEP_OFF};
  uint8_t direction{0};
  uint8_t target_temp{25};
  int16_t current_temp{25};
  bool swing_up_down{false};
  bool swing_left_right{false};
  bool eco{false};
  bool boost{false};
  bool quiet{false};
  bool display{true};
};

// Preset names shown in the Home Assistant climate card.
static constexpr const char *PRESET_QUIET = "Quiet";

// Extra fan speeds shown in the card next to Auto/Low/Medium/High.
static constexpr const char *FAN_QUIET = "Quiet";
static constexpr const char *FAN_MED_LOW = "Med-Low";
static constexpr const char *FAN_MED_HIGH = "Med-High";

}  // namespace hisense_ac
}  // namespace esphome
