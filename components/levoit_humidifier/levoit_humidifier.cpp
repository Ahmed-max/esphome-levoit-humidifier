#include "levoit_humidifier.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace levoit_humidifier {

static const char *const TAG = "levoit_humidifier";

static const uint8_t FRAME_START = 0xA5;
static const uint8_t HEADER_LEN = 6;
static const uint16_t MAX_PAYLOAD = 0x5DC;
static const uint8_t PAYLOAD_VERSION = 0x01;

static const uint8_t CTRL_VERSION = 0x02;
static const uint8_t CTRL_ACK = 0x10;
static const uint8_t CTRL_WANT_ACK = 0x20;
static const uint8_t CTRL_ERROR = 0x40;

static const uint32_t ACK_TIMEOUT_MS = 1000;
static const uint8_t MAX_RETRIES = 3;
static const uint32_t RX_GAP_MS = 100;

// Offsets in the status block (data of 0x40C1 / ACK of 0x40C0)
static const size_t ST_FW_PATCH = 0;
static const size_t ST_FW_MINOR = 1;
static const size_t ST_FW_MAJOR = 2;
static const size_t ST_POWER = 3;
// the stock firmware reports this byte as water_tank_lifted: 1 = tank removed
static const size_t ST_TANK_LIFTED = 4;
static const size_t ST_WATER_EMPTY = 5;
static const size_t ST_AUTO_STOPPED = 6;
static const size_t ST_DISPLAY = 7;
// "fog_status": not reported to the app and 1 even while off, so not used for mist_active
static const size_t ST_FOG = 8;
static const size_t ST_TARGET = 9;
static const size_t ST_HUMIDITY = 10;
static const size_t ST_TEMPERATURE = 11;
static const size_t ST_MODE = 12;
static const size_t ST_MIST_LEVEL = 13;
static const size_t ST_EXCEPTION = 14;
static const size_t ST_MIN_LEN = 15;
static const size_t ST_CLEAN_REMINDER = 20;  // only sent by MCUs that run the reminder themselves

// stock firmware maps 0 -> "auto", 1 -> "manual" (its internal mode 4)
static const uint8_t MODE_MANUAL = 1;

// set_wifi_led data: [mode, on_ms u16, off_ms u16, repeat]. Values are the stock
// firmware's LED behaviour table: connected = solid, pairing = 500 ms blink,
// everything else (booting, connecting, offline) = off.
static const uint8_t WIFI_LED_OFF = 0;
static const uint8_t WIFI_LED_ON = 1;
static const uint8_t WIFI_LED_BLINK = 2;

void LevoitHumidifier::setup() {
  // stock firmware clears any timer icon left over from before the reboot
  this->queue_command_(CMD_SET_TIMER_ICON, {0});
  this->queue_command_(CMD_QUERY_STATUS, {});
  this->publish_timer_();
}

void LevoitHumidifier::update() {
  // only poll when idle, a status query is also queued after every ACKed command
  if (!this->awaiting_ack_ && this->queue_.empty())
    this->queue_command_(CMD_QUERY_STATUS, {});
}

void LevoitHumidifier::loop() {
  this->read_uart_();

  if (this->awaiting_ack_ && millis() - this->sent_at_ > ACK_TIMEOUT_MS) {
    if (this->retries_ < MAX_RETRIES) {
      this->retries_++;
      ESP_LOGD(TAG, "No ACK for 0x%04X, resending (%u)", this->in_flight_.cmd, this->retries_);
      this->write_frame_(CTRL_VERSION | CTRL_WANT_ACK, this->tx_seq_, this->in_flight_.cmd, this->in_flight_.data);
      this->sent_at_ = millis();
    } else {
      ESP_LOGW(TAG, "MCU did not ACK command 0x%04X, dropping it", this->in_flight_.cmd);
      this->awaiting_ack_ = false;
    }
  }

  if (this->timer_active_) {
    uint32_t now = millis();
    if ((int32_t) (now - this->timer_end_) >= 0) {
      ESP_LOGI(TAG, "Timer expired, turning %s", this->timer_power_ ? "on" : "off");
      this->set_power(this->timer_power_);
      this->cancel_timer_();
    } else if (now - this->timer_published_at_ >= 60000) {
      this->publish_timer_();
    }
  }

  this->update_wifi_led_(false);
  this->send_next_();
}

// ---- timer / clean reminder -----------------------------------------------

void LevoitHumidifier::set_timer(uint16_t minutes) {
  if (minutes == 0) {
    this->cancel_timer_();
    return;
  }
  minutes = std::min(minutes, MAX_TIMER_MINUTES);
  this->timer_active_ = true;
  this->timer_power_ = !this->power_on_;
  this->timer_end_ = millis() + minutes * 60000UL;
  ESP_LOGI(TAG, "Timer: turn %s in %u min", this->timer_power_ ? "on" : "off", minutes);
  this->queue_command_(CMD_SET_TIMER_ICON, {1});
  this->publish_timer_();
}

void LevoitHumidifier::cancel_timer_() {
  if (this->timer_active_)
    this->queue_command_(CMD_SET_TIMER_ICON, {0});
  this->timer_active_ = false;
  this->publish_timer_();
}

void LevoitHumidifier::publish_timer_() {
  this->timer_published_at_ = millis();
  auto *n = this->numbers_[(size_t) NumberType::TIMER];
  if (n == nullptr)
    return;
  float remaining = 0;
  if (this->timer_active_)
    remaining = std::ceil((this->timer_end_ - millis()) / 60000.0f);  // round up, 0 only when done
  if (n->state != remaining)
    n->publish_state(remaining);
}

void LevoitHumidifier::reset_clean_reminder() {
  this->queue_command_(CMD_CLEAN_RESET, {0});
  this->queue_command_(CMD_QUERY_STATUS, {});
}

void LevoitHumidifier::handle_button_(uint16_t cmd, uint8_t value) {
  const char *type = nullptr;
  if (cmd == CMD_TEST_MODE) {
    type = "test_mode";
  } else if (value == 2 || value == 3) {
    type = "pairing";  // stock firmware enters Wi-Fi pairing
  } else if (value == 4) {
    type = "factory_reset";  // stock firmware wipes its Wi-Fi and account data
  }
  ESP_LOGI(TAG, "Button event 0x%04X value %u -> %s", cmd, value, type != nullptr ? type : "unknown");
  if (type != nullptr && this->button_event_ != nullptr)
    this->button_event_->trigger(type);
}

void LevoitHumidifier::update_wifi_led_(bool force) {
  if (!this->have_status_)
    return;  // the MCU has not answered yet

  WifiLed want = WifiLed::OFF;
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    if (wifi::global_wifi_component->is_connected()) {
      want = WifiLed::ON;
    } else if (wifi::global_wifi_component->is_ap_active()) {
      want = WifiLed::BLINK;  // fallback hotspot, the ESPHome counterpart of pairing mode
    }
  }
#endif
  if (want == this->wifi_led_ && !force)
    return;
  // stock firmware holds back non-connected LED states while the humidifier is off,
  // and sends the current state again when it is switched on
  if (want != WifiLed::ON && !this->power_on_)
    return;

  switch (want) {
    case WifiLed::ON:
      this->queue_command_(CMD_SET_WIFI_LED, {WIFI_LED_ON, 125, 0, 125, 0, 0});
      break;
    case WifiLed::BLINK:
      this->queue_command_(CMD_SET_WIFI_LED, {WIFI_LED_BLINK, 0xF4, 0x01, 0xF4, 0x01, 0});
      break;
    default:
      this->queue_command_(CMD_SET_WIFI_LED, {WIFI_LED_OFF, 75, 0, 75, 0, 0});
      break;
  }
  ESP_LOGD(TAG, "Wi-Fi icon: %s", want == WifiLed::ON ? "on" : want == WifiLed::BLINK ? "blinking" : "off");
  this->wifi_led_ = want;
}

void LevoitHumidifier::dump_config() {
  ESP_LOGCONFIG(TAG, "Levoit humidifier:");
  LOG_UPDATE_INTERVAL(this);
  this->check_uart_settings(9600);
}

// ---- commands --------------------------------------------------------------

void LevoitHumidifier::set_power(bool on) { this->queue_command_(CMD_SET_POWER, {(uint8_t) on}); }

bool LevoitHumidifier::set_display(bool on) {
  if (this->have_status_ && !this->power_on_) {
    ESP_LOGW(TAG, "Display can only be changed while the humidifier is on");
    return false;
  }
  this->queue_command_(CMD_SET_DISPLAY, {(uint8_t) (on ? 100 : 0)});
  return true;
}

void LevoitHumidifier::set_auto_stop(bool on) { this->queue_command_(CMD_SET_AUTO_STOP, {(uint8_t) on}); }

void LevoitHumidifier::set_auto_mode(bool on) {
  if (on) {
    this->set_target_humidity(this->target_humidity_);
  } else {
    this->set_mist_level(this->mist_level_);
  }
}

void LevoitHumidifier::set_target_humidity(uint8_t target) {
  target = std::clamp<uint8_t>(target, MIN_TARGET_HUMIDITY, MAX_TARGET_HUMIDITY);
  this->target_humidity_ = target;
  // same layout the stock firmware sends: target with a +-5 % band
  this->queue_command_(CMD_SET_AUTO_MODE, {target, (uint8_t) (target - 5), (uint8_t) (target + 5), 2, 1, 1});
}

void LevoitHumidifier::set_mist_level(uint8_t level) {
  level = std::clamp<uint8_t>(level, 1, MAX_MIST_LEVEL);
  this->mist_level_ = level;
  this->queue_command_(CMD_SET_MANUAL_LEVEL, {0, 1, level});
}

void LevoitHumidifier::queue_command_(uint16_t cmd, std::vector<uint8_t> data) {
  // a newer value for the same setting replaces the queued one
  auto same = [cmd](const Command &c) { return c.cmd == cmd; };
  this->queue_.erase(std::remove_if(this->queue_.begin(), this->queue_.end(), same), this->queue_.end());
  this->queue_.push_back(Command{cmd, std::move(data)});
}

void LevoitHumidifier::send_next_() {
  if (this->awaiting_ack_ || this->queue_.empty())
    return;
  this->in_flight_ = std::move(this->queue_.front());
  this->queue_.pop_front();
  this->tx_seq_++;
  this->write_frame_(CTRL_VERSION | CTRL_WANT_ACK, this->tx_seq_, this->in_flight_.cmd, this->in_flight_.data);
  this->awaiting_ack_ = true;
  this->sent_at_ = millis();
  this->retries_ = 0;
}

void LevoitHumidifier::write_frame_(uint8_t ctrl, uint8_t seq, uint16_t cmd, const std::vector<uint8_t> &data) {
  uint16_t len = 4 + data.size();
  std::vector<uint8_t> frame = {FRAME_START, ctrl, seq, (uint8_t) (len & 0xFF), (uint8_t) (len >> 8), 0,
                                PAYLOAD_VERSION, (uint8_t) (cmd & 0xFF), (uint8_t) (cmd >> 8), 0};
  frame.insert(frame.end(), data.begin(), data.end());
  uint8_t sum = 0;
  for (uint8_t b : frame)
    sum += b;
  frame[5] = 0xFF - sum;
  ESP_LOGV(TAG, ">>> %s", format_hex_pretty(frame).c_str());
  this->write_array(frame.data(), frame.size());
}

// ---- receive ---------------------------------------------------------------

void LevoitHumidifier::read_uart_() {
  uint32_t now = millis();
  if (!this->rx_.empty() && now - this->last_rx_ > RX_GAP_MS)
    this->rx_.clear();  // drop a partial frame

  uint8_t b;
  while (this->available() && this->read_byte(&b)) {
    this->last_rx_ = now;
    if (this->rx_.empty() && b != FRAME_START)
      continue;
    this->rx_.push_back(b);
    if (this->rx_.size() < HEADER_LEN)
      continue;

    uint16_t len = this->rx_[3] | (this->rx_[4] << 8);
    if (len > MAX_PAYLOAD) {
      this->rx_.clear();
      continue;
    }
    if (this->rx_.size() < HEADER_LEN + len)
      continue;

    ESP_LOGV(TAG, "<<< %s", format_hex_pretty(this->rx_).c_str());
    uint8_t sum = 0;
    for (uint8_t c : this->rx_)
      sum += c;
    if (sum != 0xFF) {
      ESP_LOGW(TAG, "Dropping frame with bad checksum");
    } else {
      this->handle_frame_(this->rx_[1], this->rx_[2], this->rx_.data() + HEADER_LEN, len);
    }
    this->rx_.clear();
  }
}

void LevoitHumidifier::handle_frame_(uint8_t ctrl, uint8_t seq, const uint8_t *payload, uint16_t len) {
  if ((ctrl & 0x0F) != CTRL_VERSION || len < 4 || payload[0] != PAYLOAD_VERSION) {
    ESP_LOGW(TAG, "Unsupported frame ctrl=0x%02X len=%u", ctrl, len);
    return;
  }
  uint16_t cmd = payload[1] | (payload[2] << 8);
  const uint8_t *data = payload + 4;
  size_t data_len = len - 4;

  if (ctrl & CTRL_ACK) {
    if (!this->awaiting_ack_ || seq != this->tx_seq_)
      return;
    this->awaiting_ack_ = false;
    if (ctrl & CTRL_ERROR)
      ESP_LOGW(TAG, "MCU rejected command 0x%04X, error 0x%02X", cmd, payload[3]);
    if (cmd == CMD_QUERY_STATUS) {
      this->handle_status_(data, data_len);
    } else {
      this->queue_command_(CMD_QUERY_STATUS, {});  // read back the new state
    }
    return;
  }

  if (cmd == CMD_STATUS_REPORT) {
    this->handle_status_(data, data_len);
  } else if ((cmd >= CMD_BUTTON_FIRST && cmd <= CMD_BUTTON_LAST) || cmd == CMD_TEST_MODE) {
    if (ctrl & CTRL_WANT_ACK)
      this->write_frame_(CTRL_VERSION | CTRL_ACK, seq, cmd, {});
    this->handle_button_(cmd, data_len > 0 ? data[0] : 0);
    this->queue_command_(CMD_QUERY_STATUS, {});
  } else if (cmd == CMD_CLEAN_RESET) {
    ESP_LOGD(TAG, "Tank-cleaning reminder reset on the device");
    this->queue_command_(CMD_QUERY_STATUS, {});
  } else {
    ESP_LOGD(TAG, "Unhandled MCU frame 0x%04X (%u bytes)", cmd, len);
  }
}

void LevoitHumidifier::handle_status_(const uint8_t *d, size_t len) {
  if (len < ST_MIN_LEN) {
    ESP_LOGW(TAG, "Status block too short (%u bytes)", len);
    return;
  }
  bool first = !this->have_status_;
  this->have_status_ = true;

  bool was_on = this->power_on_;
  bool was_display = this->display_on_;
  this->power_on_ = d[ST_POWER];
  this->display_on_ = d[ST_DISPLAY] > 0;
  if (first || (this->power_on_ && !was_on)) {
    this->update_wifi_led_(true);
  } else if (this->display_on_ && !was_display && this->wifi_led_ == WifiLed::ON) {
    this->update_wifi_led_(true);  // stock firmware re-lights the icon when the display comes back
  }

  std::vector<uint8_t> status(d, d + ST_MIN_LEN);
  if (status != this->last_status_) {
    ESP_LOGD(TAG,
             "Status: power=%u mode=%s mist=%u target=%u%% humidity=%u%% temp=%uC tank=%s water=%s "
             "fog=%u display=%u raw=%s",
             d[ST_POWER], d[ST_MODE] == MODE_MANUAL ? "manual" : "auto", d[ST_MIST_LEVEL], d[ST_TARGET],
             d[ST_HUMIDITY], d[ST_TEMPERATURE], d[ST_TANK_LIFTED] ? "lifted" : "in", d[ST_WATER_EMPTY] ? "low" : "ok",
             d[ST_FOG], d[ST_DISPLAY], format_hex_pretty(d, len).c_str());
    this->last_status_ = std::move(status);
  }
  this->target_humidity_ = d[ST_TARGET];
  if (d[ST_MIST_LEVEL] >= 1)
    this->mist_level_ = d[ST_MIST_LEVEL];

  auto pub_switch = [&](SwitchType t, bool v) {
    auto *s = this->switches_[(size_t) t];
    if (s != nullptr && (first || s->state != v))
      s->publish_state(v);
  };
  auto pub_number = [&](NumberType t, float v) {
    auto *n = this->numbers_[(size_t) t];
    if (n != nullptr && (first || n->state != v))
      n->publish_state(v);
  };
  auto pub_sensor = [&](SensorType t, float v) {
    auto *s = this->sensors_[(size_t) t];
    if (s != nullptr && (first || s->get_raw_state() != v))
      s->publish_state(v);
  };
  auto pub_binary = [&](BinarySensorType t, bool v) {
    auto *s = this->binary_sensors_[(size_t) t];
    if (s != nullptr)
      s->publish_state(v);  // binary sensors already dedupe
  };

  pub_switch(SwitchType::POWER, d[ST_POWER]);
  pub_switch(SwitchType::DISPLAY, d[ST_DISPLAY] > 0);

  pub_number(NumberType::TARGET_HUMIDITY, d[ST_TARGET]);
  if (d[ST_MIST_LEVEL] >= 1)
    pub_number(NumberType::MIST_LEVEL, d[ST_MIST_LEVEL]);

  auto *mode = this->selects_[(size_t) SelectType::MODE];
  size_t mode_index = d[ST_MODE] == MODE_MANUAL ? 0 : 1;
  if (mode != nullptr && (first || mode->active_index() != mode_index))
    mode->publish_state(mode_index);

  pub_sensor(SensorType::HUMIDITY, d[ST_HUMIDITY]);
  pub_sensor(SensorType::TEMPERATURE, d[ST_TEMPERATURE]);

  pub_binary(BinarySensorType::WATER_TANK_LIFTED, d[ST_TANK_LIFTED]);
  pub_binary(BinarySensorType::LOW_WATER, d[ST_WATER_EMPTY]);
  // misting = on, tank in place, water available and the target not reached yet
  pub_binary(BinarySensorType::MIST_ACTIVE, d[ST_POWER] && !d[ST_TANK_LIFTED] && !d[ST_WATER_EMPTY] && !d[ST_AUTO_STOPPED]);
  pub_binary(BinarySensorType::AUTO_STOPPED, d[ST_AUTO_STOPPED]);
  if (len > ST_CLEAN_REMINDER)
    pub_binary(BinarySensorType::CLEAN_REMINDER, d[ST_CLEAN_REMINDER]);

  auto *ver = this->text_sensors_[(size_t) TextSensorType::MCU_VERSION];
  if (ver != nullptr && first)
    ver->publish_state(str_sprintf("%u.%u.%02u", d[ST_FW_MAJOR], d[ST_FW_MINOR], d[ST_FW_PATCH]));

  if (d[ST_EXCEPTION] != 0)
    ESP_LOGW(TAG, "MCU reports exception flags 0x%02X", d[ST_EXCEPTION]);
}

}  // namespace levoit_humidifier
}  // namespace esphome
