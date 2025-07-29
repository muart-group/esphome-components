#pragma once

#include "esphome/components/climate/climate.h"
#include "itp_packet.h"

namespace esphome {
namespace mitsubishi_itp {

class MITPUtils {
 public:
  /// Read a string out of data, wordSize bits at a time.
  /// Used to decode serial numbers and other information from a thermostat.
  static std::string decode_n_bit_string(const uint8_t data[], size_t data_length, size_t word_size = 6) {
    auto result = std::string();

    for (int i = 0; i < data_length; i++) {
      auto bits = bit_slice(data, i * word_size, ((i + 1) * word_size) - 1);
      if (bits <= 0x1F)
        bits += 0x40;
      result += (char) bits;
    }

    return result;
  }

  static float temp_scale_a_to_deg_c(const uint8_t value) { return (float) (value - 128) / 2.0f; }

  static uint8_t deg_c_to_temp_scale_a(const float value) {
    // Special cases
    if (value < -64)
      return 0;
    if (value > 63.5f)
      return 0xFF;

    return (uint8_t) round(value * 2) + 128;
  }

  static float legacy_target_temp_to_deg_c(const uint8_t value) {
    return ((float) (31 - (value & 0x0F)) + (((value & 0xF0) > 0) ? 0.5f : 0));
  }

  static uint8_t deg_c_to_legacy_target_temp(const float value) {
    // Special cases per docs
    if (value < 16)
      return 0x0F;
    if (value > 31.5)
      return 0x10;

    return ((31 - (uint8_t) value) & 0xF) + (((int) (value * 2) % 2) << 4);
  }

  static float legacy_hp_room_temp_to_deg_c(const uint8_t value) { return (float) value + 10; }

  static uint8_t deg_c_to_legacy_hp_room_temp(const float value) {
    if (value < 10)
      return 0x00;
    if (value > 41)
      return 0x1F;

    return (uint8_t) value - 10;
  }

  static float legacy_ts_room_temp_to_deg_c(const uint8_t value) { return 8 + ((float) value * 0.5f); }

  static uint8_t deg_c_to_legacy_ts_room_temp(const float value) {
    if (value < 8)
      return 0x00;
    if (value > 39.5f)
      return 0x3F;

    return ((uint8_t) (2 * value)) - 16;
  }

  static climate::ClimateTraits capabilities_to_traits(const itp_packet::CapabilitiesResponsePacket &pkt) {
    auto ct = esphome::climate::ClimateTraits();

    // always enabled
    ct.add_supported_mode(esphome::climate::CLIMATE_MODE_COOL);
    ct.add_supported_mode(esphome::climate::CLIMATE_MODE_OFF);

    if (!pkt.is_heat_disabled())
      ct.add_supported_mode(esphome::climate::CLIMATE_MODE_HEAT);
    if (!pkt.is_dry_disabled())
      ct.add_supported_mode(esphome::climate::CLIMATE_MODE_DRY);
    if (!pkt.is_fan_disabled())
      ct.add_supported_mode(esphome::climate::CLIMATE_MODE_FAN_ONLY);

    if (pkt.supports_vane_swing()) {
      ct.add_supported_swing_mode(esphome::climate::CLIMATE_SWING_OFF);

      if (pkt.supports_vane() && pkt.supports_h_vane())
        ct.add_supported_swing_mode(esphome::climate::CLIMATE_SWING_BOTH);
      if (pkt.supports_vane())
        ct.add_supported_swing_mode(esphome::climate::CLIMATE_SWING_VERTICAL);
      if (pkt.supports_h_vane())
        ct.add_supported_swing_mode(esphome::climate::CLIMATE_SWING_HORIZONTAL);
    }

    ct.set_visual_min_temperature(std::min(pkt.get_min_cool_dry_setpoint(), pkt.get_min_heating_setpoint()));
    ct.set_visual_max_temperature(std::max(pkt.get_max_cool_dry_setpoint(), pkt.get_max_heating_setpoint()));

    // TODO: Figure out what these states *actually* map to so we aren't sending bad data.
    // This is probably a dynamic map, so the setter will need to be aware of things.
    switch (pkt.get_supported_fan_speeds()) {
      case 1:
        ct.set_supported_fan_modes({esphome::climate::CLIMATE_FAN_HIGH});
        break;
      case 2:
        ct.set_supported_fan_modes({esphome::climate::CLIMATE_FAN_LOW, esphome::climate::CLIMATE_FAN_HIGH});
        break;
      case 3:
        ct.set_supported_fan_modes({esphome::climate::CLIMATE_FAN_LOW, esphome::climate::CLIMATE_FAN_MEDIUM,
                                    esphome::climate::CLIMATE_FAN_HIGH});
        break;
      case 4:
        ct.set_supported_fan_modes({
            esphome::climate::CLIMATE_FAN_QUIET,
            esphome::climate::CLIMATE_FAN_LOW,
            esphome::climate::CLIMATE_FAN_MEDIUM,
            esphome::climate::CLIMATE_FAN_HIGH,
        });
        break;
      case 5:
        ct.set_supported_fan_modes({
            esphome::climate::CLIMATE_FAN_QUIET,
            esphome::climate::CLIMATE_FAN_LOW,
            esphome::climate::CLIMATE_FAN_MEDIUM,
            esphome::climate::CLIMATE_FAN_HIGH,
        });
        ct.add_supported_custom_fan_mode("Very High");
        break;
      default:
        // no-op, don't set a fan mode.
        break;
    }
    if (!pkt.auto_fan_speed_disabled())
      ct.add_supported_fan_mode(esphome::climate::CLIMATE_FAN_AUTO);

    return ct;
  }

 private:
  /// Extract the specified bits (inclusive) from an arbitrarily-sized byte array. Does not perform bounds checks.
  /// Max extraction is 64 bits. Preserves endianness of incoming data stream.
  static uint64_t bit_slice(const uint8_t ds[], size_t start, size_t end) {
    if ((end - start) >= 64)
      return 0;

    uint64_t result = 0;

    size_t start_byte = (start) / 8;
    size_t end_byte = ((end) / 8) + 1;  // exclusive, used for length calc

    // raw copy the relevant bytes into our int64, preserving endian-ness
    std::memcpy(&result, &ds[start_byte], end_byte - start_byte);
    result = byteswap(result);

    // shift out the bits we don't want from the end (64 + credit any pre-sliced bits)
    result >>= (sizeof(uint64_t) * 8) + (start_byte * 8) - end - 1;

    // mask out the number of bits we want
    result &= (1 << (end - start + 1)) - 1;

    return result;
  }
};

}  // namespace mitsubishi_itp
}  // namespace esphome
