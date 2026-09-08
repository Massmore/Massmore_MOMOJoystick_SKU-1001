#include "MomoGamepadState.h"

namespace massmore {

uint8_t hatToDpad(int32_t value, int32_t logicalMin, int32_t logicalMax) {
  const int32_t span = logicalMax - logicalMin;
  if (span < 3) return 0;                    // not a real hat switch
  if (value < logicalMin || value > logicalMax) return 0;  // null / centred

  // Most gamepads use 0..7 (8 positions) starting at "up" and rotating
  // clockwise. Some use 1..8 with 0 = centred; normalising against logicalMin
  // handles both.
  const int32_t positions = span + 1;
  if (positions < 8) return 0;
  int32_t v = value - logicalMin;
  if (positions > 8) {
    // 0..15 style (2 counts per position) or a null value at the top.
    if (v >= 8) return 0;
  }

  switch (v) {
    case 0: return MOMO_DPAD_UP;
    case 1: return MOMO_DPAD_UP | MOMO_DPAD_RIGHT;
    case 2: return MOMO_DPAD_RIGHT;
    case 3: return MOMO_DPAD_DOWN | MOMO_DPAD_RIGHT;
    case 4: return MOMO_DPAD_DOWN;
    case 5: return MOMO_DPAD_DOWN | MOMO_DPAD_LEFT;
    case 6: return MOMO_DPAD_LEFT;
    case 7: return MOMO_DPAD_UP | MOMO_DPAD_LEFT;
    default: return 0;
  }
}

int16_t scaleAxis(int32_t raw, int32_t logicalMin, int32_t logicalMax) {
  if (logicalMax <= logicalMin) return 0;
  if (raw < logicalMin) raw = logicalMin;
  if (raw > logicalMax) raw = logicalMax;

  const int64_t span = static_cast<int64_t>(logicalMax) - logicalMin;
  // map to 0..1023 first, then shift to -512..511
  const int64_t norm = ((static_cast<int64_t>(raw) - logicalMin) * 1023 + span / 2) / span;
  int32_t out = static_cast<int32_t>(norm) - 512;
  if (out < -512) out = -512;
  if (out > 511) out = 511;
  return static_cast<int16_t>(out);
}

uint16_t scaleTrigger(int32_t raw, int32_t logicalMin, int32_t logicalMax) {
  if (logicalMax <= logicalMin) return 0;
  if (raw < logicalMin) raw = logicalMin;
  if (raw > logicalMax) raw = logicalMax;
  const int64_t span = static_cast<int64_t>(logicalMax) - logicalMin;
  const int64_t norm = ((static_cast<int64_t>(raw) - logicalMin) * 1023 + span / 2) / span;
  return static_cast<uint16_t>(norm);
}

void applyDeadzone(int16_t& x, int16_t& y, uint16_t dz) {
  if (dz == 0) return;
  const int32_t xx = x, yy = y;
  const int32_t mag2 = xx * xx + yy * yy;
  const int32_t dz2  = static_cast<int32_t>(dz) * dz;
  if (mag2 <= dz2) {
    x = 0;
    y = 0;
  }
}

}  // namespace massmore
