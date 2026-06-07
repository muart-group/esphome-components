#pragma once

#include <optional>
#include <tuple>
#include "itp_packetreceiver.h"

using namespace itp_packet;

// TODO: Will store last received packets of each type that contains state (e.g. not most SetRequest packets, but most
// Response packets) and their received time as milliseconds

template<class T> struct TimestampedValue {
  std::optional<T> value{std::nullopt};
  uint32_t updated_at{0};  // Millis when the value was last checked

  bool set(T new_value) {
    updated_at = millis();
    if (!value || value != new_value) {
      value = new_value;
      return true;
    }
    return false;
  }
};
// TODO: Const/config max_age default

class ITPSystemState {
 public:
  bool is_connected() { return connected_.value.value(); }

  // Caches the packet, returning true if it was modified, false if it was the same
  template<class PType> bool cache_heatpump_packet(PType incoming_packet) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(heatpump_packet_cache_);
    ESP_LOGV(HEATPUMP_TAG, "Cached heatpump packet.");
    return latest_packet.set(incoming_packet);
  }

  // Checks received packets and returns the latest packet of the appripriate type if it's fresh enough
  template<class PType> std::optional<PType> check_heatpump_cache(uint32_t max_age_ms = 3000) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(heatpump_packet_cache_);
    if (millis() - latest_packet.updated_at <= max_age_ms)
      return latest_packet.value;
    return std::nullopt;
  }

  // Caches the packet, returning true if it was modified, false if it was the same
  template<class PType> bool cache_thermostat_packet(PType incoming_packet) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(thermostat_packet_cache_);
    return latest_packet.set(incoming_packet);
  }

  // Checks received packets and returns the latest packet of the appripriate type if it's fresh enough
  template<class PType> std::optional<PType> check_thermostat_cache(uint32_t max_age_ms = 3000) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(thermostat_packet_cache_);
    if (millis() - latest_packet.updated_at <= max_age_ms)
      return latest_packet.value;
    return std::nullopt;
  }

 private:
  TimestampedValue<bool> connected_ = TimestampedValue<bool>{false};

  std::tuple<TimestampedValue<CapabilitiesResponsePacket>, TimestampedValue<CurrentTempGetResponsePacket>,
             TimestampedValue<ErrorStateGetResponsePacket>, TimestampedValue<Functions1GetResponsePacket>,
             TimestampedValue<Functions2GetResponsePacket>, TimestampedValue<RunStateGetResponsePacket>,
             TimestampedValue<SettingsGetResponsePacket>, TimestampedValue<StatusGetResponsePacket>,
             TimestampedValue<ZoneGetResponsePacket>>
      heatpump_packet_cache_;

  std::tuple<TimestampedValue<RemoteTemperatureSetRequestPacket>, TimestampedValue<ThermostatHelloPacket>,
             TimestampedValue<ThermostatSensorStatusPacket>, TimestampedValue<ThermostatStateUploadPacket>>
      thermostat_packet_cache_;
};