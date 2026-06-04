#include "itp_requests.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

// Reads bytes into packet_buffer_ and when a whole packet is available, returns it.
// Note: If packet data is incomplete, eventually a second packet will arrive, be read as the (corrupt)
// contents of the first packet and rejected. The remainder of the second packet will be drained
// and packet three should be read as usual
optional<RawPacket> ITPPacketReader::check_for_packet() {
  if (buffer_position_ == 0) {
    // If we have no bytes yet, just read bytes until we get a control byte
    packet_buffer_[0] = 0;  // Reset control byte from last packet
    while (uart_comp_.available() >= PACKET_HEADER_SIZE && uart_comp_.read_byte(&packet_buffer_[buffer_position_])) {
      if (packet_buffer_[0] == BYTE_CONTROL) {
        buffer_position_++;
        break;
      }
    }
  }

  if (buffer_position_ == 1) {
    // We waited to have a headers-worth of bytes, so go ahead and read them now (shouldn't need to wait here)
    uart_comp_.read_array(&packet_buffer_[1], PACKET_HEADER_SIZE - 1);
    buffer_position_ += (PACKET_HEADER_SIZE - 1);
  }

  if (buffer_position_ == PACKET_HEADER_SIZE &&
      uart_comp_.available() >= packet_buffer_[PACKET_HEADER_INDEX_PAYLOAD_LENGTH] + 1) {
    // The rest of the packet has arrived, read it in (plus the checksum)
    uart_comp_.read_array(&packet_buffer_[PACKET_HEADER_SIZE], packet_buffer_[PACKET_HEADER_INDEX_PAYLOAD_LENGTH] + 1);
    auto rp = RawPacket(packet_buffer_, PACKET_HEADER_SIZE + packet_buffer_[PACKET_HEADER_INDEX_PAYLOAD_LENGTH] + 1);
    ESP_LOGD(REQUESTS_TAG, "Received %s packet on %s.", format_hex_pretty(rp.get_packet_type()).c_str(), log_name_);

    if (rp.is_checksum_valid()) {
      buffer_position_ = 0;  // Reset buffer
      return rp;
    } else {
      ESP_LOGW(REQUESTS_TAG, "Invalid packet checksum for %s", rp.to_string().c_str());
      buffer_position_ = 0;  // Reset buffer
      return nullopt;
    }
  }

  return nullopt;  // No packet yet
}

}  // namespace mitsubishi_itp
}  // namespace esphome