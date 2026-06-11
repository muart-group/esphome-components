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
    updated_at = esphome::millis();
    if (!value || value != new_value) {
      value = new_value;
      return true;
    }
    return false;
  }
};
// TODO: Const/config max_age default

// Define which packets will be treated a cached AND forwarded to receivers.
using HeatpumpPacketCache =
    std::tuple<TimestampedValue<CapabilitiesResponsePacket>, TimestampedValue<CurrentTempGetResponsePacket>,
               TimestampedValue<ErrorStateGetResponsePacket>, TimestampedValue<Functions1GetResponsePacket>,
               TimestampedValue<Functions2GetResponsePacket>, TimestampedValue<RunStateGetResponsePacket>,
               TimestampedValue<SettingsGetResponsePacket>, TimestampedValue<StatusGetResponsePacket>,
               TimestampedValue<ZoneGetResponsePacket>>;

using ThermostatPacketCache =
    std::tuple<TimestampedValue<RemoteTemperatureSetRequestPacket>, TimestampedValue<ThermostatHelloPacket>,
               TimestampedValue<ThermostatSensorStatusPacket>, TimestampedValue<ThermostatStateUploadPacket>>;

// Define costexpr to check if a type is part of the tuple (to statically check if a packet type is one
// we want to cache/forward to receivers or not)
template<typename T, typename Tuple> struct is_in_tuple : std::false_type {};

template<typename T, typename... Types>
struct is_in_tuple<T, std::tuple<Types...>> : std::disjunction<std::is_same<T, Types>...> {};

template<typename T, typename Tuple> inline constexpr bool is_in_tuple_v = is_in_tuple<T, Tuple>::value;

class ITPSystemState {
 public:
  bool is_connected() { return connected_.value.value(); }

  void register_receiver(ITPPacketReceiver *receiver) { this->receivers_.push_back(receiver); }

  // Caches the packet and sends to receivers *IF* it's one of the defined cached packet types above (otherwise ignores)
  template<class PType> void cache_heatpump_packet(PType incoming_packet) {
    if constexpr (is_in_tuple_v<TimestampedValue<PType>, HeatpumpPacketCache>) {
      auto &latest_packet = std::get<TimestampedValue<PType>>(heatpump_packet_cache_);
      if (latest_packet.set(incoming_packet)) {
        send_to_receivers_(incoming_packet);
      }
    }
  }

  // Checks received packets and returns the latest packet of the appripriate type if it's fresh enough
  template<class PType> std::optional<PType> check_heatpump_cache(uint32_t max_age_ms = 3000) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(heatpump_packet_cache_);
    if (esphome::millis() - latest_packet.updated_at <= max_age_ms)
      return latest_packet.value;
    return std::nullopt;
  }

  // Caches the packet and sends to receivers *IF* it's one of the defined cached packet types above (otherwise ignores)
  template<class PType> void cache_thermostat_packet(PType incoming_packet, bool always_notify = false) {
    if constexpr (is_in_tuple_v<TimestampedValue<PType>, ThermostatPacketCache>) {
      auto &latest_packet = std::get<TimestampedValue<PType>>(thermostat_packet_cache_);
      if (latest_packet.set(incoming_packet) || always_notify) {
        send_to_receivers_(incoming_packet);
      }
    }
  }

  // Checks received packets and returns the latest packet of the appripriate type if it's fresh enough
  template<class PType> std::optional<PType> check_thermostat_cache(uint32_t max_age_ms = 3000) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(thermostat_packet_cache_);
    if (esphome::millis() - latest_packet.updated_at <= max_age_ms)
      return latest_packet.value;
    return std::nullopt;
  }

 private:
  TimestampedValue<bool> connected_ = TimestampedValue<bool>{false};
  std::vector<ITPPacketReceiver *> receivers_{};

  template<typename T> void send_to_receivers_(const T &packet) const {
    ESP_LOGD("mitsubishi_itp.system", "Sending %i to %i receivers",
             esphome::format_hex_pretty(packet.get_packet_type()).c_str(), this->receivers_.size());
    for (auto *receiver : this->receivers_) {
      receiver->receive_packet(packet);
    }
  }

  HeatpumpPacketCache heatpump_packet_cache_;
  ThermostatPacketCache thermostat_packet_cache_;
};