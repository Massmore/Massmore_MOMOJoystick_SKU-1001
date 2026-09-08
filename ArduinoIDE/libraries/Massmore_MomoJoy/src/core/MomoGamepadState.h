// Massmore_MomoJoy - normalised gamepad state (pure C++)
#pragma once

#include <stdint.h>

namespace massmore {

// ---- Button bits -----------------------------------------------------------
// Ranges are deliberately Bluepad32-compatible so existing MOMO sketches can be
// ported by changing only the include and the accessor names.
enum MomoButton : uint32_t {
  MOMO_BTN_A      = 1u << 0,
  MOMO_BTN_B      = 1u << 1,
  MOMO_BTN_X      = 1u << 2,
  MOMO_BTN_Y      = 1u << 3,
  MOMO_BTN_L1     = 1u << 4,
  MOMO_BTN_R1     = 1u << 5,
  MOMO_BTN_L2     = 1u << 6,   // digital edge of the analog trigger
  MOMO_BTN_R2     = 1u << 7,
  MOMO_BTN_SELECT = 1u << 8,   // View / Back
  MOMO_BTN_START  = 1u << 9,   // Menu / Options
  MOMO_BTN_HOME   = 1u << 10,  // Power / Guide
  MOMO_BTN_L3     = 1u << 11,  // left stick click
  MOMO_BTN_R3     = 1u << 12,  // right stick click
  MOMO_BTN_M1     = 1u << 13,  // back paddles M1 / M2
  MOMO_BTN_M2     = 1u << 14,
  MOMO_BTN_C      = 1u << 15,  // extra HID buttons 3 / 6 on Android layouts
  MOMO_BTN_Z      = 1u << 16,
  MOMO_BTN_CAPTURE= 1u << 17,
};

// ---- D-pad bits ------------------------------------------------------------
enum MomoDpad : uint8_t {
  MOMO_DPAD_UP    = 1u << 0,
  MOMO_DPAD_DOWN  = 1u << 1,
  MOMO_DPAD_RIGHT = 1u << 2,
  MOMO_DPAD_LEFT  = 1u << 3,
};

// ---- State -----------------------------------------------------------------
struct MomoGamepadState {
  uint32_t buttons = 0;
  uint8_t  dpad    = 0;

  int16_t  lx = 0, ly = 0;   // -512 .. 511
  int16_t  rx = 0, ry = 0;   // -512 .. 511
  uint16_t l2 = 0, r2 = 0;   // 0 .. 1023

  uint8_t  battery = 0xFF;   // 0..100 %, 0xFF = unknown
  uint32_t seq     = 0;      // increments on every received report

  bool pressed(uint32_t mask) const { return (buttons & mask) == mask; }
  bool anyPressed(uint32_t mask) const { return (buttons & mask) != 0; }
  bool dpadUp() const    { return (dpad & MOMO_DPAD_UP) != 0; }
  bool dpadDown() const  { return (dpad & MOMO_DPAD_DOWN) != 0; }
  bool dpadLeft() const  { return (dpad & MOMO_DPAD_LEFT) != 0; }
  bool dpadRight() const { return (dpad & MOMO_DPAD_RIGHT) != 0; }
};

// Convert a HID hat-switch value into D-pad bits.
// `value` is the raw field value, `logicalMin`/`logicalMax` come from the
// descriptor; anything outside the range means "centred".
uint8_t hatToDpad(int32_t value, int32_t logicalMin, int32_t logicalMax);

// Scale a raw axis value from [logicalMin..logicalMax] into [-512..511].
int16_t scaleAxis(int32_t raw, int32_t logicalMin, int32_t logicalMax);

// Scale a raw trigger value from [logicalMin..logicalMax] into [0..1023].
uint16_t scaleTrigger(int32_t raw, int32_t logicalMin, int32_t logicalMax);

// Radial dead-zone applied to a stick pair. `dz` is in output units (0..512).
void applyDeadzone(int16_t& x, int16_t& y, uint16_t dz);

}  // namespace massmore
