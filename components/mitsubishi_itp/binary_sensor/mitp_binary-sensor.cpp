#include "mitp_binary-sensor.h"

namespace esphome {
namespace mitsubishi_itp {

void ThermostatCommandReceivedSensor::receive_packet(const SettingsGetResponsePacket &packet) {
    has_last_packet_ = true;
    last_power_ = packet.get_power();
    last_mode_ = packet.get_mode();
    last_target_temp_ = packet.get_target_temp();
    last_fan_ = packet.get_fan();
    last_vane_ = packet.get_vane();
    last_horizontal_vane_ = packet.get_horizontal_vane();
}

void ThermostatCommandReceivedSensor::receive_packet(const SettingsSetRequestPacket &packet) {
    /* MHK2s have been observed to re-send the current settings in certain circumstances when their
    RunState changes. This can make it appear as if the thermostat modified settings when it actually
    is just re-sending the current state. To avoid this, we're tracking the last state for
    (hopefully) all the important bytes, and only count this is a real change if one of them
    has been modified.
    */

    // If we haven't got any settings yet, just assume true for safety.
    if (!has_last_packet_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    if (packet.get_flags() & 0x01
        && packet.get_power() != last_power_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    if (packet.get_flags() & 0x02
        && packet.get_mode() != last_mode_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    if (packet.get_flags() & 0x04
        && packet.get_target_temp() != last_target_temp_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    if (packet.get_flags() & 0x08
        && packet.get_fan() != last_fan_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    if (packet.get_flags() & 0x10
        && packet.get_vane() != last_vane_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    if (packet.get_flags_2() & 0x01
        && packet.get_horizontal_vane() != last_horizontal_vane_) {
        mitp_binary_sensor_state_ = true;
        return;
    }

    // Nothing of note changed, so don't count this as a touch
}

}
}