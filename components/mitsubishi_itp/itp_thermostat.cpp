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

// TODO: Keep filling all these in
// TODO: This might be a better place to check for cache before sending off to heatpump.
Task Thermostat::handle_thermostat_request(RawPacket &raw_request_packet) {
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

}  // namespace mitsubishi_itp
}  // namespace esphome