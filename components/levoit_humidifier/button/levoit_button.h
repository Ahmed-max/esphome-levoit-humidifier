#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"
#include "../types.h"

namespace esphome {
namespace levoit_humidifier {

class LevoitHumidifier;

class LevoitButton : public button::Button, public Parented<LevoitHumidifier> {
 public:
  void set_type(ButtonType type) { this->type_ = type; }

 protected:
  void press_action() override;

  ButtonType type_{ButtonType::RESET_CLEAN_REMINDER};
};

}  // namespace levoit_humidifier
}  // namespace esphome
