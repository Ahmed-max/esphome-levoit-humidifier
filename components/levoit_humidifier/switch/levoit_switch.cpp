#include "levoit_switch.h"
#include "../levoit_humidifier.h"

namespace esphome {
namespace levoit_humidifier {

void LevoitSwitch::write_state(bool state) {
  switch (this->type_) {
    case SwitchType::POWER:
      this->parent_->set_power(state);
      break;
    case SwitchType::DISPLAY:
      this->parent_->set_display(state);
      break;
    case SwitchType::AUTO_STOP:
      this->parent_->set_auto_stop(state);
      break;
    default:
      return;
  }
  // optimistic, the status read after the MCU ACK corrects it
  this->publish_state(state);
}

}  // namespace levoit_humidifier
}  // namespace esphome
