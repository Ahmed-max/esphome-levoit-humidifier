#include "levoit_select.h"
#include "../levoit_humidifier.h"

namespace esphome {
namespace levoit_humidifier {

void LevoitSelect::control(size_t index) {
  if (this->type_ != SelectType::MODE)
    return;
  this->parent_->set_auto_mode(index == 1);
  this->publish_state(index);
}

}  // namespace levoit_humidifier
}  // namespace esphome
