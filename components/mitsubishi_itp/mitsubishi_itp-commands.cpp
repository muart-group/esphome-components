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
      cmd.fan_speed(SettingsSetRequestPacket::FAN_4);
    }
  } else if (call.get_fan_mode().has_value()) {
    switch (call.get_fan_mode().value()) {
      case climate::CLIMATE_FAN_QUIET:
        set_fan_mode_(climate::CLIMATE_FAN_QUIET);
        cmd.fan_speed(SettingsSetRequestPacket::FAN_QUIET);
        break;
      case climate::CLIMATE_FAN_LOW:
        set_fan_mode_(climate::CLIMATE_FAN_LOW);
        cmd.fan_speed(SettingsSetRequestPacket::FAN_1);
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
        cmd.fan_speed(SettingsSetRequestPacket::FAN_2);
        break;
      case climate::CLIMATE_FAN_HIGH:
        set_fan_mode_(climate::CLIMATE_FAN_HIGH);
        cmd.fan_speed(SettingsSetRequestPacket::FAN_3);
        break;
      case climate::CLIMATE_FAN_AUTO:
        set_fan_mode_(climate::CLIMATE_FAN_AUTO);
        cmd.fan_speed(SettingsSetRequestPacket::FAN_AUTO);
        break;
      default:
        ESP_LOGW(TAG, "Unhandled fan mode %i!", call.get_fan_mode().value());
        break;
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
      if (thermostat_) {
        thermostat_->set_heat_setpoint(target_temperature_low);
      }
    } else {
      // In the event that they *both* changed, we'll still just bump the high temperature because trying to take the
      // mean or something might be weird.
      target_temperature_high = target_temperature_low + 2;
      if (thermostat_) {
        thermostat_->set_cooldry_setpoint(target_temperature_high);
      }
    }
  }

  // Mode

  if (call.get_mode().has_value()) {
    mode = call.get_mode().value();

    // Tell thermostat if we're in auto mode or not
    if (mode == climate::CLIMATE_MODE_HEAT_COOL) {
      if (thermostat_) {
        thermostat_->set_auto_mode(0x01);  // 0x01 for now (is 0x02 the other of heat vs cool?)
      }
    } else {
      if (thermostat_) {
        thermostat_->set_auto_mode(0x00);
      }
    }
  }

  /* Based on our mode (just updated above), send the appropriate command.
     Because we're tracking two targets now, any mode change potentially requires
     a target command in case the underlying mode switched on the heat pump. Set point
     changes only *require* a mode command in some HEAT_COOL circumstances, but for consistency
     since the command may or may not have also contained a mode switch, we'll set mode
     every time.
  */

  switch (mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      // If we've just changed to (or already were in) HEAT_COOL and the
      // current_temperature calls for immediate action, do that.
      if (current_temperature >= target_temperature_high) {
        cmd.power(true)
          .mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_COOL)
          .target_temperature_degC(target_temperature_high);
      } else if (current_temperature <= target_temperature_low) {
        cmd.power(true)
          .mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_HEAT)
          .target_temperature_degC(target_temperature_low);
      } else {
        // Current temperature is somewhere in between setpoints so current heat
        // pump mode (and therefore target) is undefined

        // If it's currently on heat or cool, just reiterate the current mode
        // and pass on the appropriate (maybe just updated) target. NOTE: This could potentially
        // read a value from an "OFF" heatpump since it retains its mode, and it would resume
        // in whatever mode it was last in rather than using the midpoint.
        if (itp_sys_state_.is_heatpump_on_cool()){
          cmd.power(true)
            .mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_COOL)
            .target_temperature_degC(target_temperature_high);
        } else if (itp_sys_state_.is_heatpump_on_heat()) {
          cmd.power(true)
            .mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_HEAT)
            .target_temperature_degC(target_temperature_low);
        } else {
          // Otherwise set the mode and target based on the midpoint
          const float target_temperature_midpoint = (target_temperature_low + target_temperature_high)/2;
          if (current_temperature >= target_temperature_midpoint) {
            cmd.power(true)
              .mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_COOL)
              .target_temperature_degC(target_temperature_high);
          } else {
            cmd.power(true)
            .mode(itp_packet::SettingsSetRequestPacket::ModeByte::MODE_BYTE_HEAT)
            .target_temperature_degC(target_temperature_low);
          }
        }
      }
      break;
    case climate::CLIMATE_MODE_COOL:
      cmd.power(true)
        .mode(SettingsSetRequestPacket::MODE_BYTE_COOL)
        .target_temperature_degC(target_temperature_high);
      break;
    case climate::CLIMATE_MODE_HEAT:
      cmd.power(true)
        .mode(SettingsSetRequestPacket::MODE_BYTE_HEAT)
        .target_temperature_degC(target_temperature_low);
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      cmd.power(true).mode(SettingsSetRequestPacket::MODE_BYTE_FAN);
      // Don't change target as its a noop in this mode
      break;
    case climate::CLIMATE_MODE_DRY:
      cmd.power(true)
        .mode(SettingsSetRequestPacket::MODE_BYTE_DRY)
        .target_temperature_degC(target_temperature_high);
      break;
    case climate::CLIMATE_MODE_OFF:
    default:
      cmd.power(false);
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
