#include "levoit_select.h"
#include "../levoit_humidifier.h"

namespace esphome {
namespace levoit_humidifier {

void LevoitSelect::control(size_t index) {
  if (this->type_ != SelectType::MODE)
    return;
  if (this->parent_->set_mode(index)) {
    this->publish_state(index);
  } else if (this->active_index().has_value()) {
    this->publish_state(*this->active_index());  // refused: snap Home Assistant back
  }
}

}  // namespace levoit_humidifier
}  // namespace esphome
