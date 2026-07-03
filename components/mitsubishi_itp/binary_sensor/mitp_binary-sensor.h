#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../mitp_listener.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

class MITPBinarySensor : public MITPListener, public binary_sensor::BinarySensor {
 public:
  void publish() override {
    if (mitp_binary_sensor_state_.has_value())
      // Binary sensors automatically dedup publishes (I think) and so will only actually publish on change
      publish_state(mitp_binary_sensor_state_.value());
  }

 protected:
  optional<bool> mitp_binary_sensor_state_;
};

class DefrostSensor : public MITPBinarySensor {
  void receive_packet(const RunStateGetResponsePacket &packet) override {
    mitp_binary_sensor_state_ = packet.in_defrost();
  }
};
class FilterStatusSensor : public MITPBinarySensor {
  void receive_packet(const RunStateGetResponsePacket &packet) override {
    mitp_binary_sensor_state_ = packet.service_filter();
  }
};
class PreheatSensor : public MITPBinarySensor {
  void receive_packet(const RunStateGetResponsePacket &packet) override {
    mitp_binary_sensor_state_ = packet.in_preheat();
  }
};
class StandbySensor : public MITPBinarySensor {
  void receive_packet(const RunStateGetResponsePacket &packet) override {
    mitp_binary_sensor_state_ = packet.in_standby();
  }
};
class ISeeStatusSensor : public MITPBinarySensor {
  void receive_packet(const SettingsGetResponsePacket &packet) override {
    mitp_binary_sensor_state_ = packet.is_i_see_enabled();
  }
};
class UsingInternalTemperatureSensor : public MITPBinarySensor {
  void using_internal_temperature(const bool using_internal) override { mitp_binary_sensor_state_ = using_internal; }
};
class ThermostatCommandReceivedSensor : public MITPBinarySensor {
  // TODO: This needs to be the correct packet (but we don't forward those, so we might need)
  // a separate call a la using_internal_temperature
  void receive_packet(const SettingsSetRequestPacket &packet) override { mitp_binary_sensor_state_ = true; }

  void publish() override {
    if (mitp_binary_sensor_state_.has_value())
      // Binary sensors automatically dedup publishes (I think) and so will only actually publish on change
      publish_state(mitp_binary_sensor_state_.value());
    // We only want this sensor to briefly jump to true when a command is received, and then back to false
    if (mitp_binary_sensor_state_) {
      mitp_binary_sensor_state_ = false;
    }
  }
};

}  // namespace mitsubishi_itp
}  // namespace esphome
