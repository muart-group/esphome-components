#include "mitp_select.h"

namespace esphome {
namespace mitsubishi_itp {

void TemperatureSourceSelect::publish() {
  // Only publish if force, or a change has occurred and we have a real value
  if (mitp_select_value_.has_value() && mitp_select_value_.value() != current_option()) {
    publish_state(mitp_select_value_.value());
    if (active_index().has_value()) {
      size_t index = active_index().value();
      preferences_.save(&index);
    }
  }
}

void TemperatureSourceSelect::setup() {
  // Using App.get_build_time_string() means these will get reset each time the firmware is updated, but this
  // is an easy way to prevent wierd conflicts if e.g. select options change.
  char build_time_buffer[26];
  App.get_build_time_string(build_time_buffer);
  this->preferences_ = this->make_entity_preference<size_t>();

  size_t saved_index;
  if (this->preferences_.load(&saved_index) && has_index(saved_index) && at(saved_index).has_value()) {
    control(at(saved_index).value());
  } else {
    control(TEMPERATURE_SOURCE_INTERNAL);  // Set to internal if no preferences loaded.
  }
}

void TemperatureSourceSelect::control(const std::string &value) {
  if (parent_->select_temperature_source(value)) {
    mitp_select_value_ = value;
    publish();
  }
}

}  // namespace mitsubishi_itp
}  // namespace esphome
