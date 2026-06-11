#include "mitsubishi_itp.h"

namespace esphome {
namespace mitsubishi_itp {

// TODO: Move this to thermostat
float MitsubishiUART::get_corrected_temp_for_packet_(const Packet &packet, const float temp) {
  // if (!mhk_f_correction_ || packet.get_controller_association() != ControllerAssociation::THERMOSTAT ||
  //     packet.get_source_bridge() == SourceBridge::NONE) {
  //   return temp;
  // }
  // if (packet.get_source_bridge() == SourceBridge::THERMOSTAT) {
  //   const float corrected_temp = mhk_temp_to_actual(temp);
  //   ESP_LOGV(TAG, "Fahrenheit correction: %.1fC MHK to %.1fC actual for %.0fF", temp, corrected_temp,
  //            round(corrected_temp * 9.0f / 5.0f + 32.0f));
  //   return corrected_temp;
  // }
  // const float corrected_temp = mhk_temp_from_actual(temp);
  // ESP_LOGV(TAG, "Fahrenheit correction: %.1fC actual to %.1fC MHK for %.0fF", temp, corrected_temp,
  //          round(temp * 9.0f / 5.0f + 32.0f));
  return temp;
}

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
  // TODO: Re-implement temperature correction

  // Mode

  const climate::ClimateMode old_mode = mode;
  if (packet.get_power()) {
    switch (packet.get_mode()) {
      case 0x01:
      case 0x09:  // i-see
        mode = climate::CLIMATE_MODE_HEAT;
        break;
      case 0x02:
      case 0x0A:  // i-see
        mode = climate::CLIMATE_MODE_DRY;
        break;
      case 0x03:
      case 0x0B:  // i-see
        mode = climate::CLIMATE_MODE_COOL;
        break;
      case 0x07:
        mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case 0x08:
      // unsure when 0x21 or 0x23 would ever be sent, as they seem to be Kumo exclusive, but let's handle them anyways.
      case 0x21:
      case 0x23:
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
  const float old_target_temperature = target_temperature;
  target_temperature = packet.get_target_temp();
  publish_on_update_ |= (old_target_temperature != target_temperature);
  if (mode <= MAX_RECALL_MODE_INDEX) {
    mode_recall_setpoints_[mode] = target_temperature;
  }

  switch (mode) {
    case climate::CLIMATE_MODE_COOL:
    case climate::CLIMATE_MODE_DRY:
      this->mhk_state_.cool_setpoint_ = target_temperature;
      break;
    case climate::CLIMATE_MODE_HEAT:
      this->mhk_state_.heat_setpoint_ = target_temperature;
      break;
    case climate::CLIMATE_MODE_HEAT_COOL:
      this->mhk_state_.cool_setpoint_ = target_temperature + 2;
      this->mhk_state_.heat_setpoint_ = target_temperature - 2;
    default:
      break;
  }

  // Fan
  static bool fan_changed = false;
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
      // TODO: This only works if we get an update while the temps are in this configuration
      // Surely there's some info from the heat pump about which of these modes it's in?
      case climate::CLIMATE_MODE_HEAT_COOL:
        if (current_temperature > target_temperature) {
          action = climate::CLIMATE_ACTION_COOLING;
        } else if (current_temperature < target_temperature) {
          action = climate::CLIMATE_ACTION_HEATING;
        }
        // When the heat pump *changes* to a new action, these temperature comparisons should be accurate.
        // If the mode hasn't changed, but the temps are equal, we can assume the same action and make no change.
        // If the unit overshoots, this still doesn't work.
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
void MitsubishiUART::receive_packet(const RunStateGetResponsePacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());

  run_state_received_ = true;  // Set this since we received one

  // TODO: Not sure what AutoMode does yet
}

// void MitsubishiUART::receive_packet(const SettingsSetRequestPacket &packet) {
//   float packet_temp = packet.get_target_temp();
//   float corrected_temp = get_corrected_temp_for_packet_(packet, packet_temp);

//   if (packet_temp == corrected_temp) {
//     ESP_LOGV(TAG, "Passing through inbound %s", packet.to_string().c_str());
//     route_packet_(packet);
//     alert_listeners_packet_(packet);
//   } else {
//     auto corrected_packet = SettingsSetRequestPacket(packet).set_target_temperature(corrected_temp);
//     ESP_LOGV(TAG, "Passing through temperature-corrected inbound %s", corrected_packet.to_string().c_str());
//     route_packet_(corrected_packet);
//     alert_listeners_packet_(corrected_packet);
//   }
// }

void MitsubishiUART::receive_packet(const RemoteTemperatureSetRequestPacket &packet) {
  ESP_LOGV(TAG, "Processing %s", packet.to_string().c_str());

  // Report the temperature only if the thermostat isn't requesting internal
  if (!packet.get_use_internal_temperature()) {
    float t = packet.get_remote_temperature();
    temperature_source_report(TEMPERATURE_SOURCE_THERMOSTAT, t);
  }
}

// void MitsubishiUART::process_packet(const ThermostatStateUploadPacket &packet) {
//   if (!enhanced_mhk_support_) {
//     ESP_LOGV(TAG, "Passing through inbound %s", packet.to_string().c_str());

//     route_packet_(packet);
//     return;
//   }

//   ESP_LOGV(TAG, "Processing inbound %s", packet.to_string().c_str());

//   // In Fahrenheit correction mode, we store the actual temp in mhk_state_ and only alter it just in time to
//   // send/receive over the wire
//   if (packet.get_flags() & 0x08) {
//     this->mhk_state_.heat_setpoint_ =
//         mhk_f_correction_ ? mhk_temp_to_actual(packet.get_heat_setpoint()) : packet.get_heat_setpoint();
//   }
//   if (packet.get_flags() & 0x10) {
//     this->mhk_state_.cool_setpoint_ =
//         mhk_f_correction_ ? mhk_temp_to_actual(packet.get_cool_setpoint()) : packet.get_cool_setpoint();
//   }

//   ts_bridge_->send_packet(SetResponsePacket());
// }

// void MitsubishiUART::process_packet(const ThermostatAASetRequestPacket &packet) {
//   if (!enhanced_mhk_support_) {
//     ESP_LOGV(TAG, "Passing through inbound %s", packet.to_string().c_str());

//     route_packet_(packet);
//     return;
//   }

//   ESP_LOGV(TAG, "Processing inbound %s", packet.to_string().c_str());

//   ts_bridge_->send_packet(SetResponsePacket());
// }

// TODO: Fix mhk2 mode

// // Process incoming data requests from an MHK probing for/running in enhanced mode
// void MitsubishiUART::handle_thermostat_state_download_request(const GetRequestPacket &packet) {
//   if (!enhanced_mhk_support_) {
//     route_packet_(packet);
//     return;
//   }

//   auto response = ThermostatStateDownloadResponsePacket();

// #ifdef USE_TIME
//   if (this->time_sync_) {
//     response.set_timestamp(this->time_source_->now().timestamp);
//   } else {
//     ESP_LOGW(TAG, "Time source is not synchronized. Cannot provide accurate time!");
//     response.set_timestamp(1704067200);  // 2024-01-01 00:00:00Z
//   }
// #endif

//   response.set_auto_mode((mode == climate::CLIMATE_MODE_HEAT_COOL || mode == climate::CLIMATE_MODE_AUTO));
//   // We store the actual temp in mhk_state_ and only alter it just in time to send/receive over the wire
//   response.set_heat_setpoint(mhk_f_correction_ ? mhk_temp_from_actual(this->mhk_state_.heat_setpoint_)
//                                                : this->mhk_state_.heat_setpoint_);
//   response.set_cool_setpoint(mhk_f_correction_ ? mhk_temp_from_actual(this->mhk_state_.cool_setpoint_)
//                                                : this->mhk_state_.cool_setpoint_);

//   ts_bridge_->send_packet(response);
// }

// void MitsubishiUART::handle_thermostat_ab_get_request(const GetRequestPacket &packet) {
//   if (!enhanced_mhk_support_) {
//     route_packet_(packet);
//     return;
//   }

//   auto response = ThermostatABGetResponsePacket();

//   ts_bridge_->send_packet(response);
// }

}  // namespace mitsubishi_itp
}  // namespace esphome
