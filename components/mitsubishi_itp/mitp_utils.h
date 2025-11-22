#pragma once

#include "esphome/components/climate/climate.h"
#include "itp_packets.h"

namespace esphome {
namespace mitsubishi_itp {

class MITPUtils {
 public:
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
        ct.set_supported_custom_fan_modes(std::vector<const char *>{itp_packet::FAN_MODE_VERYHIGH});
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
