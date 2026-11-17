#include "command_sender.h"
#include "hisense_ac.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace hisense_ac {

static const char *const TAG = "hisense_ac.sender";

static const uint8_t INIT_1[] = {0xF4, 0xF5, 0x00, 0x40, 0x0B, 0x00, 0x00, 0x00, 0x00, 0xFE,
                                 0x01, 0x00, 0x00, 0x0A, 0x04, 0x00, 0x01, 0x58, 0xF4, 0xFB};
static const uint8_t INIT_2[] = {0xF4, 0xF5, 0x00, 0x40, 0x0B, 0x00, 0x00, 0x01, 0x01, 0xFE,
                                 0x01, 0x00, 0x00, 0x07, 0x01, 0x00, 0x01, 0x54, 0xF4, 0xFB};
static const uint8_t INIT_3[] = {0xF4, 0xF5, 0x00, 0x40, 0x0B, 0x00, 0x00, 0x01, 0x01, 0xFE,
                                 0x01, 0x00, 0x00, 0x66, 0x40, 0x00, 0x01, 0xF2, 0xF4, 0xFB};
static const uint8_t WIFI_STATUS[] = {0xF4, 0xF5, 0x00, 0x40, 0x13, 0x00, 0x00, 0x01, 0x01, 0xFE,
                                      0x01, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x80, 0x80, 0x02, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x02, 0x74, 0xF4, 0xFB};
static const uint8_t STATUS_REQUEST[] = {0xF4, 0xF5, 0x00, 0x40, 0x0C, 0x00, 0x00, 0x01, 0x01, 0xFE, 0x01,
                                         0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x01, 0xB3, 0xF4, 0xFB};

void CommandSender::clear() {
  this->payload_.fill(0);
  this->dirty_ = false;
}

void CommandSender::set_bit_(uint16_t bit, bool value) {
  if (bit >= this->payload_.size() * 8)
    return;
  const uint8_t mask = 1 << (7 - (bit % 8));
  if (value) {
    this->payload_[bit / 8] |= mask;
  } else {
    this->payload_[bit / 8] &= ~mask;
  }
}

// Field value = (value << 1) | 1 — the low bit tells the AC "this field changed".
void CommandSender::set_field_(Field f, uint32_t value) {
  const uint32_t encoded = (value << 1) | 0x01;
  for (uint8_t i = 0; i < f.size; i++) {
    this->set_bit_(f.offset + (f.size - 1 - i), (encoded >> i) & 0x01);
  }
  this->dirty_ = true;
}

void CommandSender::set_power(bool on) { this->set_field_(F_POWER, on ? 1 : 0); }

void CommandSender::set_mode(climate::ClimateMode mode) {
  uint8_t v;
  switch (mode) {
    case climate::CLIMATE_MODE_FAN_ONLY:
      v = 0;
      break;
    case climate::CLIMATE_MODE_HEAT:
      v = 1;
      break;
    case climate::CLIMATE_MODE_COOL:
      v = 2;
      break;
    case climate::CLIMATE_MODE_DRY:
      v = 3;
      break;
    case climate::CLIMATE_MODE_AUTO:
    case climate::CLIMATE_MODE_HEAT_COOL:
      v = 4;
      break;
    default:
      return;  // OFF is handled by set_power()
  }
  this->set_field_(F_MODE, v);
}

void CommandSender::set_temperature(int temperature) {
  if (temperature < 16)
    temperature = 16;
  if (temperature > 30)
    temperature = 30;
  this->set_field_(F_TEMPERATURE, temperature);
}

void CommandSender::set_fan(CmdFan fan) { this->set_field_(F_FAN, static_cast<uint8_t>(fan)); }

void CommandSender::set_swing(climate::ClimateSwingMode swing) {
  const bool vertical = swing == climate::CLIMATE_SWING_VERTICAL || swing == climate::CLIMATE_SWING_BOTH;
  const bool horizontal = swing == climate::CLIMATE_SWING_HORIZONTAL || swing == climate::CLIMATE_SWING_BOTH;
  this->set_field_(F_SWING_UP_DOWN, vertical);
  this->set_field_(F_SWING_LEFT_RIGHT, horizontal);
}

void CommandSender::set_sleep(SleepMode mode) { this->set_field_(F_SLEEP, static_cast<uint8_t>(mode) / 2); }
void CommandSender::set_eco(bool on) { this->set_field_(F_ECO, on); }
void CommandSender::set_boost(bool on) { this->set_field_(F_BOOST, on); }
void CommandSender::set_quiet(bool on) { this->set_field_(F_QUIET, on); }
void CommandSender::set_display(bool on) { this->set_field_(F_DISPLAY, on); }

void CommandSender::send_command(bool beep) {
  if (beep)
    this->set_bit_(BEEP_BIT, true);

  std::vector<uint8_t> msg;
  msg.reserve(50);
  // header, request, padding, length (30 payload + 11)
  const uint8_t head[] = {0xF4, 0xF5, 0x00, 0x40, 0x29, 0x00, 0x00, 0x01, 0x01,
                          0xFE, 0x01, 0x00, 0x00, 0x65, 0x00, 0x00};
  msg.insert(msg.end(), head, head + sizeof(head));
  msg.insert(msg.end(), this->payload_.begin(), this->payload_.end());

  uint16_t sum = 0;
  for (size_t i = 2; i < msg.size(); i++)
    sum += msg[i];
  msg.push_back(sum >> 8);
  msg.push_back(sum & 0xFF);
  msg.push_back(0xF4);
  msg.push_back(0xFB);

  this->send_raw_(msg.data(), msg.size());
  this->clear();
}

void CommandSender::send_init_1() { this->send_raw_(INIT_1, sizeof(INIT_1)); }
void CommandSender::send_init_2() { this->send_raw_(INIT_2, sizeof(INIT_2)); }
void CommandSender::send_init_3() { this->send_raw_(INIT_3, sizeof(INIT_3)); }
void CommandSender::send_wifi_status() { this->send_raw_(WIFI_STATUS, sizeof(WIFI_STATUS)); }
void CommandSender::send_status_request() { this->send_raw_(STATUS_REQUEST, sizeof(STATUS_REQUEST)); }

void CommandSender::send_raw_(const uint8_t *data, size_t len) {
  ESP_LOGV(TAG, "TX %s", format_hex_pretty(data, len).c_str());
  this->parent_->write_frame(data, len);
}

}  // namespace hisense_ac
}  // namespace esphome
