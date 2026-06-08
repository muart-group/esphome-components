#pragma once

namespace esphome {
namespace mitsubishi_itp {

static constexpr char LISTENER_TAG[] = "mitsubishi_itp.listener";

class MITPListener {
 public:
  virtual void publish() = 0;  // Publish only if the underlying state has changed

  virtual void setup(){};  // Called during hub-component setup();
  virtual void using_internal_temperature(const bool using_internal){};
};

}  // namespace mitsubishi_itp
}  // namespace esphome
