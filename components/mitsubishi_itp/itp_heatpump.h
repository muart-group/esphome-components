#pragma once

#include "queue"
#include "esphome/components/uart/uart.h"
#include "esphome/core/helpers.h"
#include <coroutine>
#include <variant>
#include <expected>
#include "itp_packetprocessor.h"
#include "itp_requests.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char HEATPUMP_TAG[] = "mitsubishi_itp.heatpump";

class Heatpump {
 public:
  Heatpump(uart::UARTComponent *uart_component, PacketProcessor *packet_processor);
  void loop();

 protected:
  optional<RawPacket> receive_raw_packet_() const;

 private:
  void write_raw_packet_(const RawPacket &packet_to_send) const;
  uart::UARTComponent &uart_comp_;
  PacketProcessor &pkt_processor_;
  std::queue<std::unique_ptr<RequestContext>> request_queue_;
  Task update_task_;
  uint32_t update_sent_millis_ = 0;
  uint32_t packet_sent_millis_ = 0;
  Task do_update_queries();
  Task do_connect();
  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;
};

}  // namespace mitsubishi_itp
}  // namespace esphome