#include "mitp_mhk.h"
#include <map>

namespace esphome {
namespace mitsubishi_itp {

// Keys in this map are actual temperatures in Celcius. Values are the arbitrary "Celcius" temperatures that, once the
// MHK's creative math is applied, will produce the closest Fahrenheit temperature to the true Celcius temperature on
// the MHK's display. This map only contains the temperatures where Mitsubishi has gotten creative with the conversion.
// If a temperature doesn't appear here, then the MHK should show the right Fahrenheit temperature given the actual
// Celcius temperature.
static const std::map<float, float> mhk_rewrite_map_ = {
    {12.0, 12.5}, {18.0, 17.5}, {18.5, 18.0}, {19.0, 18.5}, {19.5, 19.0}, {20.5, 21.0},
    {21.0, 21.5}, {21.5, 22.0}, {22.0, 22.5}, {28.0, 27.5}, {28.5, 28.0}, {29.0, 28.5},
    {29.5, 29.0}, {30.0, 29.5}, {30.5, 30.0}, {31.0, 30.5}, {32.0, 32.5}};

// Take a real temperature in Celcius and adjust it so that the MHK display will show the closest corresponding
// temperature in whole degrees Fahrenheit. This will actually output a slightly different Celcius value since the
// conversion happens internally in the MHK.
float mhk_temp_from_actual(float actual_c) {
  if (std::isnan(actual_c)) {
    return NAN;
  }

  auto match = mhk_rewrite_map_.find(actual_c);
  return match == mhk_rewrite_map_.end() ? actual_c : match->second;
}

// Take a temperature in Celcius that came from the MHK and adjust it so that it corresponds as closely as possible to
// the actual Fahrenheit value shown on the MHK display.
float mhk_temp_to_actual(float mhk_c) {
  if (std::isnan(mhk_c)) {
    return NAN;
  }

  for (auto iter = mhk_rewrite_map_.begin(); iter != mhk_rewrite_map_.end(); ++iter) {
    if (iter->second == mhk_c) {
      return iter->first;
    }
  }
  return mhk_c;
}

}  // namespace mitsubishi_itp
}  // namespace esphome
