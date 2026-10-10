#pragma once

#include <array>
#include <deque>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "types.h"

namespace esphome {
namespace levoit_humidifier {

// Talks to the humidifier MCU over the VeSync UART protocol:
//   A5 | ctrl | seq | len_lo | len_hi | checksum | 01 cmd_lo cmd_hi err data...
// One command is in flight at a time; it is resent until the MCU ACKs it.
class LevoitHumidifier : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void register_switch(SwitchType t, switch_::Switch *s) { this->switches_[(size_t) t] = s; }
  void register_number(NumberType t, number::Number *n) { this->numbers_[(size_t) t] = n; }
  void register_select(SelectType t, select::Select *s) { this->selects_[(size_t) t] = s; }
  void register_sensor(SensorType t, sensor::Sensor *s) { this->sensors_[(size_t) t] = s; }
  void register_binary_sensor(BinarySensorType t, binary_sensor::BinarySensor *s) {
    this->binary_sensors_[(size_t) t] = s;
  }
  void register_text_sensor(TextSensorType t, text_sensor::TextSensor *s) { this->text_sensors_[(size_t) t] = s; }

  void set_power(bool on);
  // like the stock firmware, refused while the humidifier is off; returns false then
  bool set_display(bool on);
  void set_auto_stop(bool on);
  // auto mode keeps humidity at the target, manual runs at a fixed mist level
  void set_auto_mode(bool on);
  void set_target_humidity(uint8_t target);
  void set_mist_level(uint8_t level);

 protected:
  struct Command {
    uint16_t cmd;
    std::vector<uint8_t> data;
  };

  // Wi-Fi icon on the humidifier display, driven like the stock firmware does
  enum class WifiLed : uint8_t { UNKNOWN, OFF, ON, BLINK };
  void update_wifi_led_(bool force);

  void queue_command_(uint16_t cmd, std::vector<uint8_t> data);
  void send_next_();
  void write_frame_(uint8_t ctrl, uint8_t seq, uint16_t cmd, const std::vector<uint8_t> &data);
  void read_uart_();
  void handle_frame_(uint8_t ctrl, uint8_t seq, const uint8_t *payload, uint16_t len);
  void handle_status_(const uint8_t *d, size_t len);

  std::deque<Command> queue_;
  Command in_flight_{};
  bool awaiting_ack_{false};
  uint8_t tx_seq_{0};
  uint32_t sent_at_{0};
  uint8_t retries_{0};

  std::vector<uint8_t> rx_;
  uint32_t last_rx_{0};

  bool have_status_{false};
  std::vector<uint8_t> last_status_;
  bool power_on_{false};
  bool display_on_{false};
  WifiLed wifi_led_{WifiLed::UNKNOWN};
  uint8_t target_humidity_{45};
  uint8_t mist_level_{1};

  std::array<switch_::Switch *, (size_t) SwitchType::COUNT> switches_{};
  std::array<number::Number *, (size_t) NumberType::COUNT> numbers_{};
  std::array<select::Select *, (size_t) SelectType::COUNT> selects_{};
  std::array<sensor::Sensor *, (size_t) SensorType::COUNT> sensors_{};
  std::array<binary_sensor::BinarySensor *, (size_t) BinarySensorType::COUNT> binary_sensors_{};
  std::array<text_sensor::TextSensor *, (size_t) TextSensorType::COUNT> text_sensors_{};
};

}  // namespace levoit_humidifier
}  // namespace esphome
