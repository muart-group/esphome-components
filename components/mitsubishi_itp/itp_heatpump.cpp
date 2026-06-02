#include "itp_heatpump.h"

namespace esphome {
namespace mitsubishi_itp {

Heatpump::Heatpump(uart::UARTComponent *uart_component, PacketProcessor *packet_processor)
    : uart_comp_{*uart_component}, pkt_processor_{*packet_processor} {
  // update_task_ = do_connect();
}

void Heatpump::loop() {
  if (!update_task_.is_running() && millis() - update_sent_millis_ > 16000) {
    ESP_LOGD("itp_heatpump", "Starting new update_task");
    update_task_ = do_update_queries();
  }

  if (current_request_ctx_) {
    // If there's a request in-flight, but it's been too long, timeout
    if (millis() - packet_sent_millis_ > 1000) {
      current_request_ctx_->response = Response{.err = "Timed out waiting for packet"};
      current_request_ctx_->handle.resume();
      current_request_ctx_ = nullptr;
    }

    // Otherwise, try to read a response packet
    else if (optional<RawPacket> pkt = receive_raw_packet_()) {
      // If we get a packet, read it into the response and resume the awaiter
      current_request_ctx_->response = Response{.raw_packet = pkt.value(), .err = ""};
      current_request_ctx_->handle.resume();
      current_request_ctx_ = nullptr;
    }
    // If we don't get one and haven't timed out, we'll keep waiting...
  } else if (!request_queue_.empty()) {
    ESP_LOGD("itp_heatpump", "Queue not empty");
    // Otherwise if there's a request in the queue, pop and send.
    current_request_ctx_ = std::move(request_queue_.front());
    request_queue_.pop();  // Pop empty pointer (we're holding it in current_request_ctx_ now)

    write_raw_packet_(current_request_ctx_->request.raw_packet());
    packet_sent_millis_ = millis();
  }
}

Task Heatpump::do_update_queries() {
  ESP_LOGD("itp_heatpump", "Doing update!");
  update_sent_millis_ = millis();

  std::unique_ptr<RequestContext> req = std::make_unique<RequestContext>(GetRequestPacket::get_status_instance());
  Response response = co_await RequestAwaiter(std::move(req), request_queue_);

  if (response.raw_packet) {
    // TODO: Make sure it's the right packet
    ESP_LOGD("itp_heatpump", "Got response");
    ESP_LOGD("itp_heatpump", "Got response type %i", response.raw_packet.value().get_packet_type());
    StatusGetResponsePacket rp = StatusGetResponsePacket(std::move(response.raw_packet.value()));
    pkt_processor_.process_packet(rp);
  } else {
    ESP_LOGW("itp_heatpump", "Error receiving packet: {}", response.err);
  }
}

// Task Heatpump::do_connect() {
//   ESP_LOGD("itp_heatpump", "Doing update!");
//   update_sent_millis_ = millis();
//   RawPacket response =
//       co_await RequestAwaiter{.to_send = ConnectRequestPacket::instance(), .pkt_queue = request_queue_};
//   // TODO: Make sure it's the right packet
//   ESP_LOGD("itp_heatpump", "Got response type %i", response.get_packet_type());
//   ConnectResponsePacket rp = ConnectResponsePacket(std::move(response));
//   pkt_processor_.process_packet(rp);
// }

/* Reads and deserializes a packet from UART.
Communication with heatpump is *slow*, so we need to check and make sure there are
enough packets available before we start reading.  If there aren't enough packets,
no packet will be returned.

Even at 2400 baud, the 100ms readtimeout should be enough to read a whole payload
after the first byte has been received though, so currently we're assuming that once
the header is available, it's safe to call read_array without timing out and severing
the packet.
*/
optional<RawPacket> Heatpump::receive_raw_packet_() const {
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
  uart_comp_.read_array(&packet_bytes[PACKET_HEADER_SIZE], payload_size + 1);

  return RawPacket(packet_bytes, PACKET_HEADER_SIZE + payload_size + 1);
}

void Heatpump::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

}  // namespace mitsubishi_itp
}  // namespace esphome