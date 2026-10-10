#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"
#include "../types.h"

namespace esphome {
namespace levoit_humidifier {

class LevoitHumidifier;

class LevoitSwitch : public switch_::Switch, public Parented<LevoitHumidifier> {
 public:
  void set_type(SwitchType type) { this->type_ = type; }

 protected:
  void write_state(bool state) override;

  SwitchType type_{SwitchType::POWER};
};

}  // namespace levoit_humidifier
}  // namespace esphome
