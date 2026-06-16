#pragma once

#include "esphome/components/switch/switch.h"
#include "../mitsubishi_itp.h"

namespace esphome {
namespace mitsubishi_itp {

class MITPZoneSwitch : public switch_::Switch, public Parented<MitsubishiUART>, public MITPListener {
 public:
  MITPZoneSwitch() = default;
  using Parented<MitsubishiUART>::Parented;

  void set_zone_number(uint8_t zone) { zone_ = zone; }

  void process_packet(const ZoneGetResponsePacket &packet) override {
    zone_state_ = packet.get_zone_active(zone_);
  }

  void publish() override {
    if (zone_state_.has_value() && zone_state_.value() != state) {
      publish_state(zone_state_.value());
    }
  }

 protected:
  void write_state(bool state) override {
    if (parent_->set_zone_active(zone_, state)) {
      zone_state_ = state;
      publish_state(state);
    }
  }

 private:
  uint8_t zone_ = 0;  // Zero-based zone index
  optional<bool> zone_state_;
};

}  // namespace mitsubishi_itp
}  // namespace esphome
