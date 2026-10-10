#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/helpers.h"
#include "../types.h"

namespace esphome {
namespace levoit_humidifier {

class LevoitHumidifier;

class LevoitSelect : public select::Select, public Parented<LevoitHumidifier> {
 public:
  void set_type(SelectType type) { this->type_ = type; }

 protected:
  void control(size_t index) override;

  SelectType type_{SelectType::MODE};
};

}  // namespace levoit_humidifier
}  // namespace esphome
