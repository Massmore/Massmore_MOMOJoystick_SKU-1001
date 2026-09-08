// Massmore_MomoJoy - maps a decoded HID report onto MomoGamepadState (pure C++)
#pragma once

#include "MomoGamepadState.h"
#include "MomoHidParser.h"

namespace massmore {

// Maps a HID button index (1-based, usage page 0x09) -> MomoButton bit.
struct MomoButtonMap {
  uint8_t  hidButton;
  uint32_t momoBit;
};

struct MomoProfile {
  const char*          name;
  const MomoButtonMap* buttons;
  uint8_t              buttonCount;
  bool                 invertLeftY;
  bool                 invertRightY;
};

// MOMO Joystick Controller with the mode switch on "D" (Android / BLE HID) and most
// Android-mode BLE gamepads use this 18-button layout.
extern const MomoProfile kProfileAndroidGamepad;

// Alias kept for readability in sketches.
extern const MomoProfile kProfileMomo;

// 1:1 fallback: HID button N -> the Nth MOMO_BTN bit.
extern const MomoProfile kProfileGeneric;

class MomoMapper {
 public:
  void setParser(const MomoHidParser* parser) { parser_ = parser; resolve(); }
  void setProfile(const MomoProfile* profile) {
    profile_ = profile ? profile : &kProfileAndroidGamepad;
    invertLeftY_  = profile_->invertLeftY;
    invertRightY_ = profile_->invertRightY;
  }
  void setDeadzone(uint16_t dz)         { deadzone_ = dz; }
  void setTriggerThreshold(uint16_t t)  { trigThreshold_ = t; }
  void setInvertLeftY(bool inv)         { invertLeftY_ = inv; }
  void setInvertRightY(bool inv)        { invertRightY_ = inv; }

  uint16_t deadzone() const             { return deadzone_; }
  const MomoProfile* profile() const    { return profile_; }

  // Re-scan the parsed descriptor and cache the field pointers we need.
  void resolve();

  // Decode one input report payload (report ID already stripped).
  // Returns true when the report matched a known layout.
  bool decode(uint8_t reportId, const uint8_t* payload, size_t len,
              MomoGamepadState& out) const;

  bool ready() const { return parser_ != nullptr && resolved_; }

  // Introspection, used by the RawDump / Calibration examples.
  const HidField* axisField(uint8_t idx) const { return idx < 6 ? axes_[idx] : nullptr; }
  const HidField* hatField() const { return hat_; }
  uint8_t buttonFieldCount() const { return buttonBits_; }
  uint8_t mainReportId() const { return inputReportId_; }

 private:
  const MomoHidParser* parser_  = nullptr;
  const MomoProfile*   profile_ = &kProfileAndroidGamepad;
  uint16_t             deadzone_      = 24;
  uint16_t             trigThreshold_ = 64;
  bool                 invertLeftY_   = false;
  bool                 invertRightY_  = false;

  // Some controllers ship Home / Back in a separate Consumer-page report.
  struct ConsumerKey {
    const HidField* field;
    uint32_t        momoBit;
  };
  static const uint8_t kMaxConsumerKeys = 6;

  bool decodeConsumer_(const uint8_t* payload, size_t len, MomoGamepadState& out) const;

  // axes_[0..3] = LX, LY, RX, RY ; axes_[4..5] = L2, R2
  const HidField* axes_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  const HidField* hat_     = nullptr;
  const HidField* buttons_ = nullptr;   // first field of the button block
  uint8_t         buttonBits_ = 0;      // how many button fields follow
  uint8_t         inputReportId_ = 0;

  ConsumerKey     consumer_[kMaxConsumerKeys] = {};
  uint8_t         consumerCount_ = 0;
  uint8_t         consumerReportId_ = 0xFF;

  bool            resolved_ = false;
};

}  // namespace massmore
