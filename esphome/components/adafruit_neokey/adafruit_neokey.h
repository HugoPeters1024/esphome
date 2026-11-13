#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/i2c/i2c.h"

#include <cstdint>

namespace esphome::adafruit_neokey {

class AdafruitNeoKey : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }
  void set_read_delay(uint32_t delay) { this->read_delay_ms_ = delay; }
  void set_update_interval(uint32_t interval) { this->update_interval_ms_ = interval; }
#ifdef USE_ADAFRUIT_NEOKEY_BINARY_SENSOR
  HUB_PUBLISHER(keys, uint8_t, uint8_t)
  HUB_PUBLISHER(dump, const char *)
#endif

 protected:
  enum class ReadKind : uint8_t { READ_KIND_HARDWARE_ID, READ_KIND_VERSION, READ_KIND_KEYS };
  void reset_();
  void configure_inputs_(uint8_t step);
  void start_read_(ReadKind kind);
  void finish_read_(ReadKind kind);
  void communication_failed_();
  void recover_();
  void publish_keys_(uint32_t gpio);

  uint32_t update_interval_ms_{16};
  uint32_t last_poll_ms_{0};
  uint32_t read_delay_ms_{8};
  uint8_t last_keys_{0};
  uint8_t failed_reads_{0};
  bool ready_{false};
  bool pending_{false};
  bool has_sample_{false};
};

}  // namespace esphome::adafruit_neokey
