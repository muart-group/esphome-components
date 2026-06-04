#pragma once

#include "queue"
#include "esphome/components/uart/uart.h"
#include "esphome/core/helpers.h"
#include <coroutine>
#include <optional>
#include <variant>
#include <expected>
#include "itp_requests.h"
#include "itp_packets.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char HEATPUMP_TAG[] = "mitsubishi_itp.heatpump";

class Heatpump : public ITPPacketReader {
 public:
  Heatpump(uart::UARTComponent *uart_component, HeatpumpSubscriber *subscriber);

  // Called to tick sending queued requests and reading bytes
  void loop();

  // Enqueues a request to be sent to the heatpump
  void enqueue_request(std::unique_ptr<RequestContext> req);

  // Stores last values received from heatpump
  struct HeatpumpState {
    bool connected = false;
    optional<SettingsSetRequestPacket::ModeByte> mode = nullopt;
    optional<SettingsSetRequestPacket::FanByte> fan = nullopt;
  };

 private:
  uart::UARTComponent &uart_comp_;  // UART for Heatpump
  HeatpumpSubscriber &subscriber_;  // Subscriber for heatpump notifications

  std::queue<std::unique_ptr<RequestContext>> request_queue_;
  Task hp_task_;  // Currently running task (for connecting and getting updates)
  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;  // Currently in-flight request to heatpump
  uint32_t update_completed_millis_ = 0;
  uint32_t packet_sent_millis_ = 0;

  HeatpumpState state_;

  void write_raw_packet_(const RawPacket &packet_to_send) const;  // Write out packet to heatpump UART

  Task do_update_queries();  // Creates and enqueues Awaiters, and then processes the results
  Task do_connect();

  // Packet Handling
};

}  // namespace mitsubishi_itp
}  // namespace esphome