#include "adafruit_neokey.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::adafruit_neokey {

ESPHOME_LOG_TAG(TAG, "adafruit_neokey");
static constexpr uint32_t INIT_TIMEOUT_ID = 0;
static constexpr uint32_t READ_TIMEOUT_ID = 1;
static constexpr uint32_t RETRY_TIMEOUT_ID = 2;
// Seesaw addresses are a module byte followed by a function byte.
static constexpr uint8_t STATUS = 0x00;
static constexpr uint8_t STATUS_HW_ID = 0x01;
static constexpr uint8_t STATUS_VERSION = 0x02;
static constexpr uint8_t STATUS_RESET = 0x7F;
static constexpr uint8_t GPIO = 0x01;
static constexpr uint8_t GPIO_DIRCLR = 0x03;
static constexpr uint8_t GPIO_BULK = 0x04;
static constexpr uint8_t GPIO_SET = 0x05;
static constexpr uint8_t GPIO_INTENCLR = 0x09;
static constexpr uint8_t GPIO_PULLENSET = 0x0B;
static constexpr uint8_t KEY_MASK = 0xF0;  // NeoKey pins 4..7; not ESP GPIO numbers.
static constexpr uint8_t SAMD09_HW_ID = 0x55;

void AdafruitNeoKey::setup() {
  ESP_LOGCONFIG(TAG, "Setting up NeoKey 1x4 at 0x%02X", this->address_);
  this->reset_();
}

void AdafruitNeoKey::reset_() {
  this->ready_ = false;
  this->pending_ = false;
  const uint8_t command[]{STATUS, STATUS_RESET, 0xFF};
  if (this->write(command, sizeof(command)) != i2c::ERROR_OK) {
    this->communication_failed_();
    return;
  }
  // CircuitPython uses 500 ms after reset. Keep the main loop free during boot.
  this->set_timeout(INIT_TIMEOUT_ID, 500, [this]() { this->start_read_(ReadKind::READ_KIND_HARDWARE_ID); });
}

void AdafruitNeoKey::start_read_(ReadKind kind) {
  if (this->pending_)
    return;
  const uint8_t command[]{kind == ReadKind::READ_KIND_KEYS ? GPIO : STATUS,
                          kind == ReadKind::READ_KIND_KEYS
                              ? GPIO_BULK
                              : (kind == ReadKind::READ_KIND_HARDWARE_ID ? STATUS_HW_ID : STATUS_VERSION)};
  // Send a STOP, then wait before a separate read. A repeated-start register read is insufficient.
  if (this->write(command, sizeof(command)) != i2c::ERROR_OK) {
    this->communication_failed_();
    return;
  }
  this->pending_ = true;
  this->set_timeout(READ_TIMEOUT_ID, this->read_delay_ms_, [this, kind]() { this->finish_read_(kind); });
}

void AdafruitNeoKey::finish_read_(ReadKind kind) {
  uint8_t data[4]{};
  this->pending_ = false;
  if (this->read(data, kind == ReadKind::READ_KIND_HARDWARE_ID ? 1 : 4) != i2c::ERROR_OK) {
    this->communication_failed_();
    return;
  }
  switch (kind) {
    case ReadKind::READ_KIND_HARDWARE_ID:
      if (data[0] != SAMD09_HW_ID) {
        ESP_LOGW(TAG, "0x%02X: expected NeoKey SAMD09 ID 0x55, received 0x%02X", this->address_, data[0]);
        this->status_set_warning();
        this->recover_();
        return;
      }
      this->start_read_(ReadKind::READ_KIND_VERSION);
      break;
    case ReadKind::READ_KIND_VERSION: {
      const uint32_t version = encode_uint32(data[0], data[1], data[2], data[3]);
      // Product 4980 is the NeoKey 1x4, not another Seesaw GPIO board.
      if ((version >> 16) != 4980) {
        ESP_LOGW(TAG, "0x%02X: unexpected product ID %u", this->address_, static_cast<unsigned>(version >> 16));
        this->status_set_warning();
        this->recover_();
        return;
      }
      ESP_LOGI(TAG, "0x%02X: firmware 0x%08X", this->address_, static_cast<unsigned>(version));
      this->configure_inputs_(0);
      break;
    }
    case ReadKind::READ_KIND_KEYS:
      this->failed_reads_ = 0;
      this->status_clear_warning();
      this->publish_keys_(encode_uint32(data[0], data[1], data[2], data[3]));
      break;
  }
}

void AdafruitNeoKey::configure_inputs_(uint8_t step) {
  // Big-endian 32-bit masks. Configure input + pull-up as in Adafruit's driver.
  static constexpr uint8_t REGISTERS[] PROGMEM{GPIO_DIRCLR, GPIO_PULLENSET, GPIO_SET, GPIO_INTENCLR};
  if (step == sizeof(REGISTERS)) {
    this->ready_ = true;
    this->last_poll_ms_ = App.get_loop_component_start_time();
    this->start_read_(ReadKind::READ_KIND_KEYS);
    return;
  }
  const uint8_t command[]{GPIO, progmem_read_byte(&REGISTERS[step]), 0x00, 0x00, 0x00, KEY_MASK};
  if (this->write(command, sizeof(command)) != i2c::ERROR_OK) {
    this->communication_failed_();
    return;
  }
  this->set_timeout(INIT_TIMEOUT_ID, 1, [this, step]() { this->configure_inputs_(step + 1); });
}

void AdafruitNeoKey::loop() {
  const uint32_t now = App.get_loop_component_start_time();
  if (this->ready_ && !this->pending_ && now - this->last_poll_ms_ >= this->update_interval_ms_) {
    this->last_poll_ms_ = now;
    this->start_read_(ReadKind::READ_KIND_KEYS);
  }
}

void AdafruitNeoKey::publish_keys_(uint32_t gpio) {
  const uint8_t pressed = static_cast<uint8_t>((~gpio & KEY_MASK) >> 4);
  const uint8_t changed = this->has_sample_ ? (pressed ^ this->last_keys_) : 0x0F;
  for (uint8_t key = 0; key < 4; key++) {
    if ((changed & (1U << key)) != 0 && this->key_sensors_[key] != nullptr)
      this->key_sensors_[key]->publish_state((pressed & (1U << key)) != 0);
  }

  this->last_keys_ = pressed;
  this->has_sample_ = true;
}

void AdafruitNeoKey::communication_failed_() {
  if (!this->status_has_warning()) {
    ESP_LOGW(TAG, "0x%02X: I2C communication failed; keeping last valid key states", this->address_);
  }
  this->status_set_warning();
  if (!this->ready_ || ++this->failed_reads_ >= 3)
    this->recover_();
}

void AdafruitNeoKey::recover_() {
  this->ready_ = false;
  this->pending_ = false;
  this->failed_reads_ = 0;
  this->cancel_timeout(READ_TIMEOUT_ID);
  this->cancel_timeout(INIT_TIMEOUT_ID);
  this->set_timeout(RETRY_TIMEOUT_ID, 5000, [this]() { this->reset_(); });
}

void AdafruitNeoKey::dump_config() {
  ESP_LOGCONFIG(TAG, "NeoKey 1x4:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", static_cast<unsigned>(this->update_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Read delay: %u ms", static_cast<unsigned>(this->read_delay_ms_));
  ESP_LOGCONFIG(TAG, "  Keys 1-4 use NeoKey internal pins 4-7 (active low)");
  for (auto *sensor : this->key_sensors_) {
    LOG_BINARY_SENSOR("  ", "Key", sensor);
  }
}

}  // namespace esphome::adafruit_neokey
