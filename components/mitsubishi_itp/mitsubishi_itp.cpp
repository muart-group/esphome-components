#include "mitsubishi_itp.h"

namespace esphome {
namespace mitsubishi_itp {

////
// MitsubishiUART
////

MitsubishiUART::MitsubishiUART(uart::UARTComponent *hp_uart_comp)
    : hp_uart_{*hp_uart_comp}, hp_uart_byte_(hp_uart_comp), heatpump_(&hp_uart_byte_, &itp_sys_state_) {
  /**
   * Climate pushes all its data to Home Assistant immediately when the API connects, this causes
   * the default 0 to be sent as temperatures, but since this is a valid value (0 deg C), it
   * can cause confusion and mess with graphs when looking at the state in HA.  Setting this to
   * NAN gets HA to treat this value as "unavailable" until we have a real value to publish.
   */
  target_temperature = NAN;
  current_temperature = NAN;

  // We do support this!
  climate_traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_TWO_POINT_TARGET_TEMPERATURE);

  target_temperature_high = 28.0;

  // Register to receive heatpump and thermostat packets
  itp_sys_state_.register_receiver(this);
}

// This value should be changed if the structure of the preferences object changes
// to invalidate previously stored preferences.
const uint MITP_PREFERENCE_VERSION = 2;

// Used to restore state of previous MITP-specific settings (like temperature source or pass-thru mode)
// Most other climate-state is preserved by the heatpump itself and will be retrieved after connection
void MitsubishiUART::setup() {
  // Set Heatpump update interval to same as ESPHome update interval (because Heatpump interval is
  // *between* updates, this will drift, but that's fine, everything will be synced within 2x the interval)
  heatpump_.set_update_interval(this->update_interval_);

  for (auto *listener : listeners_) {
    listener->setup();
  }
  preferences_ = this->make_entity_preference<MITPPreferences>(MITP_PREFERENCE_VERSION);
  restore_preferences_();

#ifdef USE_TIME
  this->time_source_->add_on_time_sync_callback([this] {
    this->time_sync_ = true;
    if (this->thermostat_) {
      this->thermostat_->set_timestruct_source(std::bind(&MitsubishiUART::get_timestruct, this));
    }
  });
#endif
}

void MitsubishiUART::restore_preferences_() {
  MITPPreferences prefs;
  if (preferences_.load(&prefs)) {
    if (!std::isnan(prefs.last_cool_setpoint)) {
      target_temperature_high = prefs.last_cool_setpoint;
      if (thermostat_) {
        thermostat_->set_cooldry_setpoint(target_temperature_high);
      }
    }

    if (!std::isnan(prefs.last_heat_setpoint)) {
      target_temperature_low = prefs.last_heat_setpoint;
      if (thermostat_) {
        thermostat_->set_heat_setpoint(target_temperature_low);
      }
    }

    ESP_LOGCONFIG(TAG, "Loaded previous setpoints. Low:%f High:%f", target_temperature_low, target_temperature_high);

    do_publish_();  // Publish right away so HA shows temperatures
  }
}

void MitsubishiUART::save_preferences_() {
  MITPPreferences prefs{};
  prefs.last_cool_setpoint = target_temperature_high;
  prefs.last_heat_setpoint = target_temperature_low;
  preferences_.save(&prefs);
}

// /* Calls the appropriate loop() functions on Heatpump and Thermostat to read bytes from the UART devices.
// Also tracks temperature reporting timeout to switch back to Internal source
// */
void MitsubishiUART::loop() {
  heatpump_.loop();
  if (thermostat_) {
    thermostat_->loop();
  }

  // If we're not on timeout and not on Internal
  if (!temperature_source_timeout_ && selected_temperature_source_ != TEMPERATURE_SOURCE_INTERNAL) {
    // if it's been too long since we got a report for our current selected source
    if (millis() - temperature_reports_[selected_temperature_source_].timestamp > temperature_source_timeout_ms_) {
      // Alert user and set heatpump to internal
      ESP_LOGW(TAG, "No temperature received from %s for %lu milliseconds, reverting to Internal source",
               selected_temperature_source_.c_str(), (unsigned long) temperature_source_timeout_ms_);
      // Let listeners know we've changed to the Internal temperature source (but do not change
      // selected_temperature_source)
      alert_listeners_internal_temp_(true);
      temperature_source_timeout_ = true;
      // Send a packet to the heat pump to tell it to switch to internal temperature sensing
      heatpump_.use_internal_temperature();
    } else if (temperature_source_echo_ms_ > 0 &&
               millis() - temperature_source_echo_last_timestamp_ > temperature_source_echo_ms_) {
      // If we haven't timed out, and an echo is set, check and send the last temperature for the selected source
      if (!isnan(temperature_reports_[selected_temperature_source_].temperature)) {
        ESP_LOGD(TAG, "Echoing last received temperature");
        heatpump_.set_remote_temperature(temperature_reports_[selected_temperature_source_].temperature);
        temperature_source_echo_last_timestamp_ = millis();
      }
    }
  }
}

void MitsubishiUART::dump_config() {
  optional<CapabilitiesResponsePacket> opt_cap = itp_sys_state_.check_heatpump_cache<CapabilitiesResponsePacket>();
  if (opt_cap) {
    ESP_LOGCONFIG(TAG, "Discovered Capabilities: %s", opt_cap.value().to_string().c_str());
  }

  // if ()) {
  //   ESP_LOGCONFIG(TAG, "MHK Enhanced Protocol Mode is ENABLED! This is currently *experimental* and things may
  //   break!");
  // }
}

// Set thermostat UART component
void MitsubishiUART::set_thermostat_uart(uart::UARTComponent *uart) {
  ESP_LOGCONFIG(TAG, "Thermostat uart was set.");
  ts_uart_ = uart;
  ts_uart_byte_ = UARTComponentByteProvider(uart);
  thermostat_ = make_unique<Thermostat>(&ts_uart_byte_, &heatpump_, &itp_sys_state_);

  thermostat_->intercept_remote_temperatures(true);  // MITP will be handling all remote temperatures
}

/* Called periodically as PollingComponent; used to send packets to connect or request updates.

Possible TODO: If we only publish during updates, since data is received during loop, updates will always
be about `update_interval` late from their actual time.  Generally the update interval should be low enough
(default is 5seconds) this won't pose a practical problem.
*/
void MitsubishiUART::update() {
  // Notify all listeners a publish is happening, they will decide if actual publish is needed.
  for (auto *listener : listeners_) {
    listener->publish();
  }

  // If any climate values have changed, publish
  if (publish_on_update_) {
    do_publish_();

    publish_on_update_ = false;
  }
}

void MitsubishiUART::do_publish_() {
  publish_state();
  // We can safely do this on every publish as ESPPreferences collects changes and only writes if different
  save_preferences_();
}

bool MitsubishiUART::select_temperature_source(const std::string &state) {
  selected_temperature_source_ = state;

  // If we've switched to internal, let the HP know right away
  if (TEMPERATURE_SOURCE_INTERNAL == state) {
    alert_listeners_internal_temp_(true);
    heatpump_.use_internal_temperature();
  } else {
    // If we have a fresh temperature already, go ahead and send it immediately.
    if (millis() - temperature_reports_[selected_temperature_source_].timestamp < temperature_source_timeout_ms_ &&
        !isnan(temperature_reports_[selected_temperature_source_].temperature)) {
      heatpump_.set_remote_temperature(temperature_reports_[selected_temperature_source_].temperature);
      alert_listeners_internal_temp_(false);
    } else {
      // Otherwise, reset that report so it doesn't immediately timeout
      temperature_reports_[selected_temperature_source_].timestamp = millis();
      temperature_reports_[selected_temperature_source_].temperature = NAN;
    }
  }

  return true;
}

// Called by temperature_source sensors, and packetprocessing to report new temperature values. Only
// sends temperature information on to heat pump if it matches the current selected_temperature_source
void MitsubishiUART::temperature_source_report(const std::string &temperature_source, const float &v) {
  ESP_LOGI(TAG, "Received temperature from %s of %f. (Current source: %s)", temperature_source.c_str(), v,
           selected_temperature_source_.c_str());

  if (isnan(v) || v >= 63.5 || v <= -64.0) {
    ESP_LOGW(TAG, "Temperature %f from %s is out of range and will be ignored.", v, temperature_source.c_str());
    return;
  }

  // Record report in map
  temperature_reports_[temperature_source].temperature = v;
  temperature_reports_[temperature_source].timestamp = millis();

  for (const auto &pair : temperature_reports_) {
    ESP_LOGD(TAG, "%s: %f , %lus ago", pair.first.c_str(), pair.second.temperature,
             (millis() - pair.second.timestamp) / 1000);
  }

  // Only proceed if the incomming source matches our chosen source.
  if (selected_temperature_source_ == temperature_source) {
    // Reset the timeout for received temperature
    temperature_source_timeout_ = false;

    // Tell the heat pump about the temperature asap, but don't worry about setting it locally, the next update() will
    // get it
    heatpump_.set_remote_temperature(v);

    // If we're using echos, update the last sent so the echo waits properly
    if (temperature_source_echo_ms_ > 0) {
      temperature_source_echo_last_timestamp_ = millis();
    }

    // If we've sent a remote temperature, we're not using the internal one
    alert_listeners_internal_temp_(false);
  }
}

bool MitsubishiUART::set_zone_active(uint8_t zone, bool active) {
  ESP_LOGI(TAG, "Setting zone %d to %s", zone + 1, active ? "ON" : "OFF");
  return heatpump_.set_zone_active(zone, active);
}

void MitsubishiUART::reset_filter_status() {
  ESP_LOGI(TAG, "Received a request to reset the filter status.");

  heatpump_.reset_filter();
}

tm MitsubishiUART::get_timestruct() {
  if (this->time_sync_) {
    esphome::ESPTime now = this->time_source_->now();
    return tm{
        .tm_sec = now.second,
        .tm_min = now.minute,
        .tm_hour = now.hour,
        .tm_mday = now.day_of_month,
        .tm_mon = now.month - 1,
        .tm_year = now.year - 1900,

    };
  } else {
    ESP_LOGW(TAG, "Time source is not synchronized. Cannot provide accurate time!");
    return tm{.tm_mday = 1, .tm_mon = 0, .tm_year = 124};  // 2024-01-01 00:00:00Z
  }
}

}  // namespace mitsubishi_itp
}  // namespace esphome
