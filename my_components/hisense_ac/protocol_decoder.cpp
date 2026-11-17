#include "protocol_decoder.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hisense_ac {

static const char *const TAG = "hisense_ac.decoder";

ProtocolDecoder::Frame ProtocolDecoder::validate(const std::vector<uint8_t> &data) {
  Frame f;
  const size_t n = data.size();
  if (n < proto::MIN_FRAME) {
    ESP_LOGV(TAG, "Frame too short: %u", (unsigned) n);
    return f;
  }
  if (data[0] != proto::HEADER_1 || data[1] != proto::HEADER_2 || data[n - 2] != proto::FOOTER_1 ||
      data[n - 1] != proto::FOOTER_2) {
    ESP_LOGV(TAG, "Bad header/footer");
    return f;
  }
  if (data[3] != proto::PADDING_BYTE_1) {
    ESP_LOGV(TAG, "Bad padding byte: %02X", data[3]);
    return f;
  }
  if (n != static_cast<size_t>(data[4]) + proto::LENGTH_OVERHEAD) {
    ESP_LOGV(TAG, "Length mismatch: declared %u, got %u", data[4], (unsigned) n);
    return f;
  }
  // Checksum: 16-bit sum of bytes [2 .. n-4), stored big endian at [n-4, n-3].
  uint16_t sum = 0;
  for (size_t i = 2; i < n - 4; i++)
    sum += data[i];
  const uint16_t received = (static_cast<uint16_t>(data[n - 4]) << 8) | data[n - 3];
  if (sum != received) {
    ESP_LOGV(TAG, "Checksum mismatch: got %04X, calc %04X", received, sum);
    return f;
  }
  f.valid = true;
  f.type = data[13];
  f.sub_type = data[14];
  return f;
}

uint32_t ProtocolDecoder::bits(const std::vector<uint8_t> &data, size_t payload_end, Field f) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < f.size; i++) {
    const size_t bit = f.offset + i;
    const size_t byte = proto::PAYLOAD_START + bit / 8;
    if (byte >= payload_end)
      return 0;
    const uint8_t b = (data[byte] >> (7 - (bit % 8))) & 0x01;
    value = (value << 1) | b;
  }
  return value;
}

bool ProtocolDecoder::decode_status(const std::vector<uint8_t> &data, AcStatus &out) {
  const size_t payload_end = data.size() - 4;
  if (payload_end <= proto::PAYLOAD_START || (payload_end - proto::PAYLOAD_START) * 8 < MIN_PAYLOAD_BITS) {
    ESP_LOGW(TAG, "Status payload too short (%u bytes)", (unsigned) data.size());
    return false;
  }

  out.fan_raw = bits(data, payload_end, F_FAN);
  const uint8_t sleep_raw = bits(data, payload_end, F_SLEEP);
  out.sleep = is_valid_sleep(sleep_raw) ? static_cast<SleepMode>(sleep_raw) : SleepMode::SLEEP_OFF;
  out.mode_raw = bits(data, payload_end, F_MODE);
  out.power = bits(data, payload_end, F_POWER) != 0;
  out.direction = bits(data, payload_end, F_DIRECTION);
  out.target_temp = bits(data, payload_end, F_SET_TEMP);
  out.current_temp = bits(data, payload_end, F_INDOOR_TEMP);
  out.swing_up_down = bits(data, payload_end, F_SWING_UP_DOWN) != 0;
  out.swing_left_right = bits(data, payload_end, F_SWING_LEFT_RIGHT) != 0;
  out.eco = bits(data, payload_end, F_ECO) != 0;
  out.boost = bits(data, payload_end, F_BOOST) != 0;
  out.quiet = bits(data, payload_end, F_QUIET) != 0;
  out.display = bits(data, payload_end, F_DISPLAY) != 0;

  ESP_LOGD(TAG, "Status: power=%d mode=%u target=%u room=%d fan=%u sleep=%u eco=%d boost=%d quiet=%d", out.power,
           out.mode_raw, out.target_temp, out.current_temp, out.fan_raw, (uint8_t) out.sleep, out.eco, out.boost,
           out.quiet);
  return true;
}

}  // namespace hisense_ac
}  // namespace esphome
