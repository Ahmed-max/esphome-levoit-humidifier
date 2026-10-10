#include "levoit_button.h"
#include "../levoit_humidifier.h"

namespace esphome {
namespace levoit_humidifier {

void LevoitButton::press_action() {
  if (this->type_ == ButtonType::RESET_CLEAN_REMINDER)
    this->parent_->reset_clean_reminder();
}

}  // namespace levoit_humidifier
}  // namespace esphome
