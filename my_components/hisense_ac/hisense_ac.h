#pragma once

#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#include "common_types.h"
#include "command_sender.h"
#include "protocol_decoder.h"

namespace esphome {
namespace hisense_ac {

class HisenseAC : public uart::UARTDevice, public Component, public climate::Climate {
 public:
  HisenseAC() : sender_(this) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }

  // Used by the switches
  void set_display(bool on);
  void set_beep(bool on) { this->beep_ = on; }
#ifdef USE_SWITCH
  void set_display_switch(switch_::Switch *s) { this->display_switch_ = s; }
  void set_beep_switch(switch_::Switch *s) { this->beep_switch_ = s; }
#endif

  // Used by CommandSender
  void write_frame(const uint8_t *data, size_t len);

 protected:
  enum class Step : uint8_t { INIT_1, INIT_2, INIT_3, WIFI_STATUS, POLLING };

  struct Preset {
    bool eco{false};
    bool boost{false};
    bool quiet{false};
    SleepMode sleep{SleepMode::SLEEP_OFF};
  };

  void read_uart_();
  void handle_frame_();
  void run_step_();
  void reset_link_(const char *reason);
  void apply_status_();
  void log_payload_changes_();
  void apply_preset_(const Preset &want);
  static Preset preset_from_status_(const AcStatus &s);

  CommandSender sender_;
  GPIOPin *flow_control_pin_{nullptr};
#ifdef USE_SWITCH
  switch_::Switch *display_switch_{nullptr};
  switch_::Switch *beep_switch_{nullptr};
#endif

  std::vector<uint8_t> rx_;
  uint32_t last_rx_byte_{0};
  uint32_t last_step_{0};
  uint32_t last_valid_{0};
  uint8_t bad_frames_{0};
  Step step_{Step::INIT_1};
  bool linked_{false};        // init sequence done
  bool status_known_{false};  // at least one status decoded
  bool beep_{true};
  // After we send a command, ignore the next status so the card does not jump back
  // to the old values before the AC has applied the change.
  uint8_t skip_status_{0};

  AcStatus status_{};

  // Last temperature / fan used while the AC was running. Sent again on power-on so the
  // AC does not fall back to its own default (26°C). Kept in flash across reboots.
  struct LastOn {
    uint8_t target_temp{0};
    uint8_t fan_raw{0};
  } last_on_{};
  ESPPreferenceObject last_on_pref_;
  void remember_last_on_(uint8_t target, uint8_t fan_raw);
  static CommandSender::CmdFan cmd_fan_from_level_(uint8_t fan_raw);
  std::vector<uint8_t> last_payload_;  // for the bit-change log (DEBUG level)
};

#ifdef USE_SWITCH
class HisenseDisplaySwitch : public switch_::Switch, public Parented<HisenseAC> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_display(state);
    this->publish_state(state);
  }
};

class HisenseBeepSwitch : public switch_::Switch, public Parented<HisenseAC> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_beep(state);
    this->publish_state(state);
  }
};
#endif

}  // namespace hisense_ac
}  // namespace esphome
