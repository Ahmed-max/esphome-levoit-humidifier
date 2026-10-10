#include "levoit_number.h"
#include "../levoit_humidifier.h"

namespace esphome {
namespace levoit_humidifier {

void LevoitNumber::control(float value) {
  switch (this->type_) {
    case NumberType::TARGET_HUMIDITY:
      this->parent_->set_target_humidity((uint8_t) value);
      break;
    case NumberType::MIST_LEVEL:
      this->parent_->set_mist_level((uint8_t) value);
      break;
    case NumberType::TIMER:
      this->parent_->set_timer((uint16_t) value);
      return;  // the hub publishes the remaining time
    default:
      return;
  }
  this->publish_state(value);
}

}  // namespace levoit_humidifier
}  // namespace esphome
