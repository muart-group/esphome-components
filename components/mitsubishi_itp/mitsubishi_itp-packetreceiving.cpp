#include "mitsubishi_itp.h"

namespace esphome {
namespace mitsubishi_itp {

// Packet Receivers
void MitsubishiUART::receive_packet(const Packet &packet) {
  ESP_LOGI(TAG, "Generic unhandled packet type %x received.", packet.get_packet_type());
  ESP_LOGD(TAG, "%s", packet.to_string().c_str());
}

void MitsubishiUART::receive_packet(const CapabilitiesResponsePacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());
  ESP_LOGI(TAG, "Received heat pump identification packet.");
}

void MitsubishiUART::receive_packet(const SettingsGetResponsePacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());

  float packet_temp = packet.get_target_temp();

  // Mode
  const climate::ClimateMode old_mode = mode;
  if (packet.get_power()) {
    switch (packet.get_mode()) {
      case 0x01:
      case 0x09:  // i-see
      case 0x21:  // unsure when 0x21 or 0x23 would ever be sent, as they seem to be Kumo exclusive, but let's handle
                  // them anyways.
        if (mode != climate::CLIMATE_MODE_HEAT_COOL) {
          mode = climate::CLIMATE_MODE_HEAT;
        }
        break;
      case 0x02:
      case 0x0A:  // i-see
        mode = climate::CLIMATE_MODE_DRY;
        break;
      case 0x03:
      case 0x0B:  // i-see
      case 0x32:  // unsure when 0x21 or 0x23 would ever be sent, as they seem to be Kumo exclusive, but let's handle
                  // them anyways.
        if (mode != climate::CLIMATE_MODE_HEAT_COOL) {
          mode = climate::CLIMATE_MODE_COOL;
        }
        break;
      case 0x07:
        mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case 0x08:  // Auto
        // Override built-in auto with HEAT_COOL
        mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
      default:
        mode = climate::CLIMATE_MODE_OFF;
    }
  } else {
    mode = climate::CLIMATE_MODE_OFF;
  }

  publish_on_update_ |= (old_mode != mode);

  // Temperature
  switch (packet.get_mode()) {
    case 0x02:  // Dry
    case 0x0A:  // i-See Dry
    case 0x03:  // Cool
    case 0x0B:  // i-See Cool
    case 0x23:  // Auto-Cool (not sure this will ever be returned outside Kumo)
      if (target_temperature_high != packet_temp) {
        target_temperature_high = packet_temp;
        publish_on_update_ = true;
      }
      break;
    case 0x01:  // Heat
    case 0x09:  // i-See Heat
    case 0x21:  // Auto-Heat (not sure this will ever be returned outside Kumo)
      if (target_temperature_low != packet_temp) {
        target_temperature_low = packet_temp;
        publish_on_update_ = true;
      }
      break;
    case 0x07:  // Fan
                // Do nothing, setpoint for fan isn't really valid/tracked
    case 0x08:  // Auto
      // Do nothing, this mode is fleeting
      break;
    default:
      break;
  }

  // Fan
  bool fan_changed = false;
  switch (packet.get_fan()) {
    case 0x00:
      fan_changed = set_fan_mode_(climate::CLIMATE_FAN_AUTO);
      break;
    case 0x01:
      fan_changed = set_fan_mode_(climate::CLIMATE_FAN_QUIET);
      break;
    case 0x02:
      fan_changed = set_fan_mode_(climate::CLIMATE_FAN_LOW);
      break;
    case 0x03:
      fan_changed = set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
      break;
    case 0x05:
      fan_changed = set_fan_mode_(climate::CLIMATE_FAN_HIGH);
      break;
    case 0x06:
      fan_changed = set_custom_fan_mode_(FAN_MODE_VERYHIGH);
      break;
  }

  publish_on_update_ |= fan_changed;
}

void MitsubishiUART::receive_packet(const CurrentTempGetResponsePacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());

  float packet_temp = packet.get_current_temp();

  // This will be the same as the remote temperature if we're using a remote sensor, otherwise the internal temp
  const float old_current_temperature = current_temperature;
  current_temperature = packet.get_current_temp();

  publish_on_update_ |= (old_current_temperature != current_temperature);

  // Use the presense of ThermostatStateUploadPacket as a proxy for an auto-capable thermostat being attached
  if (mode == climate::CLIMATE_MODE_HEAT_COOL &&
      !itp_sys_state_.get_thermostat_cache_age<ThermostatStateUploadPacket>() < 900000) {
    if (itp_sys_state_.is_heatpump_on_heat() && current_temperature >= target_temperature_high) {
      // If we're on heat, but the temperature has hit the high-setpoint, switch to COOL
      ClimateCommand cmd = ClimateCommand();
      cmd.mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_COOL)
          .target_temperature_degC(target_temperature_high);
      heatpump_.send_command(cmd);
    } else if (itp_sys_state_.is_heatpump_on_cool() && current_temperature <= target_temperature_low) {
      // If we're on cool, but the temperature has hit the low-setpoint, switch to HEAT
      ClimateCommand cmd = ClimateCommand();
      cmd.mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_HEAT)
          .target_temperature_degC(target_temperature_low);
      heatpump_.send_command(cmd);
    }
  }
}

void MitsubishiUART::receive_packet(const StatusGetResponsePacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());

  const climate::ClimateAction old_action = action;

  // If mode is off, action is off
  if (mode == climate::CLIMATE_MODE_OFF) {
    action = climate::CLIMATE_ACTION_OFF;
  }
  // If mode is fan only, packet.getOperating() may be false, but the fan is running
  else if (mode == climate::CLIMATE_MODE_FAN_ONLY) {
    action = climate::CLIMATE_ACTION_FAN;
  }
  // If mode is anything other than off or fan, and the unit is operating, determine the action
  else if (packet.get_operating()) {
    switch (mode) {
      case climate::CLIMATE_MODE_HEAT:
        action = climate::CLIMATE_ACTION_HEATING;
        break;
      case climate::CLIMATE_MODE_COOL:
        action = climate::CLIMATE_ACTION_COOLING;
        break;
      case climate::CLIMATE_MODE_DRY:
        action = climate::CLIMATE_ACTION_DRYING;
        break;
      case climate::CLIMATE_MODE_HEAT_COOL:
        if (itp_sys_state_.is_heatpump_on_cool()) {
          action = climate::CLIMATE_ACTION_COOLING;
        } else if (itp_sys_state_.is_heatpump_on_heat()) {
          action = climate::CLIMATE_ACTION_HEATING;
        }
        // This is a little wishy-washy because we have to assume that a GetSettingsResponse was cached
        // just before this to ensure the heat pump's state is correctly known, but it should either happen that way
        // or will be corrected within one update cycle
        break;
      default:
        ESP_LOGW(TAG, "Unhandled mode %i.", mode);
        break;
    }
  }
  // If we're not operating (but not off or in fan mode), we're idle
  // Should be relatively safe to fall through any unknown modes into showing IDLE
  else {
    action = climate::CLIMATE_ACTION_IDLE;
  }

  publish_on_update_ |= (old_action != action);
}

void MitsubishiUART::receive_packet(const RemoteTemperatureSetRequestPacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());

  // Report the temperature only if the thermostat isn't requesting internal
  if (!packet.get_use_internal_temperature()) {
    float t = packet.get_remote_temperature();
    temperature_source_report(TEMPERATURE_SOURCE_THERMOSTAT, t);
  }
}

void MitsubishiUART::receive_packet(const ThermostatStateUploadPacket &packet) {
  if (packet.get_flags() & 0x08) {
    if (packet.get_auto_mode() > 0x00) {
      mode = climate::CLIMATE_MODE_HEAT_COOL;
      publish_on_update_ = true;
    } else {
      if (itp_sys_state_.check_heatpump_cache<SettingsGetResponsePacket>()) {
        SettingsGetResponsePacket last_settings = *itp_sys_state_.check_heatpump_cache<SettingsGetResponsePacket>();
        switch (last_settings.get_mode()) {
          case 0x02:  // Dry
          case 0x0A:  // i-See Dry
            mode = climate::CLIMATE_MODE_DRY;
            break;
          case 0x03:  // Cool
          case 0x0B:  // i-See Cool
          case 0x23:  // Auto-Cool (not sure this will ever be returned outside Kumo)
            mode = climate::CLIMATE_MODE_COOL;
            break;
          case 0x01:  // Heat
          case 0x09:  // i-See Heat
          case 0x21:  // Auto-Heat (not sure this will ever be returned outside Kumo)
            mode = climate::CLIMATE_MODE_HEAT;
            break;
          case 0x07:  // Fan
            mode = climate::CLIMATE_MODE_FAN_ONLY;
            break;
          case 0x08:  // Auto
            // Do nothing, this mode is fleeting
            break;
          default:
            mode = climate::CLIMATE_MODE_OFF;
            break;
        }
      }
    }
  }
  if (packet.get_flags() & 0x08) {
    target_temperature_low = thermostat_->mhk_fahrenheit_correction_is_on()
                                 ? mhk_temp_to_actual(packet.get_heat_setpoint())
                                 : packet.get_heat_setpoint();
  }
  if (packet.get_flags() & 0x10) {
    target_temperature_high = thermostat_->mhk_fahrenheit_correction_is_on()
                                  ? mhk_temp_to_actual(packet.get_cool_setpoint())
                                  : packet.get_cool_setpoint();
  }
}

}  // namespace mitsubishi_itp
}  // namespace esphome
