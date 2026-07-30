#pragma once
#include "esphome/components/button/button.h"
#include "../tuya_lock.h"

#ifdef USE_ESP32

namespace esphome {
namespace tuya_lock {

class UnlockButton : public button::Button, public Parented<TuyaLock> {
 protected:
  void press_action() override { this->parent_->unlock(); }
};

class LockButton : public button::Button, public Parented<TuyaLock> {
 protected:
  void press_action() override { this->parent_->lock(); }
};

class StatusButton : public button::Button, public Parented<TuyaLock> {
 protected:
  void press_action() override { this->parent_->status(); }
};

}  // namespace tuya_lock
}  // namespace esphome

#endif
