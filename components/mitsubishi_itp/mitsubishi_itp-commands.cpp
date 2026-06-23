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

    // Tell thermostat if we're in auto mode or not
    if (mode == climate::CLIMATE_MODE_HEAT_COOL) {
      if (thermostat_) {
        thermostat_->set_auto_mode(0x01);  // 0x01 for now (is 0x02 the other of heat vs cool?)
      }
    } else {
      if (thermostat_) {
        thermostat_->set_auto_mode(0x00);  // 0x01 for now (is 0x02 the other of heat vs cool?)
      }
    }
  }

  // Target Temperature

  // Home Assistant tends to send both low and high targets even if only one has changed, so we need to track which
  // changed to bump the other if they're too close
  bool low_changed = false;
  bool high_changed = false;

  // Set the low/high target temperatures
  if (call.get_target_temperature_low().has_value()) {
    low_changed = call.get_target_temperature_low().value() != target_temperature_low;
    target_temperature_low = call.get_target_temperature_low().value();
    if (thermostat_) {
      thermostat_->set_heat_setpoint(target_temperature_low);
    }
  }

  if (call.get_target_temperature_high().has_value()) {
    high_changed = call.get_target_temperature_high().value() != target_temperature_high;
    target_temperature_high = call.get_target_temperature_high().value();
    if (thermostat_) {
      thermostat_->set_cooldry_setpoint(target_temperature_high);
    }
  }

  if (target_temperature_high - target_temperature_low < 2) {
    ITP_LOGW(TAG, "Target temperatures must be at least 2°C apart!");
    if (high_changed) {
      target_temperature_low = target_temperature_high - 2;
    } else {
      // In the event that they *both* changed, we'll still just bump the high temperature because trying to take the
      // mean or something might be weird.
      target_temperature_high = target_temperature_low + 2;
    }
  }

  // Based on our mode (just updated above), set the target temperature on the heatpump

  switch (mode) {
    case climate::CLIMATE_MODE_COOL:
    case climate::CLIMATE_MODE_DRY:
      cmd.target_temperature_degC(target_temperature_high);
      break;
    case climate::CLIMATE_MODE_HEAT:
      cmd.target_temperature_degC(target_temperature_low);
      break;
    case climate::CLIMATE_MODE_HEAT_COOL:
      if (current_temperature <= target_temperature_low) {
        cmd.target_temperature_degC(target_temperature_low);
      } else {
        cmd.target_temperature_degC(target_temperature_high);
      }
      break;
    default:
      // Other modes don't use target temperature, just leave it where it was
      break;
  }

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
