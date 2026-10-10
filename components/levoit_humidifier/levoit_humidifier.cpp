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

#ifdef USE_LEVOIT_MQTT_HUMIDIFIER
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/application.h"
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

// Modes as the device button cycles them: Auto, then manual at mist level 1 (Low) or 2 (High)
static const char *const MODE_NAMES[] = {"auto", "low", "high"};
static size_t mode_index(const uint8_t *d) {
  if (d[ST_MODE] != MODE_MANUAL)
    return 0;
  return d[ST_MIST_LEVEL] >= 2 ? 2 : 1;
}
// stock firmware reports humidity_high to the app above this value
static const uint8_t HUMIDITY_HIGH_ABOVE = 80;

// set_wifi_led data: [mode, on_ms u16, off_ms u16, repeat]. Values are the stock
// firmware's LED behaviour table: connected = solid, pairing = 500 ms blink,
// everything else (booting, connecting, offline) = off.
static const uint8_t WIFI_LED_OFF = 0;
static const uint8_t WIFI_LED_ON = 1;
static const uint8_t WIFI_LED_BLINK = 2;

void LevoitHumidifier::setup() {
  this->auto_stop_pref_ = global_preferences->make_preference<bool>(fnv1_hash("levoit_auto_stop"));
  if (!this->auto_stop_pref_.load(&this->auto_stop_))
    this->auto_stop_ = false;
  // stock firmware clears any timer icon left over from before the reboot
  this->queue_command_(CMD_SET_TIMER_ICON, {0});
  this->queue_command_(CMD_QUERY_STATUS, {});
  this->publish_timer_();
#ifdef USE_LEVOIT_MQTT_HUMIDIFIER
  this->mqtt_setup_();
#endif
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
#ifdef USE_LEVOIT_MQTT_HUMIDIFIER
  this->mqtt_loop_();
#endif
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
  // The stock firmware starts Wi-Fi pairing (2/3), a factory reset (4) or factory test
  // mode (0xD104) on these long-presses. None of that exists here, so they are only logged.
  ESP_LOGD(TAG, "Long press on the device (0x%04X value %u) ignored", cmd, value);
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

void LevoitHumidifier::set_auto_stop(bool on) {
  this->auto_stop_ = on;
  this->auto_stop_pref_.save(&on);  // stock firmware keeps this setting in flash
  this->queue_command_(CMD_SET_AUTO_STOP, {(uint8_t) on});
}

// Rules below follow the stock firmware's app command handlers.

bool LevoitHumidifier::check_water_(const char *what) {
  if (!this->have_status_)
    return true;
  if (this->water_low_) {
    ESP_LOGW(TAG, "Cannot set %s: water lacks", what);
    return false;
  }
  if (this->tank_lifted_) {
    ESP_LOGW(TAG, "Cannot set %s: water tank was lifted", what);
    return false;
  }
  return true;
}

void LevoitHumidifier::send_auto_mode_() {
  uint8_t t = this->target_humidity_;
  // same layout the stock firmware sends: target with a +-5 % band
  this->queue_command_(CMD_SET_AUTO_MODE, {t, (uint8_t) (t - 5), (uint8_t) (t + 5), 2, 1, 1});
}

bool LevoitHumidifier::set_auto_mode(bool on) {
  if (!this->check_water_("mode"))
    return false;
  if (this->have_status_ && !this->power_on_)
    this->set_power(true);  // choosing a mode switches the humidifier on
  if (!this->have_status_ || on == this->manual_mode_) {
    if (on) {
      this->send_auto_mode_();
    } else {
      this->queue_command_(CMD_SET_MANUAL_LEVEL, {0, 1, this->mist_level_});
    }
  }
  if (this->display_on_ && this->wifi_led_ == WifiLed::ON)
    this->update_wifi_led_(true);
  return true;
}

bool LevoitHumidifier::set_target_humidity(uint8_t target) {
  if (target < MIN_TARGET_HUMIDITY || target > MAX_TARGET_HUMIDITY) {
    ESP_LOGW(TAG, "Target humidity %u%% is out of range", target);
    return false;
  }
  if (this->have_status_ && !this->power_on_) {
    ESP_LOGW(TAG, "Cannot set target humidity: the humidifier is off");
    return false;
  }
  if (!this->check_water_("target humidity"))
    return false;
  if (this->have_status_ && this->manual_mode_) {
    ESP_LOGW(TAG, "Manual mode cannot set target humidity, switch to Auto first");
    return false;
  }
  this->target_humidity_ = target;
  this->send_auto_mode_();
  return true;
}

bool LevoitHumidifier::set_mist_level(uint8_t level) {
  if (level < 1 || level > MAX_MIST_LEVEL) {
    ESP_LOGW(TAG, "Mist level %u is out of range", level);
    return false;
  }
  if (!this->check_water_("mist level"))
    return false;
  if (this->have_status_ && !this->power_on_)
    this->set_power(true);  // like the app, a mist level switches the humidifier on
  this->mist_level_ = level;
  this->queue_command_(CMD_SET_MANUAL_LEVEL, {0, 1, level});  // also selects manual mode
  return true;
}

bool LevoitHumidifier::set_mode(size_t index) {
  if (index == 0)
    return this->set_auto_mode(true);
  return this->set_mist_level(index == 2 ? 2 : 1);  // Low / High = manual at that level
}

bool LevoitHumidifier::set_auto_target(uint8_t target) {
  if (target < MIN_TARGET_HUMIDITY || target > MAX_TARGET_HUMIDITY) {
    ESP_LOGW(TAG, "Target humidity %u%% is out of range", target);
    return false;
  }
  if (this->have_status_ && !this->power_on_) {
    ESP_LOGW(TAG, "Cannot set target humidity: the humidifier is off");
    return false;
  }
  if (!this->check_water_("target humidity"))
    return false;
  this->target_humidity_ = target;
  if (this->have_status_ && this->manual_mode_)
    return this->set_auto_mode(true);  // sends auto mode with the new target
  this->send_auto_mode_();
  return true;
}

#ifdef USE_LEVOIT_MQTT_HUMIDIFIER
std::string LevoitHumidifier::mqtt_topic_(const char *suffix) const {
  return mqtt::global_mqtt_client->get_topic_prefix() + "/levoit_humidifier/" + suffix;
}

void LevoitHumidifier::mqtt_setup_() {
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr)
    return;
  client->subscribe(this->mqtt_topic_("power/set"), [this](const std::string &, const std::string &payload) {
    this->set_power(payload == "ON");
    this->mqtt_dirty_ = true;
  });
  client->subscribe(this->mqtt_topic_("mode/set"), [this](const std::string &, const std::string &payload) {
    for (size_t i = 0; i < 3; i++) {
      if (payload == MODE_NAMES[i])
        this->set_mode(i);
    }
    this->mqtt_dirty_ = true;  // a refused change snaps the entity back
  });
  client->subscribe(this->mqtt_topic_("target/set"), [this](const std::string &, const std::string &payload) {
    auto target = parse_number<float>(payload);
    if (target.has_value())
      this->set_auto_target((uint8_t) *target);
    this->mqtt_dirty_ = true;
  });
}

void LevoitHumidifier::mqtt_loop_() {
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) {
    this->mqtt_announced_ = false;  // announce again after a reconnect
    return;
  }

  if (!this->mqtt_announced_) {
    std::string node = App.get_name();
    std::string topic = this->mqtt_discovery_prefix_ + "/humidifier/" + node + "/humidifier/config";
    bool ok = client->publish_json(
        topic,
        [this, &node](JsonObject root) {
          root["name"] = nullptr;  // takes the device name
          root["unique_id"] = node + "_levoit_humidifier";
          root["device_class"] = "humidifier";
          root["icon"] = "mdi:air-humidifier";
          root["command_topic"] = this->mqtt_topic_("power/set");
          root["state_topic"] = this->mqtt_topic_("power");
          root["target_humidity_command_topic"] = this->mqtt_topic_("target/set");
          root["target_humidity_state_topic"] = this->mqtt_topic_("target");
          root["current_humidity_topic"] = this->mqtt_topic_("current");
          root["mode_command_topic"] = this->mqtt_topic_("mode/set");
          root["mode_state_topic"] = this->mqtt_topic_("mode");
          JsonArray modes = root["modes"].to<JsonArray>();
          for (const char *name : MODE_NAMES)
            modes.add(name);
          root["action_topic"] = this->mqtt_topic_("action");
          root["min_humidity"] = MIN_TARGET_HUMIDITY;
          root["max_humidity"] = MAX_TARGET_HUMIDITY;
          const auto &availability = mqtt::global_mqtt_client->get_availability();
          if (!availability.topic.empty()) {
            root["availability_topic"] = availability.topic;
            root["payload_available"] = availability.payload_available;
            root["payload_not_available"] = availability.payload_not_available;
          }
          // same MAC as the ESPHome device, so Home Assistant puts this entity on it
          JsonObject device = root["device"].to<JsonObject>();
          JsonArray connections = device["connections"].to<JsonArray>();
          JsonArray mac = connections.add<JsonArray>();
          mac.add("mac");
          mac.add(get_mac_address_pretty());
        },
        0, true);
    if (!ok)
      return;
    ESP_LOGI(TAG, "Announced the humidifier entity over MQTT");
    this->mqtt_announced_ = true;
    this->mqtt_dirty_ = true;
  }

  if (!this->mqtt_dirty_ || this->last_status_.size() < ST_MIN_LEN)
    return;
  this->mqtt_dirty_ = false;
  const uint8_t *d = this->last_status_.data();
  bool on = d[ST_POWER];
  bool misting = on && !d[ST_TANK_LIFTED] && !d[ST_WATER_EMPTY] && !d[ST_AUTO_STOPPED];
  // std::string payloads: a bare literal would pick the (char *, length) overload
  client->publish(this->mqtt_topic_("power"), std::string(on ? "ON" : "OFF"), 0, true);
  client->publish(this->mqtt_topic_("mode"), std::string(MODE_NAMES[mode_index(d)]), 0, true);
  client->publish(this->mqtt_topic_("target"), to_string(d[ST_TARGET]), 0, true);
  client->publish(this->mqtt_topic_("current"), to_string(d[ST_HUMIDITY]), 0, true);
  client->publish(this->mqtt_topic_("action"), std::string(!on ? "off" : misting ? "humidifying" : "idle"), 0, true);
}
#endif
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
    if (ctrl & CTRL_ERROR) {
      ESP_LOGW(TAG, "MCU rejected command 0x%04X, error 0x%02X", cmd, payload[3]);
    }
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
  this->manual_mode_ = d[ST_MODE] == MODE_MANUAL;
  this->water_low_ = d[ST_WATER_EMPTY];
  this->tank_lifted_ = d[ST_TANK_LIFTED];
  if (first) {
    // the auto-stop setting lives on the ESP in the stock firmware: re-apply the saved one
    this->queue_command_(CMD_SET_AUTO_STOP, {(uint8_t) this->auto_stop_});
    auto *s = this->switches_[(size_t) SwitchType::AUTO_STOP];
    if (s != nullptr)
      s->publish_state(this->auto_stop_);
  }
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
#ifdef USE_LEVOIT_MQTT_HUMIDIFIER
    this->mqtt_dirty_ = true;
#endif
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
  // reported like the app does: the display counts as on only while the humidifier is on
  pub_switch(SwitchType::DISPLAY, d[ST_DISPLAY] > 0 && d[ST_POWER]);

  pub_number(NumberType::TARGET_HUMIDITY, d[ST_TARGET]);
  if (d[ST_MIST_LEVEL] >= 1)
    pub_number(NumberType::MIST_LEVEL, d[ST_MIST_LEVEL]);

  auto *mode = this->selects_[(size_t) SelectType::MODE];
  size_t index = mode_index(d);
  if (mode != nullptr && (first || mode->active_index() != index))
    mode->publish_state(index);

  pub_sensor(SensorType::HUMIDITY, d[ST_HUMIDITY]);
  pub_sensor(SensorType::TEMPERATURE, d[ST_TEMPERATURE]);

  pub_binary(BinarySensorType::WATER_TANK_LIFTED, d[ST_TANK_LIFTED]);
  pub_binary(BinarySensorType::LOW_WATER, d[ST_WATER_EMPTY]);
  // misting = on, tank in place, water available and the target not reached yet
  pub_binary(BinarySensorType::MIST_ACTIVE, d[ST_POWER] && !d[ST_TANK_LIFTED] && !d[ST_WATER_EMPTY] && !d[ST_AUTO_STOPPED]);
  pub_binary(BinarySensorType::AUTO_STOPPED, d[ST_AUTO_STOPPED]);
  pub_binary(BinarySensorType::HUMIDITY_HIGH, d[ST_HUMIDITY] > HUMIDITY_HIGH_ABOVE);
  // the stock firmware raises a humidifierError event while this byte is non-zero
  pub_binary(BinarySensorType::FAULT, d[ST_EXCEPTION] != 0);
  if (len > ST_CLEAN_REMINDER)
    pub_binary(BinarySensorType::CLEAN_REMINDER, d[ST_CLEAN_REMINDER]);

  auto *ver = this->text_sensors_[(size_t) TextSensorType::MCU_VERSION];
  if (ver != nullptr && first)
    ver->publish_state(str_sprintf("%u.%u.%02u", d[ST_FW_MAJOR], d[ST_FW_MINOR], d[ST_FW_PATCH]));

  if (d[ST_EXCEPTION] != 0) {
    ESP_LOGW(TAG, "MCU reports exception flags 0x%02X", d[ST_EXCEPTION]);
  }
}

}  // namespace levoit_humidifier
}  // namespace esphome
