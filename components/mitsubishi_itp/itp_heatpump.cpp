#include "itp_heatpump.h"

namespace esphome {
namespace mitsubishi_itp {

Task ClimateCommand::send(Heatpump &target) {
  SettingsSetRequestPacket set_request_packet = SettingsSetRequestPacket();
  if (fan_speed_) {
    set_request_packet.set_fan(*fan_speed_);
  }
  if (power_) {
    set_request_packet.set_power(*power_);
  }
  if (mode_) {
    set_request_packet.set_mode(*mode_);
  }
  if (target_temperature_degC_) {
    set_request_packet.set_target_temperature(*target_temperature_degC_);
  }
  if (vane_) {
    set_request_packet.set_vane(*vane_);
  }
  if (horizontal_vane_) {
    set_request_packet.set_horizontal_vane(*horizontal_vane_);
  }

  std::unique_ptr<RequestContext> req = std::make_unique<RequestContext>(set_request_packet);

  optional<SetResponsePacket> response_pkt =
      co_await RequestAwaiter<SetResponsePacket, Heatpump>(std::move(req), target);

  if (!response_pkt) {
    ESP_LOGW(HEATPUMP_TAG, "No response from set request!");
  }
}

Heatpump::Heatpump(uart::UARTComponent *uart_component, ITPSystemState *sys_state)
    : ITPPacketReader(uart_component, "Heatpump"), uart_comp_{*uart_component}, sys_state_{*sys_state} {}

void Heatpump::loop() {
  // If we're disconnected try to connect
  // If we're connected, periodically ask for updates
  if (!connected_ && !hp_task_.is_running()) {
    hp_task_ = do_connect();
  } else if (connected_ && !hp_task_.is_running() && millis() - update_completed_millis_ > update_interval_ms_) {
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
    sys_state_.set_connected(true);

    // Once we're connected, try once to discover
    std::unique_ptr<RequestContext> disc_req = std::make_unique<RequestContext>(CapabilitiesRequestPacket::instance());
    optional<CapabilitiesResponsePacket> disc_res =
        co_await RequestAwaiter<CapabilitiesResponsePacket, Heatpump>(std::move(disc_req), *this);

    if (disc_res) {
      ESP_LOGV(HEATPUMP_TAG, "Received %s", disc_res->to_string().c_str());
      sys_state_.cache_heatpump_packet(*disc_res);
    } else {
      ESP_LOGI(HEATPUMP_TAG, "Capability packets not supported.");
    }
  }
}

Task Heatpump::do_update_queries() {
  // Check cache first
  optional<RunStateGetResponsePacket> runstate_res = sys_state_.check_heatpump_cache<RunStateGetResponsePacket>();
  // If not in cache, try requesting from heatpump
  if (!runstate_res) {
    // Runstate
    std::unique_ptr<RequestContext> runstate_req =
        std::make_unique<RequestContext>(GetRequestPacket::get_runstate_instance());
    runstate_res = co_await RequestAwaiter<RunStateGetResponsePacket, Heatpump>(std::move(runstate_req), *this);
  }
  // If we received it, cache it (cache will notify subscribed receivers)
  if (runstate_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", runstate_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(runstate_res.value());
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Runstate Packet not recevied!");
  }

  // Settings & Status processed together for mode logic to work
  optional<SettingsGetResponsePacket> settings_res = sys_state_.check_heatpump_cache<SettingsGetResponsePacket>();
  if (!settings_res) {
    std::unique_ptr<RequestContext> settings_req =
        std::make_unique<RequestContext>(GetRequestPacket::get_settings_instance());
    settings_res = co_await RequestAwaiter<SettingsGetResponsePacket, Heatpump>(std::move(settings_req), *this);
  }

  optional<StatusGetResponsePacket> status_res = sys_state_.check_heatpump_cache<StatusGetResponsePacket>();
  if (!status_res) {
    std::unique_ptr<RequestContext> status_req =
        std::make_unique<RequestContext>(GetRequestPacket::get_status_instance());
    status_res = co_await RequestAwaiter<StatusGetResponsePacket, Heatpump>(std::move(status_req), *this);
  }

  if (settings_res && status_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", settings_res->to_string().c_str());
    ESP_LOGV(HEATPUMP_TAG, "Received %s", status_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(settings_res.value());
    sys_state_.cache_heatpump_packet(status_res.value());
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Settings/Status Packet not recevied!");
  }

  // Current temp
  optional<CurrentTempGetResponsePacket> temp_res = sys_state_.check_heatpump_cache<CurrentTempGetResponsePacket>();
  if (!temp_res) {
    std::unique_ptr<RequestContext> temp_req =
        std::make_unique<RequestContext>(GetRequestPacket::get_current_temp_instance());
    temp_res = co_await RequestAwaiter<CurrentTempGetResponsePacket, Heatpump>(std::move(temp_req), *this);
  }

  if (temp_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", temp_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(temp_res.value());
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Current Temperature Packet not recevied!");
  }

  // Error Info
  optional<ErrorStateGetResponsePacket> error_res = sys_state_.check_heatpump_cache<ErrorStateGetResponsePacket>();
  if (!error_res) {
    std::unique_ptr<RequestContext> error_req =
        std::make_unique<RequestContext>(GetRequestPacket::get_error_info_instance());
    error_res = co_await RequestAwaiter<ErrorStateGetResponsePacket, Heatpump>(std::move(error_req), *this);
  }

  if (error_res) {
    ESP_LOGV(HEATPUMP_TAG, "Received %s", error_res->to_string().c_str());
    sys_state_.cache_heatpump_packet(error_res.value());
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Error Info Packet not recevied!");
  }

  // Zones (may not work on all units)
  if (zones_enabled_) {
    optional<ZoneGetResponsePacket> zone_res = sys_state_.check_heatpump_cache<ZoneGetResponsePacket>();
    if (!zone_res) {
      std::unique_ptr<RequestContext> zone_req =
          std::make_unique<RequestContext>(GetRequestPacket::get_zone_instance());
      zone_res = co_await RequestAwaiter<ZoneGetResponsePacket, Heatpump>(std::move(zone_req), *this);
    }
    if (zone_res) {
      ESP_LOGV(HEATPUMP_TAG, "Received %s", zone_res->to_string().c_str());
      sys_state_.cache_heatpump_packet(zone_res.value());
    } else {
      ESP_LOGI(HEATPUMP_TAG, "Zone info packet not received (may not be supported).");
    }
  }

  update_completed_millis_ = millis();
}

void Heatpump::write_raw_packet_(const RawPacket &packet_to_send) const {
  uart_comp_.write_array(packet_to_send.get_bytes(), packet_to_send.get_length());
}

bool Heatpump::check_command_queue_() {
  // Clear finished tasks
  std::erase_if(command_tasks_, [](const Task &t) { return !t.is_running(); });
  return command_tasks_.size() < MAX_INFLIGHT_COMMANDS;
}

bool Heatpump::send_command(ClimateCommand cmd) {
  if (check_command_queue_()) {
    command_tasks_.push_back(cmd.send(*this));
    return true;
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Command task queue full!");
    return false;
  }
}

bool Heatpump::reset_filter() {
  if (check_command_queue_()) {
    SetRunStatePacket set_packet = SetRunStatePacket();
    set_packet.set_filter_reset(true);
    command_tasks_.push_back(enqueue_packet<SetResponsePacket>(set_packet));
    return true;
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Command task queue full!");
    return false;
  }
}

bool Heatpump::set_remote_temperature(float degC) {
  if (check_command_queue_()) {
    RemoteTemperatureSetRequestPacket set_packet = RemoteTemperatureSetRequestPacket();
    set_packet.set_remote_temperature(degC);
    command_tasks_.push_back(enqueue_packet<SetResponsePacket>(set_packet));
    return true;
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Command task queue full!");
    return false;
  }
}

bool Heatpump::use_internal_temperature(bool use_internal) {
  if (check_command_queue_()) {
    RemoteTemperatureSetRequestPacket set_packet = RemoteTemperatureSetRequestPacket();
    set_packet.set_use_internal_temperature(use_internal);
    command_tasks_.push_back(enqueue_packet<SetResponsePacket>(set_packet));
    return true;
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Command task queue full!");
    return false;
  }
}

bool Heatpump::set_zone_active(uint8_t zone, bool active) {
  if (check_command_queue_()) {
    ZoneSetRequestPacket set_packet = ZoneSetRequestPacket();
    set_packet.set_zone_active(zone, active);
    command_tasks_.push_back(enqueue_packet<SetResponsePacket>(set_packet));
    return true;
  } else {
    ESP_LOGW(HEATPUMP_TAG, "Command task queue full!");
    return false;
  }
}

}  // namespace mitsubishi_itp
}  // namespace esphome