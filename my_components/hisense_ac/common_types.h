#pragma once

#include <cstdint>

namespace esphome {
namespace hisense_ac {

// Sleep values as reported in the status payload. Command value = status / 2.
enum class SleepMode : uint8_t {
  OFF = 0,
  GENERAL = 2,
  OLD = 4,
  YOUNG = 6,
  KIDS = 8,
};

inline bool is_valid_sleep(uint8_t raw) { return raw <= 8 && (raw % 2) == 0; }

// Fan levels as reported in the status payload.
enum class FanLevel : uint8_t {
  OFF = 0,
  AUTO = 1,
  QUIET = 2,
  LOW = 10,
  MED_LOW = 12,
  MEDIUM = 14,
  MED_HIGH = 16,
  HIGH = 18,
};

// Everything we read back from the AC that matters for control.
struct AcStatus {
  bool power{false};
  uint8_t mode_raw{0};     // 0 fan, 1 heat, 2 cool, 3 dry, 4..7 auto
  uint8_t fan_raw{1};      // FanLevel
  SleepMode sleep{SleepMode::OFF};
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
static constexpr const char *PRESET_SLEEP_OLD = "Sleep Old";
static constexpr const char *PRESET_SLEEP_YOUNG = "Sleep Young";
static constexpr const char *PRESET_SLEEP_KIDS = "Sleep Kids";

// Extra fan speeds shown in the card next to Auto/Low/Medium/High.
static constexpr const char *FAN_QUIET = "Quiet";
static constexpr const char *FAN_MED_LOW = "Med-Low";
static constexpr const char *FAN_MED_HIGH = "Med-High";

}  // namespace hisense_ac
}  // namespace esphome
