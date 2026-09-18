#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "itp_shim.h"

namespace itp_packet {
void itp_log_write(LogLevel level, const char *tag, const char *message) {
  switch (level) {
    case LogLevel::ERROR:
      ESP_LOGE(tag, "%s", message);
      break;
    case LogLevel::WARN:
      ESP_LOGW(tag, "%s", message);
      break;
    case LogLevel::INFO:
      ESP_LOGI(tag, "%s", message);
      break;
    case LogLevel::DEBUG:
      ESP_LOGD(tag, "%s", message);
      break;
    case LogLevel::VERBOSE:
      ESP_LOGV(tag, "%s", message);
      break;
  }
}
uint32_t itp_millis() { return esphome::millis(); }
}  // namespace itp_packet