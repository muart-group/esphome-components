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
#include "itp_systemstate.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char HEATPUMP_TAG[] = "mitsubishi_itp.heatpump";

const size_t MAX_INFLIGHT_COMMANDS = 4;

class Heatpump;

class ClimateCommand {
 public:
  ClimateCommand(){};
  Task send(Heatpump &target);

  ClimateCommand &fanSpeed(SettingsSetRequestPacket::FanByte fan_speed) {
    fan_speed_ = fan_speed;
    return *this;
  }

  ClimateCommand &power(bool power_on) {
    power_ = power_on;
    return *this;
  }

  ClimateCommand &mode(SettingsSetRequestPacket::ModeByte mode) {
    mode_ = mode;
    return *this;
  }

  ClimateCommand &target_temperature_degC(float target_temperature_degC) {
    target_temperature_degC_ = target_temperature_degC;
    return *this;
  }

  ClimateCommand &vane(SettingsSetRequestPacket::VaneByte vane) {
    vane_ = vane;
    return *this;
  }

  ClimateCommand &horizontal_vane(SettingsSetRequestPacket::HorizontalVaneByte horizontal_vane) {
    horizontal_vane_ = horizontal_vane;
    return *this;
  }

 private:
  optional<SettingsSetRequestPacket::FanByte> fan_speed_ = nullopt;
  optional<bool> power_ = nullopt;
  optional<SettingsSetRequestPacket::ModeByte> mode_ = nullopt;
  optional<float> target_temperature_degC_ = nullopt;
  optional<SettingsSetRequestPacket::VaneByte> vane_ = nullopt;
  optional<SettingsSetRequestPacket::HorizontalVaneByte> horizontal_vane_ = nullopt;
};

class Heatpump : public ITPPacketReader {
 public:
  Heatpump(uart::UARTComponent *uart_component, ITPSystemState *sys_state);

  // Called to tick sending queued requests and reading bytes
  void loop();

  // Enqueues a request to be sent to the heatpump
  void enqueue_request(std::unique_ptr<RequestContext> req);

  void enable_zones(bool enable = true) { zones_enabled_ = enable; };

  // Checks to see if there is room in command_tasks_ then executes comand and stores the Task
  bool send_command(ClimateCommand command);

  bool reset_filter();

  bool set_remote_temperature(float degC);
  bool use_internal_temperature(bool use_internal = true);
  bool set_zone_active(uint8_t zone, bool active = true);

 private:
  uart::UARTComponent &uart_comp_;  // UART for Heatpump
  ITPSystemState &sys_state_;       // System cache / notifier

  std::queue<std::unique_ptr<RequestContext>> request_queue_;
  Task hp_task_;  // Currently running task (for connecting and getting updates)
  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;  // Currently in-flight request to heatpump
  uint32_t update_completed_millis_ = 0;
  uint32_t packet_sent_millis_ = 0;

  bool connected_ = false;

  bool zones_enabled_ = false;

  void write_raw_packet_(const RawPacket &packet_to_send) const;  // Write out packet to heatpump UART

  Task do_update_queries();  // Creates and enqueues Awaiters, and then processes the results
  Task do_connect();

  // Tasks for currently-in-flight-commands (held so that the coroutine frame lives)
  std::vector<Task> command_tasks_;
  // Returns true if command_tasks_ is smaller than MAX_INFLIGHT_COMMANDS
  bool check_command_queue_();

  // Creates the appropriate coroutine structure for a packet and enqueues it
  template<class ResponsePacket> Task enqueue_packet(Packet packet) {
    std::unique_ptr<RequestContext> req = std::make_unique<RequestContext>(packet);

    optional<ResponsePacket> response_pkt = co_await RequestAwaiter<ResponsePacket, Heatpump>(std::move(req), *this);
  };
};

}  // namespace mitsubishi_itp
}  // namespace esphome