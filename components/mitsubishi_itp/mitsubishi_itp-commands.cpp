#include "mitsubishi_itp.h"

namespace esphome {
namespace mitsubishi_itp {

// Called to instruct a change of the climate controls
void MitsubishiUART::control(const climate::ClimateCall &call) {
  ClimateCommand cmd = ClimateCommand();

  // Apply fan settings
  // Prioritize a custom fan mode if it's set.
  if (call.has_custom_fan_mode()) {
    if (call.get_custom_fan_mode() == FAN_MODE_VERYHIGH) {
      set_custom_fan_mode_(FAN_MODE_VERYHIGH);
      cmd.fanSpeed(SettingsSetRequestPacket::FAN_4);
    }
  } else if (call.get_fan_mode().has_value()) {
    switch (call.get_fan_mode().value()) {
      case climate::CLIMATE_FAN_QUIET:
        set_fan_mode_(climate::CLIMATE_FAN_QUIET);
        cmd.fanSpeed(SettingsSetRequestPacket::FAN_QUIET);
        break;
      case climate::CLIMATE_FAN_LOW:
        set_fan_mode_(climate::CLIMATE_FAN_LOW);
        cmd.fanSpeed(SettingsSetRequestPacket::FAN_1);
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
        cmd.fanSpeed(SettingsSetRequestPacket::FAN_2);
        break;
      case climate::CLIMATE_FAN_HIGH:
        set_fan_mode_(climate::CLIMATE_FAN_HIGH);
        cmd.fanSpeed(SettingsSetRequestPacket::FAN_3);
        break;
      case climate::CLIMATE_FAN_AUTO:
        set_fan_mode_(climate::CLIMATE_FAN_AUTO);
        cmd.fanSpeed(SettingsSetRequestPacket::FAN_AUTO);
        break;
      default:
        ESP_LOGW(TAG, "Unhandled fan mode %i!", call.get_fan_mode().value());
        break;
    }
  }

  // Temperature

  // Mode

  if (call.get_mode().has_value()) {
    mode = call.get_mode().value();

    switch (call.get_mode().value()) {
      case climate::CLIMATE_MODE_HEAT_COOL:
        if (thermostat_) {
          thermostat_->set_auto_mode(0x01);  // 0x01 for now (is 0x02 the other of heat vs cool?)
        }
        if (current_temperature > target_temperature_low) {
          cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_COOL);
        } else {
          cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_HEAT);
        }
        break;
      case climate::CLIMATE_MODE_COOL:
        cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_COOL);
        break;
      case climate::CLIMATE_MODE_HEAT:
        cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_HEAT);
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_FAN);
        break;
      case climate::CLIMATE_MODE_DRY:
        cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_DRY);
        break;
      case climate::CLIMATE_MODE_OFF:
      default:
        cmd.power(false);
        break;
    }
  }

  // Target Temperature
  // TODO: This is a bit messy-- we need to support climate calls that don't contain a new target high/low temperature,
  // so we have to look them up from the previous temp. Seems like maybe this should be stored a little more robustly
  // (like a specific high/low cool/heat instead of in the mode array), but does that differ from the thermostat's
  // high/low? If there's no thermostat we need to store it somewhere, should we just load low/high setpoints from
  // memory directly rather than recalling them here?

  if (call.get_target_temperature_low().has_value()) {
    target_temperature_low = call.get_target_temperature_low().value();
    if (thermostat_) {
      thermostat_->set_heat_setpoint(target_temperature_low);
    }

    // If we're in HEAT or auto-HEAT, set as target
    if (mode == climate::CLIMATE_MODE_HEAT ||
        (mode == climate::CLIMATE_MODE_HEAT_COOL && current_temperature <= target_temperature_low)) {
      ESP_LOGD(TAG, "Setting target temp to %f, mode is %i", target_temperature_low, mode);
      cmd.target_temperature_degC(target_temperature_low);
    }
  } else if (call.get_mode().has_value() &&
             (mode == climate::CLIMATE_MODE_HEAT ||
              (mode == climate::CLIMATE_MODE_HEAT_COOL && current_temperature <= target_temperature_low))) {
    // If we didn't get a new target temp, but we did get a mode, use the last known target temp:
    auto previous_target = mode_recall_setpoints_[call.get_mode().value()];
    if (previous_target > 0.0f) {
      ESP_LOGD(TAG, "Loading previous target temp %f", previous_target);
      target_temperature_low = previous_target;
      cmd.target_temperature_degC(target_temperature_low);
    }
  }

  if (call.get_target_temperature_high().has_value()) {
    target_temperature_high = call.get_target_temperature_high().value();

    if (thermostat_) {
      thermostat_->set_cooldry_setpoint(target_temperature_high);
    }

    // If we're in COOL or auto-COOL, set as target
    if (mode == climate::CLIMATE_MODE_COOL ||
        (mode == climate::CLIMATE_MODE_HEAT_COOL && current_temperature > target_temperature_high)) {
      ESP_LOGD(TAG, "Setting target temp to %f, mode is %i", target_temperature_high, mode);
      cmd.target_temperature_degC(target_temperature_high);
    }
  } else if (call.get_mode().has_value() &&
             (mode == climate::CLIMATE_MODE_COOL ||
              (mode == climate::CLIMATE_MODE_HEAT_COOL && current_temperature > target_temperature_high))) {
    // If we didn't get a new target temp, but we did get a mode, use the last known target temp:
    auto previous_target = mode_recall_setpoints_[call.get_mode().value()];
    if (previous_target > 0.0f) {
      ESP_LOGD(TAG, "Loading previous target temp %f", previous_target);
      target_temperature_high = previous_target;
      cmd.target_temperature_degC(target_temperature_high);
    }
  }

  // TODO: Maybe fix this?
  // } else if (call.get_mode().has_value()) {
  //   // If we didn't get a new target temp, but we did get a mode, use the last known target temp:
  //   auto previous_target = mode_recall_setpoints_[call.get_mode().value()];
  //   if (previous_target > 0.0f) {
  //     ESP_LOGD(TAG, "Loading previous target temp %f", previous_target);
  //     target_temperature = previous_target;
  //     cmd.target_temperature_degC(target_temperature);
  //   }
  // }

  // We're assuming that every climate call *does* make some change worth sending to the heat pump
  heatpump_.send_command(cmd);

  // Publish state and any sensor changes (shouldn't be any a result of this function, but
  // since they lazy-publish, no harm in trying)
  do_publish_();
}

bool MitsubishiUART::select_vane_position(const std::string &state) {
  ClimateCommand cmd = ClimateCommand();

  // NOTE: Annoyed that C++ doesn't have switches for strings, but since this is going to be called
  // infrequently, this is probably a better solution than over-optimizing via maps or something

  if (state == "Auto") {
    cmd.vane(SettingsSetRequestPacket::VANE_AUTO);
  } else if (state == "1") {
    cmd.vane(SettingsSetRequestPacket::VANE_1);
  } else if (state == "2") {
    cmd.vane(SettingsSetRequestPacket::VANE_2);
  } else if (state == "3") {
    cmd.vane(SettingsSetRequestPacket::VANE_3);
  } else if (state == "4") {
    cmd.vane(SettingsSetRequestPacket::VANE_4);
  } else if (state == "5") {
    cmd.vane(SettingsSetRequestPacket::VANE_5);
  } else if (state == "Swing") {
    cmd.vane(SettingsSetRequestPacket::VANE_SWING);
  } else {
    ESP_LOGW(TAG, "Unknown vane position %s", state.c_str());
    return false;
  }

  heatpump_.send_command(cmd);

  return true;
}

bool MitsubishiUART::select_horizontal_vane_position(const std::string &state) {
  ClimateCommand cmd = ClimateCommand();

  // NOTE: Annoyed that C++ doesn't have switches for strings, but since this is going to be called
  // infrequently, this is probably a better solution than over-optimizing via maps or something

  if (state == "Auto") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_AUTO);
  } else if (state == "<<") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_LEFT_FULL);
  } else if (state == "<") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_LEFT);
  } else if (state == "|") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_CENTER);
  } else if (state == ">") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_RIGHT);
  } else if (state == ">>") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_RIGHT_FULL);
  } else if (state == "<>") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_SPLIT);
  } else if (state == "Swing") {
    cmd.horizontal_vane(SettingsSetRequestPacket::HV_SWING);
  } else {
    ESP_LOGW(TAG, "Unknown horizontal vane position %s", state.c_str());
    return false;
  }

  heatpump_.send_command(cmd);

  return true;
}

}  // namespace mitsubishi_itp
}  // namespace esphome
