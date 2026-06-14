#pragma once

#include "queue"
#include "esphome/core/helpers.h"
#include <coroutine>
#include <variant>
#include <expected>
#include "itp_requests.h"
#include "itp_heatpump.h"
#include "mitp_mhk.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char THERMOSTAT_TAG[] = "mitsubishi_itp.thermostat";

class Thermostat : public ITPPacketReader {
 public:
  Thermostat(uart::UARTComponent *uart_component, Heatpump *connected_heatpump, ITPSystemState *sys_state);
  void loop();

  void intercept_remote_temperatures(bool do_intercept) { intercept_remote_temp_ = do_intercept; };
  void mhk_fahrenheit_correction(bool do_mhk_f_correction) { mhk_fahrenheit_correction_ = do_mhk_f_correction; };
  void enchanced_mhk(bool enable_enhanced_mhk) { enhanced_mhk_ = enable_enhanced_mhk; };
  void set_epoch_timestamp_source(std::function<time_t()> source_function) { get_epoch_timestamp_ = source_function; };

 protected:
  template<class RequestType, class ResponseType> Task send_to_heatpump(RawPacket &raw_request_packet) {
    std::unique_ptr<RequestContext> req = std::make_unique<RequestContext>(RequestType(std::move(raw_request_packet)));
    ESP_LOGV(THERMOSTAT_TAG, "Receiving from thermostat %s", req->request.to_string().c_str());
    sys_state_.cache_thermostat_packet(static_cast<RequestType *>(&req->request));

    optional<ResponseType> response_pkt =
        co_await RequestAwaiter<ResponseType, Heatpump>(std::move(req), connected_heatpump_);

    if (response_pkt) {
      // If temperature correction is on, adjust temperatures
      if (mhk_fahrenheit_correction_) {
        response_pkt = ResponseType(adjust_mhk_temperature(response_pkt->raw_packet()));
      }

      ESP_LOGV(THERMOSTAT_TAG, "Sending to thermostat %s", response_pkt.value().to_string().c_str());
      write_raw_packet_(response_pkt.value().raw_packet());  // Send to thermostat ASAP
      sys_state_.cache_heatpump_packet(
          *response_pkt);  // Send to SystemState to be cached/forwarded (if it's of the appropriate type)

    } else {
      ESP_LOGW(THERMOSTAT_TAG, "No response to thermostat packet");
    }
  }

 private:
  Heatpump &connected_heatpump_;  // Heat pump responsible for handling incoming packets
  ITPSystemState &sys_state_;
  Task in_flight_request_;  // Packet currently out for processing by MITP/Heatpump

  Task handle_thermostat_request(RawPacket &raw_request_packet);

  Task send_immediately(Packet packet);

  RawPacket adjust_mhk_temperature(RawPacket &raw_pkt);

  void write_raw_packet_(const RawPacket &packet_to_send) const;

  void handle_state_upload(RawPacket &raw_pkt);
  ThermostatStateDownloadResponsePacket get_state_download_response();

  std::queue<std::unique_ptr<RequestContext>> request_queue_;

  uint32_t update_sent_millis_ = 0;
  uint32_t packet_sent_millis_ = 0;

  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;

  bool intercept_remote_temp_ = false;
  bool mhk_fahrenheit_correction_ = false;
  bool enhanced_mhk_ = false;
  std::function<time_t()> get_epoch_timestamp_ = []() {
    ESP_LOGW(THERMOSTAT_TAG, "Time source is not synchronized. Cannot provide accurate time!");
    return 1704067200;  // 2024-01-01 00:00:00Z
  };
  MHKState mhk_state_;
};

}  // namespace mitsubishi_itp
}  // namespace esphome