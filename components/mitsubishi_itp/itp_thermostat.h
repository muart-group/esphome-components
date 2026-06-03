#pragma once

#include "queue"
#include "esphome/core/helpers.h"
#include <coroutine>
#include <variant>
#include <expected>
#include "itp_packetprocessor.h"
#include "itp_requests.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char THERMOSTAT_TAG[] = "mitsubishi_itp.thermostat";

class Thermostat : public ITPPacketReader {
 public:
  Thermostat(uart::UARTComponent *uart_component, PacketProcessor *packet_processor);
  void loop();

 protected:
  optional<RawPacket> receive_raw_packet_() const;

 private:
  PacketProcessor &pkt_processor_;
  Task in_flight_request_;  // Packet currently out for processing by MITP/Heatpump

  Task handle_thermostat_request();

  void write_raw_packet_(const RawPacket &packet_to_send) const;

  std::queue<std::unique_ptr<RequestContext>> request_queue_;

  uint32_t update_sent_millis_ = 0;
  uint32_t packet_sent_millis_ = 0;

  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;
};

}  // namespace mitsubishi_itp
}  // namespace esphome