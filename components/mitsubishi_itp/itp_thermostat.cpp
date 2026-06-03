#include "itp_thermostat.h"

namespace esphome {
namespace mitsubishi_itp {

Thermostat::Thermostat(uart::UARTComponent *uart_component, PacketProcessor *packet_processor)
    : ITPPacketReader(uart_component, "Thermostat"), pkt_processor_{*packet_processor} {}

void Thermostat::loop() {
  if (in_flight_request_.is_running()) {
    // If there is already a request being processed, don't do anything else and just wait for it to return.
    // TODO: This is where we should check to see if the thermostat has sent another packet and cancel the inflight one
  } else {
    if (optional<RawPacket> rp = check_for_packet()) {
      ESP_LOGD(THERMOSTAT_TAG, "Got a thermostat packet!");
    }
  }
}

/* Reads and deserializes a packet from UART.
Communication with heatpump is *slow*, so we need to check and make sure there are
enough packets available before we start reading.  If there aren't enough packets,
no packet will be returned.

Even at 2400 baud, the 100ms readtimeout should be enough to read a whole payload
after the first byte has been received though, so currently we're assuming that once
the header is available, it's safe to call read_array without timing out and severing
the packet.
*/
// TODO: Move this into loop to be less-blocking (only read if enough are available)
optional<RawPacket> Thermostat::receive_raw_packet_() const {
  uint8_t packet_bytes[PACKET_MAX_SIZE];
  packet_bytes[0] = 0;  // Reset control byte before starting

  // Drain UART until we see a control byte (times out after 100ms in UARTComponent)
  while (uart_comp_.available() >= PACKET_HEADER_SIZE && uart_comp_.read_byte(&packet_bytes[0])) {
    if (packet_bytes[0] == BYTE_CONTROL)
      break;
    // TODO: If the serial is all garbage, this may never stop-- we should have our own timeout
  }

  // If we never found a control byte, we didn't receive a packet
  if (packet_bytes[0] != BYTE_CONTROL) {
    return nullopt;
  }

  // Read the header
  uart_comp_.read_array(&packet_bytes[1], PACKET_HEADER_SIZE - 1);

  // Read payload + checksum
  uint8_t payload_size = packet_bytes[PACKET_HEADER_INDEX_PAYLOAD_LENGTH];
  auto start = millis();
  uart_comp_.read_array(&packet_bytes[PACKET_HEADER_SIZE], payload_size + 1);
  auto done = millis();
  ESP_LOGD(THERMOSTAT_TAG, "Took %i ms", done - start);

  return RawPacket(packet_bytes, PACKET_HEADER_SIZE + payload_size + 1);
}

void Thermostat::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

}  // namespace mitsubishi_itp
}  // namespace esphome