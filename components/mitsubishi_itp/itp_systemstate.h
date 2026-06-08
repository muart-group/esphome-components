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

  void register_heatpump_receiver(HeatpumpPacketReceiver *receiver) { this->heatpump_receivers_.push_back(receiver); }
  void register_thermostat_receiver(ThermostatPacketReceiver *receiver) {
    this->thermostat_receivers_.push_back(receiver);
  }

  // Caches the packet and sends to receivers *IF* it's one of the defined cached packet types above (otherwise ignores)
  template<class PType> void cache_heatpump_packet(PType incoming_packet) {
    if constexpr (is_in_tuple_v<TimestampedValue<PType>, HeatpumpPacketCache>) {
      ESP_LOGV("mitsubishi_itp.system", "Caching heatpump packet.");
      auto &latest_packet = std::get<TimestampedValue<PType>>(heatpump_packet_cache_);
      latest_packet.set(incoming_packet);
      send_to_heatpump_receivers_(incoming_packet);
    }
  }

  // Checks received packets and returns the latest packet of the appripriate type if it's fresh enough
  template<class PType> std::optional<PType> check_heatpump_cache(uint32_t max_age_ms = 3000) const {
    auto &latest_packet = std::get<TimestampedValue<PType>>(heatpump_packet_cache_);
    if (esphome::millis() - latest_packet.updated_at <= max_age_ms)
      return latest_packet.value;
    return std::nullopt;
  }

  // Caches the packet, returning true if it was modified, false if it was the same
  template<class PType> bool cache_thermostat_packet(PType incoming_packet) {
    auto &latest_packet = std::get<TimestampedValue<PType>>(thermostat_packet_cache_);
    return latest_packet.set(incoming_packet);
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
  std::vector<HeatpumpPacketReceiver *> heatpump_receivers_{};
  std::vector<ThermostatPacketReceiver *> thermostat_receivers_{};

  template<typename T> void send_to_heatpump_receivers_(const T &packet) const {
    for (auto *receiver : this->heatpump_receivers_) {
      receiver->receive_packet(packet);
    }
  }

  template<typename T> void send_to_thermostat_receivers_(const T &packet) const {
    for (auto *receiver : this->thermostat_receivers_) {
      receiver->receive_packet(packet);
    }
  }

  HeatpumpPacketCache heatpump_packet_cache_;
  ThermostatPacketCache thermostat_packet_cache_;
};