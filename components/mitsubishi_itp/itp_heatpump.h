#pragma once

#include "queue"
#include "esphome/components/uart/uart.h"
#include "esphome/core/helpers.h"
#include <coroutine>
#include "itp_packetprocessor.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

struct Task {
  struct promise_type {
    Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };

  std::coroutine_handle<promise_type> handle = nullptr;

  Task(std::coroutine_handle<promise_type> h) : handle(h) {}
  Task() = default;

  // Move constructor: Steals the handle and nulls out the source
  Task(Task &&other) noexcept : handle(other.handle) { other.handle = nullptr; }

  // Move assignment: Destroys any existing frame, steals the new one
  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (handle) {
        handle.destroy();
      }
      handle = other.handle;
      other.handle = nullptr;
    }
    return *this;
  }

  // Prevent copies (they mess with handle pointer)
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;

  bool is_running() const { return handle && !handle.done(); }

  ~Task() {
    if (handle)
      handle.destroy();
  }
};

struct RequestContext {
  Packet request;
  RawPacket response;
  std::coroutine_handle<> handle;

  RequestContext(Packet request) : request(request) {}
};

// "Awaiter" for requests sent to heatpump
// On construction, this will keep a non-owned copy of the context pointer for use on resume,
// and move ownership of the context to the provided queue.
struct RequestAwaiter {
  RequestContext *ctx_ptr;
  std::queue<std::unique_ptr<RequestContext>> &request_queue;

  RequestAwaiter(std::unique_ptr<RequestContext> &&req, std::queue<std::unique_ptr<RequestContext>> &queue)
      : ctx_ptr(req.get()), request_queue(queue) {
    request_queue.push(std::move(req));  // Queue takes ownership
  }

  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> h) { ctx_ptr->handle = h; }

  RawPacket await_resume() {
    ESP_LOGD("itp_heatpump", "Resuming!");
    return std::move(ctx_ptr->response);  // Last use of ctx_ptr before Awaiter is destroyed.
  }
};

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
  Task do_update_queries();
  Task do_connect();
  std::unique_ptr<RequestContext> current_request_ctx_ = nullptr;
};

}  // namespace mitsubishi_itp
}  // namespace esphome