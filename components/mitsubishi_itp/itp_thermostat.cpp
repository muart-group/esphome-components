#include "itp_thermostat.h"

namespace esphome {
namespace mitsubishi_itp {

Thermostat::Thermostat(uart::UARTComponent *uart_component, Heatpump *connected_heatpump, ITPSystemState *sys_state)
    : ITPPacketReader(uart_component, "Thermostat"), connected_heatpump_{*connected_heatpump}, sys_state_{*sys_state} {}

void Thermostat::loop() {
  if (in_flight_request_.is_running()) {
    // If there is already a request being processed, don't do anything else and just wait for it to return.
    // TODO: This is where we should check to see if the thermostat has sent another packet and cancel the inflight one
  } else {
    if (optional<RawPacket> rp = check_for_packet()) {
      in_flight_request_ = handle_thermostat_request(rp.value());
    }
  }
}

// TODO: This might be a better place to check for cache before sending off to heatpump.
Task Thermostat::handle_thermostat_request(RawPacket &raw_request_packet) {
  // If temperature correction is on, adjust temperatures
  if (mhk_fahrenheit_correction_) {
    raw_request_packet = adjust_mhk_temperature(raw_request_packet);
  }

  switch (static_cast<PacketType>(raw_request_packet.get_packet_type())) {
    case PacketType::CONNECT_REQUEST:
      return send_to_heatpump<ConnectRequestPacket, ConnectResponsePacket>(raw_request_packet);
    case PacketType::IDENTIFY_REQUEST:
      return send_to_heatpump<IdentifyCDRequestPacket, IdentifyCDResponsePacket>(raw_request_packet);
    case PacketType::GET_REQUEST:
      switch (static_cast<GetCommand>(raw_request_packet.get_command())) {
        case GetCommand::SETTINGS:
          return send_to_heatpump<GetRequestPacket, SettingsGetResponsePacket>(raw_request_packet);
        case GetCommand::CURRENT_TEMP:
          return send_to_heatpump<GetRequestPacket, CurrentTempGetResponsePacket>(raw_request_packet);
        case GetCommand::ERROR_INFO:
          return send_to_heatpump<GetRequestPacket, ErrorStateGetResponsePacket>(raw_request_packet);
        case GetCommand::RUN_STATE:
          return send_to_heatpump<GetRequestPacket, RunStateGetResponsePacket>(raw_request_packet);
        case GetCommand::STATUS:
          return send_to_heatpump<GetRequestPacket, StatusGetResponsePacket>(raw_request_packet);
        case GetCommand::FUNCTIONS_1:
          return send_to_heatpump<GetRequestPacket, Functions1GetResponsePacket>(raw_request_packet);
        case GetCommand::FUNCTIONS_2:
          return send_to_heatpump<GetRequestPacket, Functions2GetResponsePacket>(raw_request_packet);
        case GetCommand::ZONE_STATE:
          return send_to_heatpump<GetRequestPacket, ZoneGetResponsePacket>(raw_request_packet);
        case GetCommand::THERMOSTAT_STATE_DOWNLOAD:
          return send_to_heatpump<GetRequestPacket, ThermostatStateDownloadResponsePacket>(raw_request_packet);
        default:
          // Unknown GET_REQUEST goes to default
          goto unknown_packet;
      }
    case PacketType::SET_REQUEST:
      switch (static_cast<SetCommand>(raw_request_packet.get_command())) {
        case SetCommand::REMOTE_TEMPERATURE:
          if (intercept_remote_temp_) {
            // If we're intercepting, cache in incoming packet (to notify MITP for temperature recording)
            sys_state_.cache_thermostat_packet(RemoteTemperatureSetRequestPacket(std::move(raw_request_packet)), true);
            // And immediately return a response without contacting heatpump
            return send_immediately(SetResponsePacket());
          } else {
            return send_to_heatpump<RemoteTemperatureSetRequestPacket, SetResponsePacket>(raw_request_packet);
          }
        case SetCommand::SETTINGS:
          return send_to_heatpump<SettingsSetRequestPacket, SetResponsePacket>(raw_request_packet);
        case SetCommand::THERMOSTAT_SENSOR_STATUS:
          return send_to_heatpump<ThermostatSensorStatusPacket, SetResponsePacket>(raw_request_packet);
        case SetCommand::THERMOSTAT_HELLO:
          return send_to_heatpump<ThermostatHelloPacket, SetResponsePacket>(raw_request_packet);
        case SetCommand::THERMOSTAT_STATE_UPLOAD:
          return send_to_heatpump<ThermostatStateUploadPacket, SetResponsePacket>(raw_request_packet);
        case SetCommand::ZONE_STATE:
          return send_to_heatpump<ZoneSetRequestPacket, SetResponsePacket>(raw_request_packet);
        case SetCommand::THERMOSTAT_SET_AA:
          return send_to_heatpump<ThermostatAASetRequestPacket, SetResponsePacket>(raw_request_packet);
        default:
          // Unknown SET_REQUEST goes to default
          goto unknown_packet;
      }

    default:
    unknown_packet:
      ESP_LOGI(THERMOSTAT_TAG, "Unexpected thermostat packet type %s/%s",
               format_hex_pretty(raw_request_packet.get_packet_type()).c_str(),
               format_hex_pretty(raw_request_packet.get_command()).c_str());
      return send_to_heatpump<Packet, UnknownPacket>(raw_request_packet);
  };
}

Task Thermostat::send_immediately(Packet packet) {
  write_raw_packet_(packet.raw_packet());
  co_return;
}

void Thermostat::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

RawPacket Thermostat::adjust_mhk_temperature(RawPacket &raw_pkt) {
  // Set Remote
  if (raw_pkt.get_packet_type() == static_cast<uint8_t>(PacketType::SET_REQUEST) &&
      raw_pkt.get_command() == static_cast<uint8_t>(SetCommand::REMOTE_TEMPERATURE)) {
    RemoteTemperatureSetRequestPacket temp_pkt = RemoteTemperatureSetRequestPacket(std::move(raw_pkt));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusting MHK temp from %f", temp_pkt.get_remote_temperature());
    temp_pkt.set_remote_temperature(mhk_temp_to_actual(temp_pkt.get_remote_temperature()));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusted MHK temp to %f", temp_pkt.get_remote_temperature());
    return temp_pkt.raw_packet();
  }
  // Set Target
  else if (raw_pkt.get_packet_type() == static_cast<uint8_t>(PacketType::SET_REQUEST) &&
           raw_pkt.get_command() == static_cast<uint8_t>(SetCommand::SETTINGS)) {
    SettingsSetRequestPacket temp_pkt = SettingsSetRequestPacket(std::move(raw_pkt));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusting MHK temp from %f", temp_pkt.get_target_temp());
    temp_pkt.set_target_temperature(mhk_temp_to_actual(temp_pkt.get_target_temp()));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusted MHK temp to %f", temp_pkt.get_target_temp());
    return temp_pkt.raw_packet();
  }
  // Get Current
  else if (raw_pkt.get_packet_type() == static_cast<uint8_t>(PacketType::GET_RESPONSE) &&
           raw_pkt.get_command() == static_cast<uint8_t>(GetCommand::CURRENT_TEMP)) {
    CurrentTempGetResponsePacket temp_pkt = CurrentTempGetResponsePacket(std::move(raw_pkt));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusting MHK temp from %f", temp_pkt.get_current_temp());
    temp_pkt.set_current_temperature(mhk_temp_from_actual(temp_pkt.get_current_temp()));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusted MHK temp to %f", temp_pkt.get_current_temp());
    return temp_pkt.raw_packet();
  }
  // Get Target
  else if (raw_pkt.get_packet_type() == static_cast<uint8_t>(PacketType::GET_RESPONSE) &&
           raw_pkt.get_command() == static_cast<uint8_t>(SetCommand::SETTINGS)) {
    SettingsGetResponsePacket temp_pkt = SettingsGetResponsePacket(std::move(raw_pkt));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusting MHK temp from %f", temp_pkt.get_target_temp());
    temp_pkt.set_target_temperature(mhk_temp_from_actual(temp_pkt.get_target_temp()));
    ESP_LOGV(THERMOSTAT_TAG, "Adjusted MHK temp to %f", temp_pkt.get_target_temp());
    return temp_pkt.raw_packet();
  } else {
    return raw_pkt;
  }
}

}  // namespace mitsubishi_itp
}  // namespace esphome