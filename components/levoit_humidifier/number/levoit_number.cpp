#include "levoit_number.h"
#include "../levoit_humidifier.h"

namespace esphome {
namespace levoit_humidifier {

void LevoitNumber::control(float value) {
  bool accepted;
  switch (this->type_) {
    case NumberType::TARGET_HUMIDITY:
      accepted = this->parent_->set_target_humidity((uint8_t) value);
      break;
    case NumberType::MIST_LEVEL:
      accepted = this->parent_->set_mist_level((uint8_t) value);
      break;
    case NumberType::TIMER:
      this->parent_->set_timer((uint16_t) value);
      return;  // the hub publishes the remaining time
    default:
      return;
  }
  // refused like the stock firmware would: snap Home Assistant back to the current value
  this->publish_state(accepted ? value : this->state);
}

}  // namespace levoit_humidifier
}  // namespace esphome
