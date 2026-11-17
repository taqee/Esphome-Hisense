#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "common_types.h"

namespace esphome {
namespace hisense_ac {

namespace proto {
static constexpr uint8_t HEADER_1 = 0xF4;
static constexpr uint8_t HEADER_2 = 0xF5;
static constexpr uint8_t FOOTER_1 = 0xF4;
static constexpr uint8_t FOOTER_2 = 0xFB;
static constexpr uint8_t PADDING_BYTE_1 = 0x40;
static constexpr size_t MIN_FRAME = 16;
static constexpr size_t MAX_FRAME = 255 + 9;
static constexpr size_t LENGTH_OVERHEAD = 9;  // total = data[4] + 9
static constexpr size_t PAYLOAD_START = 16;

static constexpr uint8_t TYPE_STATUS = 0x66;
static constexpr uint8_t SUB_STATUS = 0x00;
static constexpr uint8_t TYPE_COMMAND = 0x65;
static constexpr uint8_t SUB_COMMAND = 0x00;
}  // namespace proto

class ProtocolDecoder {
 public:
  struct Frame {
    bool valid{false};
    uint8_t type{0};
    uint8_t sub_type{0};
  };

  // Header, footer, length and checksum validation.
  static Frame validate(const std::vector<uint8_t> &data);

  // True for frames that carry the full AC status (status reply or command reply).
  static bool carries_status(const Frame &f) {
    return (f.type == proto::TYPE_STATUS && f.sub_type == proto::SUB_STATUS) ||
           (f.type == proto::TYPE_COMMAND && f.sub_type == proto::SUB_COMMAND);
  }

  // Fills `out` from a validated status frame. Returns false if the payload is too short.
  static bool decode_status(const std::vector<uint8_t> &data, AcStatus &out);

 private:
  struct Field {
    uint16_t offset;
    uint8_t size;
  };
  // Bit offsets inside the payload (MSB first).
  static constexpr Field F_FAN = {0, 8};
  static constexpr Field F_SLEEP = {8, 8};
  static constexpr Field F_MODE = {16, 4};
  static constexpr Field F_POWER = {20, 1};
  static constexpr Field F_DIRECTION = {22, 2};
  static constexpr Field F_SET_TEMP = {24, 8};
  static constexpr Field F_INDOOR_TEMP = {32, 8};
  static constexpr Field F_SWING_UP_DOWN = {152, 1};
  static constexpr Field F_SWING_LEFT_RIGHT = {153, 1};
  static constexpr Field F_ECO = {157, 1};
  static constexpr Field F_BOOST = {158, 1};
  static constexpr Field F_QUIET = {165, 1};
  static constexpr Field F_DISPLAY = {168, 1};
  static constexpr size_t MIN_PAYLOAD_BITS = 176;

  static uint32_t bits(const std::vector<uint8_t> &data, size_t payload_end, Field f);
};

}  // namespace hisense_ac
}  // namespace esphome
