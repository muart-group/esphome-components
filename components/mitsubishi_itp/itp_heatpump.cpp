#include "itp_heatpump.h"

namespace esphome {
namespace mitsubishi_itp {

Heatpump::Heatpump(uart::UARTComponent *uart_component, ITPSystemState *sys_state)
    : ITPPacketReader(uart_component, "Heatpump"), uart_comp_{*uart_component}, sys_state_{*sys_state} {}

void Heatpump::loop() {
  // If we're disconnected try to connect
  // If we're connected, periodically ask for updates
  if (!connected_ && !hp_task_.is_running()) {
    hp_task_ = do_connect();
  } else if (connected_ && !hp_task_.is_running() && millis() - update_completed_millis_ > 16000) {
    ESP_LOGD(HEATPUMP_TAG, "Starting new update_task");
    hp_task_ = do_update_queries();
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

void Heatpump::enqueue_request(std::unique_ptr<RequestContext> req) { request_queue_.push(std::move(req)); }

Task Heatpump::do_connect() {
  // Send connect packet
  std::unique_ptr<RequestContext> connect_req = std::make_unique<RequestContext>(ConnectRequestPacket::instance());
  optional<ConnectResponsePacket> connect_res =
      co_await RequestAwaiter<ConnectResponsePacket, Heatpump>(std::move(connect_req), *this);

  if (connect_res) {
    connected_ = true;  // Connected!

    // Once we're connected, try once to discover
    std::unique_ptr<RequestContext> disc_req = std::make_unique<RequestContext>(CapabilitiesRequestPacket::instance());
    optional<CapabilitiesResponsePacket> disc_res =
        co_await RequestAwaiter<CapabilitiesResponsePacket, Heatpump>(std::move(disc_req), *this);

    if (disc_res) {
      ESP_LOGV(HEATPUMP_TAG, "Received %s", disc_res->to_string().c_str());
      sys_state_.cache_heatpump_packet(disc_res);
    } else {
      ESP_LOGI(HEATPUMP_TAG, "Capability packets not supported.");
    }
  }
}

Task Heatpump::do_update_queries() {
  ESP_LOGD(HEATPUMP_TAG, "Doing update!");

  // Runstate
  std::unique_ptr<RequestContext> runstate_req =
      std::make_unique<RequestContext>(GetRequestPacket::get_runstate_instance());
  // Check cache first
  optional<RunStateGetResponsePacket> runstate_res = sys_state_.check_heatpump_cache<RunStateGetResponsePacket>();
  // If not in cache, try requesting from heatpump
  if (!runstate_res) {
    runstate_res = co_await RequestAwaiter<RunStateGetResponsePacket, Heatpump>(std::move(runstate_req), *this);
  }
  // If we received it, cache it (cache will notify subscribed receivers)
  if (runstate_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", runstate_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(runstate_res);
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Runstate Packet not recevied!");
  }

  // Settings & Status processed together for mode logic to work
  std::unique_ptr<RequestContext> settings_req =
      std::make_unique<RequestContext>(GetRequestPacket::get_settings_instance());
  optional<SettingsGetResponsePacket> settings_res =
      co_await RequestAwaiter<SettingsGetResponsePacket, Heatpump>(std::move(settings_req), *this);

  std::unique_ptr<RequestContext> status_req =
      std::make_unique<RequestContext>(GetRequestPacket::get_status_instance());
  optional<StatusGetResponsePacket> status_res =
      co_await RequestAwaiter<StatusGetResponsePacket, Heatpump>(std::move(status_req), *this);

  if (settings_res && status_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", settings_res->to_string().c_str());
    ESP_LOGV(HEATPUMP_TAG, "Received %s", status_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(settings_res);
    sys_state_.cache_heatpump_packet(status_res);
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Settings/Status Packet not recevied!");
  }

  // Current temp
  std::unique_ptr<RequestContext> temp_req =
      std::make_unique<RequestContext>(GetRequestPacket::get_current_temp_instance());
  optional<CurrentTempGetResponsePacket> temp_res =
      co_await RequestAwaiter<CurrentTempGetResponsePacket, Heatpump>(std::move(temp_req), *this);
  if (temp_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", temp_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(temp_res);
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Current Temperature Packet not recevied!");
  }

  // Error Info
  std::unique_ptr<RequestContext> error_req =
      std::make_unique<RequestContext>(GetRequestPacket::get_error_info_instance());
  optional<ErrorStateGetResponsePacket> error_res =
      co_await RequestAwaiter<ErrorStateGetResponsePacket, Heatpump>(std::move(error_req), *this);
  if (error_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", error_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(error_res);
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Error Info Packet not recevied!");
  }

  // Zones (may not work on all units)
  // TODO: Add zone support setting to avoid these timeouts
  std::unique_ptr<RequestContext> zone_req = std::make_unique<RequestContext>(GetRequestPacket::get_zone_instance());
  optional<ZoneGetResponsePacket> zone_res =
      co_await RequestAwaiter<ZoneGetResponsePacket, Heatpump>(std::move(zone_req), *this);
  if (zone_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", zone_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(zone_res);
  } else {
    ESP_LOGI(HEATPUMP_TAG, "Zone info packet not received (may not be supported).");
  }

  update_completed_millis_ = millis();
}

void Heatpump::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

}  // namespace mitsubishi_itp
}  // namespace esphome