#pragma once

#include "esphome/components/sensor/sensor.h"
#include "../mitp_listener.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

class MITPSensor : public MITPListener, public sensor::Sensor {
 public:
  void publish() override {
    // Only publish if force, or a change has occurred and we have a real value
    if (!std::isnan(mitp_sensor_state_) && mitp_sensor_state_ != state) {
      publish_state(mitp_sensor_state_);
    }
  }

 protected:
  float mitp_sensor_state_ = NAN;
};

class CompressorFrequencySensor : public MITPSensor {
  void process_packet(const StatusGetResponsePacket &packet) { mitp_sensor_state_ = packet.get_compressor_frequency(); }
};

class InputWattsSensor : public MITPSensor {
  void process_packet(const StatusGetResponsePacket &packet) { mitp_sensor_state_ = packet.get_input_watts(); }
};

class LifetimeKwhSensor : public MITPSensor {
  void process_packet(const StatusGetResponsePacket &packet) { mitp_sensor_state_ = packet.get_lifetime_kwh(); }
};

class OutdoorTemperatureSensor : public MITPSensor {
  void process_packet(const CurrentTempGetResponsePacket &packet) { mitp_sensor_state_ = packet.get_outdoor_temp(); }
};

class RuntimeSensor : public MITPSensor {
  void process_packet(const CurrentTempGetResponsePacket &packet) { mitp_sensor_state_ = packet.get_runtime_minutes(); }
};

class ThermostatHumiditySensor : public MITPSensor {
  void process_packet(const ThermostatSensorStatusPacket &packet) {
    mitp_sensor_state_ = packet.get_indoor_humidity_percent();
  }
};

class ThermostatTemperatureSensor : public MITPSensor {
  void process_packet(const RemoteTemperatureSetRequestPacket &packet) {
    if (!packet.get_use_internal_temperature()) {
      mitp_sensor_state_ = packet.get_remote_temperature();
      force_next_publish_ = true;  // Set true to force publish even if value the same
    }
  }
  void publish() override {
    // Always publish if force_next_publish_ so that we can expose how often
    // the thermostat is actually reporting values
    if (!std::isnan(mitp_sensor_state_) && (mitp_sensor_state_ != state || force_next_publish_)) {
      force_next_publish_ = false;
      publish_state(mitp_sensor_state_);
    }
  }

 private:
  bool force_next_publish_ = false;  // If true, will force a publish on next listener->publish() call
};

}  // namespace mitsubishi_itp
}  // namespace esphome
