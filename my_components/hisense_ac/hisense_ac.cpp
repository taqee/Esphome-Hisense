#include "hisense_ac.h"
#include <cmath>
#include <cstring>
#include "esphome/core/log.h"

namespace esphome {
namespace hisense_ac {

static const char *const TAG = "hisense_ac";

static constexpr uint32_t STEP_INTERVAL_MS = 2000;     // normal pace of the polling loop
static constexpr uint32_t COMMAND_INTERVAL_MS = 1000;  // a pending command may jump the queue after this
static constexpr uint32_t RX_GAP_MS = 100;             // drop a half-received frame after this silence
static constexpr uint32_t LINK_TIMEOUT_MS = 30000;     // no valid frame for this long -> restart init
static constexpr uint8_t MAX_BAD_FRAMES = 5;

// ---------------------------------------------------------------- lifecycle

void HisenseAC::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);
  }

  this->set_supported_custom_fan_modes({FAN_QUIET, FAN_MED_LOW, FAN_MED_HIGH});
  this->set_supported_custom_presets({PRESET_QUIET});

  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 25;
  this->current_temperature = NAN;
  this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
  this->swing_mode = climate::CLIMATE_SWING_OFF;
  this->set_preset_(climate::CLIMATE_PRESET_NONE);

  this->rx_.reserve(proto::MAX_FRAME);
  this->last_step_ = millis();
  this->last_valid_ = millis();
}

void HisenseAC::dump_config() {
  ESP_LOGCONFIG(TAG, "Hisense AC");
  LOG_PIN("  Flow control pin: ", this->flow_control_pin_);
}

void HisenseAC::loop() {
  this->read_uart_();

  const uint32_t now = millis();
  if (this->linked_ && now - this->last_valid_ > LINK_TIMEOUT_MS) {
    this->reset_link_("no reply from AC");
    return;
  }

  const uint32_t wait = (this->linked_ && this->sender_.has_changes()) ? COMMAND_INTERVAL_MS : STEP_INTERVAL_MS;
  if (now - this->last_step_ >= wait) {
    this->last_step_ = now;
    this->run_step_();
  }
}

// ---------------------------------------------------------------- UART

void HisenseAC::write_frame(const uint8_t *data, size_t len) {
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);
  this->write_array(data, len);
  // Wait for the last byte to leave before switching the RS485 driver back to receive.
  this->flush();
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);
}

void HisenseAC::read_uart_() {
  const uint32_t now = millis();
  if (!this->rx_.empty() && now - this->last_rx_byte_ > RX_GAP_MS) {
    ESP_LOGV(TAG, "Dropping partial frame (%u bytes)", (unsigned) this->rx_.size());
    this->rx_.clear();
  }

  uint8_t byte;
  while (this->available() && this->read_byte(&byte)) {
    this->last_rx_byte_ = now;

    // Wait for F4 F5 at the start of a frame.
    if (this->rx_.empty() && byte != proto::HEADER_1)
      continue;
    if (this->rx_.size() == 1 && byte != proto::HEADER_2) {
      this->rx_.clear();
      if (byte == proto::HEADER_1)
        this->rx_.push_back(byte);
      continue;
    }
    this->rx_.push_back(byte);

    // Byte 4 is the length field: whole frame = length + 9.
    if (this->rx_.size() >= 5) {
      const size_t expected = static_cast<size_t>(this->rx_[4]) + proto::LENGTH_OVERHEAD;
      if (expected < proto::MIN_FRAME) {
        this->rx_.clear();
        continue;
      }
      if (this->rx_.size() == expected) {
        this->handle_frame_();
        this->rx_.clear();
      }
    }
  }
}

void HisenseAC::handle_frame_() {
  const auto frame = ProtocolDecoder::validate(this->rx_);
  if (!frame.valid) {
    ESP_LOGW(TAG, "Invalid frame from AC (%u bytes)", (unsigned) this->rx_.size());
    if (++this->bad_frames_ >= MAX_BAD_FRAMES)
      this->reset_link_("too many invalid frames");
    return;
  }
  this->bad_frames_ = 0;
  this->last_valid_ = millis();

  if (ProtocolDecoder::carries_status(frame) && this->linked_) {
    AcStatus decoded = this->status_;
    if (ProtocolDecoder::decode_status(this->rx_, decoded)) {
      this->status_ = decoded;
      this->status_known_ = true;
      if (this->skip_status_ > 0) {
        this->skip_status_--;
      } else {
        this->apply_status_();
      }
    }
  }

  // Each reply moves the handshake / polling loop forward.
  switch (this->step_) {
    case Step::INIT_1:
      this->step_ = Step::INIT_2;
      break;
    case Step::INIT_2:
      this->step_ = Step::INIT_3;
      break;
    case Step::INIT_3:
      this->step_ = Step::WIFI_STATUS;
      this->linked_ = true;
      ESP_LOGI(TAG, "Connected to AC");
      break;
    case Step::WIFI_STATUS:
      this->step_ = Step::POLLING;
      break;
    case Step::POLLING:
      this->step_ = Step::WIFI_STATUS;
      break;
  }
}

void HisenseAC::run_step_() {
  switch (this->step_) {
    case Step::INIT_1:
      this->sender_.send_init_1();
      return;
    case Step::INIT_2:
      this->sender_.send_init_2();
      return;
    case Step::INIT_3:
      this->sender_.send_init_3();
      return;
    case Step::WIFI_STATUS:
    case Step::POLLING:
      break;
  }

  // A user command takes the slot of the next wifi-status / poll message.
  if (this->sender_.has_changes()) {
    ESP_LOGD(TAG, "Sending command");
    this->sender_.send_command(this->beep_);
    this->skip_status_ = 1;
  } else if (this->step_ == Step::WIFI_STATUS) {
    this->sender_.send_wifi_status();
  } else {
    this->sender_.send_status_request();
  }
}

void HisenseAC::reset_link_(const char *reason) {
  ESP_LOGW(TAG, "Restarting AC handshake: %s", reason);
  this->step_ = Step::INIT_1;
  this->linked_ = false;
  this->bad_frames_ = 0;
  this->skip_status_ = 0;
  this->last_valid_ = millis();
  this->sender_.clear();
}

// ---------------------------------------------------------------- AC -> Home Assistant

HisenseAC::Preset HisenseAC::preset_from_status_(const AcStatus &s) {
  Preset p;
  p.eco = s.eco;
  p.boost = s.boost;
  p.quiet = s.quiet;
  p.sleep = s.sleep;
  return p;
}

void HisenseAC::apply_status_() {
  const AcStatus &s = this->status_;
  bool changed = false;

  // Mode
  climate::ClimateMode mode = climate::CLIMATE_MODE_OFF;
  if (s.power) {
    switch (s.mode_raw) {
      case 0:
        mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case 1:
        mode = climate::CLIMATE_MODE_HEAT;
        break;
      case 2:
        mode = climate::CLIMATE_MODE_COOL;
        break;
      case 3:
        mode = climate::CLIMATE_MODE_DRY;
        break;
      default:
        mode = climate::CLIMATE_MODE_AUTO;  // 4..7 = auto (with remote offsets)
        break;
    }
  }
  if (this->mode != mode) {
    this->mode = mode;
    changed = true;
  }

  // Temperatures
  if (this->target_temperature != s.target_temp) {
    this->target_temperature = s.target_temp;
    changed = true;
  }
  if (this->current_temperature != s.current_temp) {
    this->current_temperature = s.current_temp;
    changed = true;
  }

  // Fan
  switch (static_cast<FanLevel>(s.fan_raw)) {
    case FanLevel::QUIET:
      changed |= this->set_custom_fan_mode_(FAN_QUIET);
      break;
    case FanLevel::LOW:
      changed |= this->set_fan_mode_(climate::CLIMATE_FAN_LOW);
      break;
    case FanLevel::MED_LOW:
      changed |= this->set_custom_fan_mode_(FAN_MED_LOW);
      break;
    case FanLevel::MEDIUM:
      changed |= this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
      break;
    case FanLevel::MED_HIGH:
      changed |= this->set_custom_fan_mode_(FAN_MED_HIGH);
      break;
    case FanLevel::HIGH:
      changed |= this->set_fan_mode_(climate::CLIMATE_FAN_HIGH);
      break;
    default:
      changed |= this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
      break;
  }

  // Swing
  const bool vertical = s.direction == 2 && s.swing_up_down;
  const bool horizontal = s.swing_left_right;
  climate::ClimateSwingMode swing = climate::CLIMATE_SWING_OFF;
  if (vertical && horizontal) {
    swing = climate::CLIMATE_SWING_BOTH;
  } else if (vertical) {
    swing = climate::CLIMATE_SWING_VERTICAL;
  } else if (horizontal) {
    swing = climate::CLIMATE_SWING_HORIZONTAL;
  }
  if (this->swing_mode != swing) {
    this->swing_mode = swing;
    changed = true;
  }

  // Preset (one at a time in the card; boost > eco > quiet > sleep if the AC reports several)
  if (s.boost) {
    changed |= this->set_preset_(climate::CLIMATE_PRESET_BOOST);
  } else if (s.eco) {
    changed |= this->set_preset_(climate::CLIMATE_PRESET_ECO);
  } else if (s.quiet) {
    changed |= this->set_custom_preset_(PRESET_QUIET);
  } else if (s.sleep != SleepMode::OFF) {
    // Any sleep variant set from the remote shows as the standard Sleep preset.
    changed |= this->set_preset_(climate::CLIMATE_PRESET_SLEEP);
  } else {
    changed |= this->set_preset_(climate::CLIMATE_PRESET_NONE);
  }

#ifdef USE_SWITCH
  if (this->display_switch_ != nullptr &&
      (!this->display_switch_->has_state() || this->display_switch_->state != s.display))
    this->display_switch_->publish_state(s.display);
#endif

  if (changed)
    this->publish_state();
}

// ---------------------------------------------------------------- Home Assistant -> AC

climate::ClimateTraits HisenseAC::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
                              climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                                  climate::CLIMATE_FAN_HIGH});
  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                                    climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH});
  traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                                climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP});
  traits.set_visual_min_temperature(16);
  traits.set_visual_max_temperature(30);
  traits.set_visual_temperature_step(1);
  return traits;
}

void HisenseAC::control(const climate::ClimateCall &call) {
  if (!this->linked_) {
    ESP_LOGW(TAG, "AC not connected yet, ignoring command");
    this->publish_state();
    return;
  }

  // Mode / power
  if (call.get_mode().has_value()) {
    const auto mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      this->sender_.set_power(false);
      this->status_.power = false;
    } else {
      if (!this->status_.power) {
        this->sender_.set_power(true);
        this->status_.power = true;
      }
      this->sender_.set_mode(mode);
    }
    this->mode = mode;
  }

  // Target temperature
  if (call.get_target_temperature().has_value()) {
    int t = static_cast<int>(lroundf(*call.get_target_temperature()));
    t = t < 16 ? 16 : (t > 30 ? 30 : t);
    this->sender_.set_temperature(t);
    this->status_.target_temp = t;
    this->target_temperature = t;
  }

  // Fan
  if (call.get_fan_mode().has_value()) {
    const auto fan = *call.get_fan_mode();
    switch (fan) {
      case climate::CLIMATE_FAN_LOW:
        this->sender_.set_fan(CommandSender::CmdFan::LOW);
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        this->sender_.set_fan(CommandSender::CmdFan::MEDIUM);
        break;
      case climate::CLIMATE_FAN_HIGH:
        this->sender_.set_fan(CommandSender::CmdFan::HIGH);
        break;
      default:
        this->sender_.set_fan(CommandSender::CmdFan::AUTO);
        break;
    }
    this->set_fan_mode_(fan);
  } else if (call.has_custom_fan_mode()) {
    const StringRef fan = call.get_custom_fan_mode();
    if (fan == FAN_QUIET) {
      this->sender_.set_fan(CommandSender::CmdFan::QUIET);
    } else if (fan == FAN_MED_LOW) {
      this->sender_.set_fan(CommandSender::CmdFan::MED_LOW);
    } else if (fan == FAN_MED_HIGH) {
      this->sender_.set_fan(CommandSender::CmdFan::MED_HIGH);
    }
    this->set_custom_fan_mode_(fan);
  }

  // Swing
  if (call.get_swing_mode().has_value()) {
    this->swing_mode = *call.get_swing_mode();
    this->sender_.set_swing(this->swing_mode);
  }

  // Preset
  if (call.get_preset().has_value()) {
    const auto preset = *call.get_preset();
    Preset want;
    want.eco = preset == climate::CLIMATE_PRESET_ECO;
    want.boost = preset == climate::CLIMATE_PRESET_BOOST;
    want.sleep = preset == climate::CLIMATE_PRESET_SLEEP ? SleepMode::GENERAL : SleepMode::OFF;
    this->apply_preset_(want);
    this->set_preset_(preset);
  } else if (call.has_custom_preset()) {
    const StringRef preset = call.get_custom_preset();
    Preset want;
    want.quiet = preset == PRESET_QUIET;
    this->apply_preset_(want);
    this->set_custom_preset_(preset);
  }

  this->publish_state();
}

// Send only the special-mode bits that differ from what the AC currently has.
void HisenseAC::apply_preset_(const Preset &want) {
  const Preset have = preset_from_status_(this->status_);
  if (want.eco != have.eco) {
    this->sender_.set_eco(want.eco);
    this->status_.eco = want.eco;
  }
  if (want.boost != have.boost) {
    this->sender_.set_boost(want.boost);
    this->status_.boost = want.boost;
  }
  if (want.quiet != have.quiet) {
    this->sender_.set_quiet(want.quiet);
    this->status_.quiet = want.quiet;
  }
  // Any sleep variant already running counts as "Sleep", so it is not overwritten.
  const bool want_sleep = want.sleep != SleepMode::OFF;
  const bool have_sleep = have.sleep != SleepMode::OFF;
  if (want_sleep != have_sleep) {
    this->sender_.set_sleep(want.sleep);
    this->status_.sleep = want.sleep;
  }
}

void HisenseAC::set_display(bool on) {
  if (!this->linked_)
    return;
  this->sender_.set_display(on);
  this->status_.display = on;
}

}  // namespace hisense_ac
}  // namespace esphome
