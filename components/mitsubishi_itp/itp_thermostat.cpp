#include "itp_thermostat.h"

namespace esphome {
namespace mitsubishi_itp {

Thermostat::Thermostat(uart::UARTComponent *uart_component, Heatpump *connected_heatpump,
                       ThermostatSubscriber *subscriber)
    : ITPPacketReader(uart_component, "Thermostat"),
      connected_heatpump_{*connected_heatpump},
      subscriber_{*subscriber} {}

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
Task Thermostat::handle_thermostat_request(RawPacket &raw_request_packet) {
  switch (static_cast<PacketType>(raw_request_packet.get_packet_type())) {
    case PacketType::CONNECT_REQUEST:
      return send_to_heatpump<ConnectRequestPacket, ConnectResponsePacket>(raw_request_packet);
      break;
    case PacketType::GET_REQUEST:
      switch (static_cast<GetCommand>(raw_request_packet.get_command())) {
        case GetCommand::SETTINGS:
          return send_to_heatpump<GetRequestPacket, SettingsGetResponsePacket>(raw_request_packet);
      }
    default:
      ESP_LOGI(THERMOSTAT_TAG, "Unexpected thermostat packet type %s/%s",
               format_hex_pretty(raw_request_packet.get_packet_type()).c_str(),
               format_hex_pretty(raw_request_packet.get_command()).c_str());
      return send_to_heatpump<Packet, UnknownPacket>(raw_request_packet);
      break;
  };
}

void Thermostat::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

}  // namespace mitsubishi_itp
}  // namespace esphome