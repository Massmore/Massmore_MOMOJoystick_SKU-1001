// Massmore_MomoJoy - HID usage constants
// Pure C++ (no Arduino) so it can be unit-tested on the host with `pio test -e native`.
#pragma once

#include <stdint.h>

namespace massmore {

// ---- HID Usage Pages -------------------------------------------------------
enum HidUsagePage : uint16_t {
  kPageGenericDesktop = 0x01,
  kPageSimulation     = 0x02,
  kPageButton         = 0x09,
  kPageConsumer       = 0x0C,
  kPageVendor         = 0xFF00,
};

// ---- Generic Desktop usages ------------------------------------------------
enum HidUsageGD : uint16_t {
  kUsageGamepad = 0x05,
  kUsageX       = 0x30,
  kUsageY       = 0x31,
  kUsageZ       = 0x32,
  kUsageRx      = 0x33,
  kUsageRy      = 0x34,
  kUsageRz      = 0x35,
  kUsageSlider  = 0x36,
  kUsageDial    = 0x37,
  kUsageWheel   = 0x38,
  kUsageHat     = 0x39,
  kUsageSystemMainMenu = 0x85,
};

// ---- Simulation controls ---------------------------------------------------
enum HidUsageSim : uint16_t {
  kUsageAccelerator = 0xC4,  // -> R2
  kUsageBrake       = 0xC5,  // -> L2
};

// ---- Consumer usages seen on Android-mode gamepads -------------------------
enum HidUsageConsumer : uint16_t {
  kUsageConsumerMenu     = 0x0040,
  kUsageConsumerHome     = 0x0223,  // AC Home
  kUsageConsumerBack     = 0x0224,  // AC Back
  kUsageConsumerPlayPause= 0x00CD,
};

// ---- HID item flags (Input/Output/Feature) ---------------------------------
enum HidItemFlag : uint8_t {
  kFlagConstant = 1 << 0,  // 0 = Data, 1 = Constant (padding)
  kFlagVariable = 1 << 1,  // 0 = Array, 1 = Variable
  kFlagRelative = 1 << 2,  // 0 = Absolute, 1 = Relative
  kFlagNullable = 1 << 3,  // has a "null" state (typical for hat switch)
};

}  // namespace massmore
