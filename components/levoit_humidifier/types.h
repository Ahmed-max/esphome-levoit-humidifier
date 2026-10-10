#pragma once

#include <cstdint>

namespace esphome {
namespace levoit_humidifier {

enum class SwitchType : uint8_t { POWER, DISPLAY, AUTO_STOP, COUNT };
enum class NumberType : uint8_t { TARGET_HUMIDITY, MIST_LEVEL, COUNT };
enum class SelectType : uint8_t { MODE, COUNT };
enum class SensorType : uint8_t { HUMIDITY, TEMPERATURE, COUNT };
enum class BinarySensorType : uint8_t { WATER_TANK_LIFTED, LOW_WATER, MIST_ACTIVE, AUTO_STOPPED, COUNT };
enum class TextSensorType : uint8_t { MCU_VERSION, COUNT };

// Opcodes, see docs/PROTOCOL.md
static const uint16_t CMD_SET_AUTO_MODE = 0x4080;
static const uint16_t CMD_QUERY_STATUS = 0x40C0;
static const uint16_t CMD_STATUS_REPORT = 0x40C1;
static const uint16_t CMD_SET_POWER = 0xA000;
static const uint16_t CMD_SET_DISPLAY = 0xA105;
static const uint16_t CMD_SET_MANUAL_LEVEL = 0xA260;
static const uint16_t CMD_SET_AUTO_STOP = 0xA5E5;
static const uint16_t CMD_BUTTON_FIRST = 0xD102;
static const uint16_t CMD_BUTTON_LAST = 0xD104;

static const uint8_t MIN_TARGET_HUMIDITY = 30;
static const uint8_t MAX_TARGET_HUMIDITY = 80;
static const uint8_t MAX_MIST_LEVEL = 2;

}  // namespace levoit_humidifier
}  // namespace esphome
