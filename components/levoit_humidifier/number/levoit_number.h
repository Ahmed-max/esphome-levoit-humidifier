#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/helpers.h"
#include "../types.h"

namespace esphome {
namespace levoit_humidifier {

class LevoitHumidifier;

class LevoitNumber : public number::Number, public Parented<LevoitHumidifier> {
 public:
  void set_type(NumberType type) { this->type_ = type; }

 protected:
  void control(float value) override;

  NumberType type_{NumberType::TARGET_HUMIDITY};
};

}  // namespace levoit_humidifier
}  // namespace esphome
