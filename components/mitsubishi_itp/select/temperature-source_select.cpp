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

// This value should be changed if the structure of the preferences object changes
// to invalidate previously stored preferences.
const uint TEMP_SOURCE_SELECT_PREFERENCE_VERSION = 1;

void TemperatureSourceSelect::setup() {
  this->preferences_ = this->make_entity_preference<size_t>(TEMP_SOURCE_SELECT_PREFERENCE_VERSION);

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
