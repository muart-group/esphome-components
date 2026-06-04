#include "itp_heatpump.h"

namespace esphome {
namespace mitsubishi_itp {

Heatpump::Heatpump(uart::UARTComponent *uart_component, PacketProcessor *packet_processor)
    : ITPPacketReader(uart_component, "Heatpump"), uart_comp_{*uart_component}, pkt_processor_{*packet_processor} {
  // update_task_ = do_connect();
}

void Heatpump::loop() {
  if (!update_task_.is_running() && millis() - update_sent_millis_ > 16000) {
    ESP_LOGD(HEATPUMP_TAG, "Starting new update_task");
    update_task_ = do_update_queries();
  }

  if (current_request_ctx_) {
    // If there's a request in-flight, but it's been too long, timeout
    if (millis() - packet_sent_millis_ > 1000) {
      ESP_LOGW(HEATPUMP_TAG, "Timed out waiting for packet!");
      current_request_ctx_->handle.resume();
      current_request_ctx_ = nullptr;
    }

    // Otherwise, try to read a response packet
    else if (optional<RawPacket> pkt = check_for_packet()) {
      // If we get a packet, read it into the response and resume the awaiter
      current_request_ctx_->raw_response = pkt;
      current_request_ctx_->handle.resume();
      current_request_ctx_ = nullptr;
    }
    // If we don't get one and haven't timed out, we'll keep waiting...
  } else if (!request_queue_.empty()) {
    // Otherwise if there's a request in the queue, pop and send.
    current_request_ctx_ = std::move(request_queue_.front());
    request_queue_.pop();  // Pop empty pointer (we're holding it in current_request_ctx_ now)

    write_raw_packet_(current_request_ctx_->request.raw_packet());
    packet_sent_millis_ = millis();
  }
}

Task Heatpump::do_update_queries() {
  ESP_LOGD(HEATPUMP_TAG, "Doing update!");
  update_sent_millis_ = millis();

  std::unique_ptr<RequestContext> req = std::make_unique<RequestContext>(GetRequestPacket::get_status_instance());
  optional<StatusGetResponsePacket> status_pkt =
      co_await RequestAwaiter<StatusGetResponsePacket>(std::move(req), request_queue_);

  if (status_pkt) {
    // TODO: Make sure it's the right packet
    ESP_LOGD(HEATPUMP_TAG, "Got response");
    ESP_LOGD(HEATPUMP_TAG, "Got response type %i", status_pkt.value().get_packet_type());
    pkt_processor_.process_packet(status_pkt.value());
  } else {
    ESP_LOGW(HEATPUMP_TAG, "No status packet received!");
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

void Heatpump::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

}  // namespace mitsubishi_itp
}  // namespace esphome