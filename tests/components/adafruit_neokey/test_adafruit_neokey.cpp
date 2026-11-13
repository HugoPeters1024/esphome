#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

#include "esphome/components/adafruit_neokey/adafruit_neokey.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome::adafruit_neokey::testing {
namespace {

std::vector<std::pair<uint8_t, uint8_t>> snapshots;
std::vector<std::pair<uint8_t, uint8_t>> second_snapshots;

class FakeBus : public i2c::I2CBus {
 public:
  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *data, size_t size, uint8_t *out,
                             size_t out_size) override {
    EXPECT_EQ(address, this->address);
    // Selecting a Seesaw register and reading it must be separate transactions.
    EXPECT_FALSE(size != 0 && out_size != 0);
    if (size != 0) {
      this->writes.emplace_back(data, data + size);
      if (this->fail_writes)
        return i2c::ERROR_NOT_ACKNOWLEDGED;
      if (size == 2) {
        this->selected = (uint16_t(data[0]) << 8) | data[1];
        this->selected_at = micros();
      }
    }
    if (out_size != 0) {
      this->reads++;
      EXPECT_GE(micros() - this->selected_at, 1000U);
      switch (this->selected) {
        case 0x0001:
          EXPECT_EQ(out_size, 1U);
          out[0] = this->hardware_id;
          break;
        case 0x0002:
          EXPECT_EQ(out_size, 4U);
          put_be32(out, this->version);
          break;
        case 0x0104:
          EXPECT_EQ(out_size, 4U);
          // Unrelated high bits must not become key states.
          put_be32(out, 0xABCDEF00U | ((~this->pressed & 0x0FU) << 4));
          break;
        default:
          ADD_FAILURE() << "Unexpected register " << this->selected;
      }
      // Deliberately fill the buffer even on error: it must not be published.
      if (this->fail_reads)
        return i2c::ERROR_TIMEOUT;
    }
    return i2c::ERROR_OK;
  }

  uint8_t address{0x30};
  uint8_t hardware_id{0x55};
  uint32_t version{0x13742AA4};
  uint8_t pressed{0};
  bool fail_reads{false};
  bool fail_writes{false};
  unsigned reads{0};
  uint16_t selected{0};
  uint32_t selected_at{0};
  std::vector<std::vector<uint8_t>> writes;
};

class TestNeoKey : public AdafruitNeoKey {
 public:
  bool ready() const { return this->ready_; }
  void prepare() { this->ready_ = true; }
  void restart() {
    this->cancel_timeout(uint32_t{2});
    this->reset_();
  }
  void stop() {
    for (uint32_t id = 0; id < 3; id++)
      this->cancel_timeout(id);
  }
};

void run_scheduler(uint32_t duration) {
  const uint32_t started = millis();
  do {
    delay(2);  // NOLINT
    App.scheduler.call(millis());
  } while (millis() - started < duration);
}

class NeoKeyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    snapshots.clear();
    second_snapshots.clear();
    this->board_.set_i2c_bus(&this->bus_);
    this->board_.set_i2c_address(0x30);
    this->board_.set_read_delay(1);
    this->board_.set_keys_publisher([](uint8_t pressed, uint8_t changed) { snapshots.emplace_back(pressed, changed); });
  }
  void TearDown() override { this->board_.stop(); }
  void loop_at(uint32_t now) {
    LoopBlockingGuard guard{nullptr, nullptr, now};
    this->board_.loop();
  }
  void sample(uint8_t pressed) {
    this->bus_.pressed = pressed;
    this->now_ += 16;
    this->loop_at(this->now_);
    run_scheduler(3);
  }

  FakeBus bus_;
  TestNeoKey board_;
  uint32_t now_{1000};
};

TEST_F(NeoKeyTest, InitializesInputsWithBigEndianMasksAndPublishesHeldKeys) {
  this->bus_.pressed = 5;
  this->board_.setup();
  ASSERT_EQ(this->bus_.writes.size(), 1U);
  EXPECT_EQ(this->bus_.writes.front(), (std::vector<uint8_t>{0, 0x7F, 0xFF}));
  run_scheduler(30);
  EXPECT_EQ(this->bus_.writes.size(), 1U);  // Reset must not be followed immediately by register selection.
  run_scheduler(520);
  ASSERT_TRUE(this->board_.ready());
  ASSERT_EQ(this->bus_.writes.size(), 8U);
  EXPECT_EQ(this->bus_.writes[1], (std::vector<uint8_t>{0, 1}));
  EXPECT_EQ(this->bus_.writes[2], (std::vector<uint8_t>{0, 2}));
  const std::array<uint8_t, 4> registers{3, 0x0B, 5, 9};
  for (size_t i = 0; i < registers.size(); i++)
    EXPECT_EQ(this->bus_.writes[i + 3], (std::vector<uint8_t>{1, registers[i], 0, 0, 0, 0xF0}));
  EXPECT_EQ(snapshots, (std::vector<std::pair<uint8_t, uint8_t>>{{5, 15}}));
}

TEST_F(NeoKeyTest, AllKeyCombinationsAndHeldKeys) {
  this->board_.prepare();
  uint8_t previous = 0;
  for (uint8_t pressed = 0; pressed < 16; pressed++) {
    this->sample(pressed);
    ASSERT_EQ(snapshots.size(), pressed + 1U);
    EXPECT_EQ(snapshots.back(),
              (std::pair<uint8_t, uint8_t>{pressed, pressed == 0 ? uint8_t{15} : uint8_t(pressed ^ previous)}));
    this->sample(pressed);
    EXPECT_EQ(snapshots.size(), pressed + 1U);
    previous = pressed;
  }
}

TEST_F(NeoKeyTest, PendingReadAndPollingIntervalDoNotOverwriteSelection) {
  this->board_.prepare();
  this->board_.set_update_interval(50);
  this->loop_at(100);
  ASSERT_EQ(this->bus_.writes.size(), 1U);
  this->loop_at(200);  // Still waiting for the response.
  EXPECT_EQ(this->bus_.writes.size(), 1U);
  run_scheduler(3);
  this->loop_at(149);
  EXPECT_EQ(this->bus_.writes.size(), 1U);
  this->loop_at(150);
  EXPECT_EQ(this->bus_.writes.size(), 2U);
  run_scheduler(3);
}

TEST_F(NeoKeyTest, FailedSelectionDoesNotReadOrPublish) {
  this->board_.prepare();
  this->sample(0);
  this->bus_.fail_writes = true;
  const auto reads = this->bus_.reads;
  this->sample(15);
  EXPECT_EQ(this->bus_.reads, reads);
  EXPECT_EQ(snapshots.size(), 1U);
  EXPECT_TRUE(this->board_.status_has_warning());
}

TEST_F(NeoKeyTest, FailedReadsKeepStateAndRecoverWithoutInventingTransitions) {
  this->board_.prepare();
  this->sample(15);
  this->bus_.fail_reads = true;
  for (int i = 0; i < 3; i++)
    this->sample(0);
  EXPECT_EQ(snapshots.size(), 1U);
  EXPECT_FALSE(this->board_.ready());
  EXPECT_TRUE(this->board_.status_has_warning());
  const auto writes = this->bus_.writes.size();
  this->sample(0);
  EXPECT_EQ(this->bus_.writes.size(), writes);
  const auto next = App.scheduler.next_schedule_in(millis());
  ASSERT_TRUE(next.has_value());
  EXPECT_GT(*next, 4000U);
  this->bus_.fail_reads = false;
  this->board_.restart();  // Test reinitialization without sleeping through the retry backoff.
  run_scheduler(530);
  EXPECT_TRUE(this->board_.ready());
  EXPECT_FALSE(this->board_.status_has_warning());
  ASSERT_EQ(snapshots.size(), 2U);
  EXPECT_EQ(snapshots.back(), (std::pair<uint8_t, uint8_t>{0, 15}));
}

TEST_F(NeoKeyTest, MissingBoardAtStartupCanRecover) {
  this->bus_.fail_writes = true;
  this->board_.setup();
  EXPECT_FALSE(this->board_.is_failed());
  EXPECT_TRUE(this->board_.status_has_warning());
  EXPECT_TRUE(snapshots.empty());
  this->bus_.fail_writes = false;
  this->board_.restart();
  run_scheduler(530);
  EXPECT_TRUE(this->board_.ready());
  EXPECT_FALSE(this->board_.status_has_warning());
}

TEST_F(NeoKeyTest, WrongHardwareIdDoesNotConfigureGPIO) {
  this->bus_.hardware_id = 0xFF;
  this->board_.setup();
  run_scheduler(530);
  EXPECT_FALSE(this->board_.ready());
  EXPECT_EQ(this->bus_.writes.size(), 2U);
  EXPECT_TRUE(snapshots.empty());
}

TEST_F(NeoKeyTest, AnotherSeesawProductDoesNotConfigureGPIO) {
  this->bus_.version = 0x12340000;
  this->board_.setup();
  run_scheduler(530);
  EXPECT_FALSE(this->board_.ready());
  EXPECT_EQ(this->bus_.writes.size(), 3U);
  EXPECT_TRUE(snapshots.empty());
}

TEST_F(NeoKeyTest, BoardsHaveIndependentTransactionsAndState) {
  FakeBus second_bus;
  second_bus.address = 0x31;
  second_bus.pressed = 8;
  TestNeoKey second;
  second.set_i2c_bus(&second_bus);
  second.set_i2c_address(0x31);
  second.set_read_delay(1);
  second.set_keys_publisher([](uint8_t pressed, uint8_t changed) { second_snapshots.emplace_back(pressed, changed); });
  this->board_.prepare();
  second.prepare();
  this->sample(1);
  {
    LoopBlockingGuard guard{nullptr, nullptr, 1000};
    second.loop();
  }
  run_scheduler(3);
  EXPECT_EQ(snapshots.back().first, 1);
  ASSERT_EQ(second_snapshots.size(), 1U);
  EXPECT_EQ(second_snapshots.back().first, 8);
  second.stop();
}

}  // namespace
}  // namespace esphome::adafruit_neokey::testing
