#pragma once

#include <cmath>

namespace esphome {
namespace mitsubishi_itp {

/// A struct that represents the connected MHK's state for management and synchronization purposes.
struct MHKState {
  float cool_setpoint = NAN;
  float heat_setpoint = NAN;
};

float mhk_temp_from_actual(float actual_c);
float mhk_temp_to_actual(float mhk_c);

}  // namespace mitsubishi_itp
}  // namespace esphome
