#pragma once

#include "esphome/core/helpers.h"
#include "itp_packet.h"
#include <coroutine>
#include <queue>

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

// Provides an object to receive/manage the coroutine_handle, and check to see if coroutine is still running
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

// Context for requests sent to heat pump (allows passing request/result in and out of coroutine)
struct RequestContext {
  Packet request;
  optional<RawPacket> raw_response;
  std::coroutine_handle<> handle;

  RequestContext(Packet request) : request(request) {}
};

// "Awaiter" for requests sent to heatpump
// On construction, this will keep a non-owned copy of the context pointer for use on resume,
// and move ownership of the context to the provided queue.
template<class PType> struct RequestAwaiter {
  static_assert(std::is_base_of_v<Packet, PType>, "PType must derive from Packet");
  RequestContext *ctx_ptr;
  std::queue<std::unique_ptr<RequestContext>> &request_queue;

  RequestAwaiter(std::unique_ptr<RequestContext> &&req, std::queue<std::unique_ptr<RequestContext>> &queue)
      : ctx_ptr(req.get()), request_queue(queue) {
    request_queue.push(std::move(req));  // Queue takes ownership
  }

  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> h) { ctx_ptr->handle = h; }

  optional<PType> await_resume() {
    ESP_LOGD(TAG, "Resuming!");
    if (ctx_ptr->raw_response) {
      return PType(std::move(ctx_ptr->raw_response.value()));  // Last use of ctx_ptr before Awaiter is destroyed.
    }
    return nullopt;
  }
};

}  // namespace mitsubishi_itp
}  // namespace esphome
