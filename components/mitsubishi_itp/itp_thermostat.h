#pragma once

#include "queue"
#include "esphome/core/helpers.h"
#include <coroutine>
#include <variant>
#include <expected>
#include "itp_packetprocessor.h"
#include "itp_requests.h"
#include "itp_heatpump.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char THERMOSTAT_TAG[] = "mitsubishi_itp.thermostat";

class Thermostat : public ITPPacketReader {
 public:
  Thermostat(uart::UARTComponent *uart_component, Heatpump *connected_heatpump, ThermostatSubscriber *subscriber);
  void loop();

 protected:
  template<class RequestType, class ResponseType> Task send_to_heatpump(RawPacket &raw_request_packet) {
    std::unique_ptr<RequestContext> req = std::make_unique<RequestContext>(RequestType(std::move(raw_request_packet)));
    ESP_LOGV(THERMOSTAT_TAG, "Receiving thermostat packet %s", req->request.to_string().c_str());

    optional<ResponseType> response_pkt =
        co_await RequestAwaiter<ResponseType, Heatpump>(std::move(req), connected_heatpump_);

    if (response_pkt) {
      ESP_LOGV(THERMOSTAT_TAG, "Sending thermostat packet %s", response_pkt.value().to_string().c_str());
      write_raw_packet_(response_pkt.value().raw_packet());
    } else {
      ESP_LOGW(THERMOSTAT_TAG, "No response to thermostat packet");
    }
  }

 private:
  Heatpump &connected_heatpump_;  // Heat pump responsible for handling incoming packets
  ThermostatSubscriber &subscriber_;
  Task in_flight_request_;  // Packet currently out for processing by MITP/Heatpump

  Task handle_thermostat_request(RawPacket &raw_request_packet);

  void write_raw_packet_(const RawPacket &packet_to_send) const;

  std::queue<std::unique_ptr<RequestContext>> request_queue_;

  uint32_t update_sent_millis_ = 0;
  uint32_t packet_sent_millis_ = 0;

  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;
};

}  // namespace mitsubishi_itp
}  // namespace esphome