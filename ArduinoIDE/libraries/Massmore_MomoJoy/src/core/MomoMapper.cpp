#include "MomoMapper.h"

namespace massmore {

// ---- Profiles --------------------------------------------------------------

static const MomoButtonMap kAndroidMap[] = {
    {1,  MOMO_BTN_A},
    {2,  MOMO_BTN_B},
    {3,  MOMO_BTN_C},
    {4,  MOMO_BTN_X},
    {5,  MOMO_BTN_Y},
    {6,  MOMO_BTN_Z},
    {7,  MOMO_BTN_L1},
    {8,  MOMO_BTN_R1},
    {9,  MOMO_BTN_L2},
    {10, MOMO_BTN_R2},
    {11, MOMO_BTN_SELECT},
    {12, MOMO_BTN_START},
    {13, MOMO_BTN_HOME},
    {14, MOMO_BTN_L3},
    {15, MOMO_BTN_R3},
    {16, MOMO_BTN_M1},
    {17, MOMO_BTN_M2},
    {18, MOMO_BTN_CAPTURE},
};

const MomoProfile kProfileAndroidGamepad = {
    "MOMO / Android BLE HID",
    kAndroidMap,
    static_cast<uint8_t>(sizeof(kAndroidMap) / sizeof(kAndroidMap[0])),
    false,
    false,
};

// Readable alias for sketches that want to be explicit about the pad.
const MomoProfile kProfileMomo = kProfileAndroidGamepad;

static const MomoButtonMap kGenericMap[] = {
    {1,  MOMO_BTN_A},  {2,  MOMO_BTN_B},  {3,  MOMO_BTN_X},  {4,  MOMO_BTN_Y},
    {5,  MOMO_BTN_L1}, {6,  MOMO_BTN_R1}, {7,  MOMO_BTN_L2}, {8,  MOMO_BTN_R2},
    {9,  MOMO_BTN_SELECT}, {10, MOMO_BTN_START}, {11, MOMO_BTN_HOME},
    {12, MOMO_BTN_L3}, {13, MOMO_BTN_R3}, {14, MOMO_BTN_M1}, {15, MOMO_BTN_M2},
};

const MomoProfile kProfileGeneric = {
    "Generic HID gamepad",
    kGenericMap,
    static_cast<uint8_t>(sizeof(kGenericMap) / sizeof(kGenericMap[0])),
    false,
    false,
};

// ---- Resolution ------------------------------------------------------------

void MomoMapper::resolve() {
  for (int i = 0; i < 6; ++i) axes_[i] = nullptr;
  hat_ = nullptr;
  buttons_ = nullptr;
  buttonBits_ = 0;
  resolved_ = false;
  if (parser_ == nullptr || parser_->fieldCount() == 0) return;

  // Pick the report that actually carries the sticks; fall back to the first.
  inputReportId_ = parser_->field(0).reportId;
  for (size_t i = 0; i < parser_->fieldCount(); ++i) {
    const HidField& f = parser_->field(i);
    if (f.usagePage == kPageGenericDesktop && f.usage == kUsageX) {
      inputReportId_ = f.reportId;
      break;
    }
  }

  const uint8_t rid = inputReportId_;

  axes_[0] = parser_->find(kPageGenericDesktop, kUsageX,  rid);
  axes_[1] = parser_->find(kPageGenericDesktop, kUsageY,  rid);
  axes_[2] = parser_->find(kPageGenericDesktop, kUsageZ,  rid);
  axes_[3] = parser_->find(kPageGenericDesktop, kUsageRz, rid);

  // Some controllers expose the right stick as Rx/Ry instead of Z/Rz.
  if (axes_[2] == nullptr) axes_[2] = parser_->find(kPageGenericDesktop, kUsageRx, rid);
  if (axes_[3] == nullptr) axes_[3] = parser_->find(kPageGenericDesktop, kUsageRy, rid);

  // Analog triggers: Simulation page Brake/Accelerator first, then Rx/Ry.
  axes_[4] = parser_->find(kPageSimulation, kUsageBrake, rid);
  axes_[5] = parser_->find(kPageSimulation, kUsageAccelerator, rid);
  if (axes_[4] == nullptr && axes_[2] != parser_->find(kPageGenericDesktop, kUsageRx, rid))
    axes_[4] = parser_->find(kPageGenericDesktop, kUsageRx, rid);
  if (axes_[5] == nullptr && axes_[3] != parser_->find(kPageGenericDesktop, kUsageRy, rid))
    axes_[5] = parser_->find(kPageGenericDesktop, kUsageRy, rid);

  hat_ = parser_->find(kPageGenericDesktop, kUsageHat, rid);

  // Button block: contiguous 1-bit variable fields on usage page 0x09.
  for (size_t i = 0; i < parser_->fieldCount(); ++i) {
    const HidField& f = parser_->field(i);
    if (f.reportId != rid) continue;
    if (f.usagePage != kPageButton) continue;
    if (buttons_ == nullptr) buttons_ = &f;
    buttonBits_++;
  }

  // Consumer-page keys (Home / Back / Menu) - often in their own report.
  consumerCount_ = 0;
  consumerReportId_ = 0xFF;
  for (size_t i = 0; i < parser_->fieldCount() && consumerCount_ < kMaxConsumerKeys; ++i) {
    const HidField& f = parser_->field(i);
    if (f.usagePage != kPageConsumer || !f.isVariable()) continue;

    uint32_t bit = 0;
    switch (f.usage) {
      case kUsageConsumerHome: bit = MOMO_BTN_HOME; break;
      case kUsageConsumerBack: bit = MOMO_BTN_SELECT; break;
      case kUsageConsumerMenu: bit = MOMO_BTN_START; break;
      default: continue;
    }
    if (consumerReportId_ == 0xFF) consumerReportId_ = f.reportId;
    if (f.reportId != consumerReportId_) continue;
    consumer_[consumerCount_].field = &f;
    consumer_[consumerCount_].momoBit = bit;
    consumerCount_++;
  }

  resolved_ = true;
}

bool MomoMapper::decodeConsumer_(const uint8_t* payload, size_t len,
                                 MomoGamepadState& out) const {
  bool any = false;
  for (uint8_t i = 0; i < consumerCount_; ++i) {
    const HidField* f = consumer_[i].field;
    if (f == nullptr) continue;
    if (hidExtract(payload, len, *f) != 0) {
      out.buttons |= consumer_[i].momoBit;
    } else {
      out.buttons &= ~consumer_[i].momoBit;
    }
    any = true;
  }
  if (any) out.seq++;
  return any;
}

// ---- Decode ----------------------------------------------------------------

bool MomoMapper::decode(uint8_t reportId, const uint8_t* payload, size_t len,
                        MomoGamepadState& out) const {
  if (!ready() || payload == nullptr) return false;

  // Keys that arrive in a separate Consumer report are sticky: they are not
  // present in the main gamepad report, so carry them over.
  uint32_t consumerMask = 0;
  for (uint8_t i = 0; i < consumerCount_; ++i) consumerMask |= consumer_[i].momoBit;

  // reportId 0 with an ID-using descriptor means the transport did not tell us
  // which report this is (no Report Reference descriptor) - assume the main one.
  if (parser_->usesReportIds() && reportId != 0 && reportId != inputReportId_) {
    if (consumerCount_ > 0 && reportId == consumerReportId_) {
      return decodeConsumer_(payload, len, out);
    }
    return false;
  }

  MomoGamepadState s;
  s.battery = out.battery;
  s.seq     = out.seq + 1;
  s.buttons = out.buttons & consumerMask;

  // Sticks
  if (axes_[0]) s.lx = scaleAxis(hidExtract(payload, len, *axes_[0]), axes_[0]->logicalMin, axes_[0]->logicalMax);
  if (axes_[1]) s.ly = scaleAxis(hidExtract(payload, len, *axes_[1]), axes_[1]->logicalMin, axes_[1]->logicalMax);
  if (axes_[2]) s.rx = scaleAxis(hidExtract(payload, len, *axes_[2]), axes_[2]->logicalMin, axes_[2]->logicalMax);
  if (axes_[3]) s.ry = scaleAxis(hidExtract(payload, len, *axes_[3]), axes_[3]->logicalMin, axes_[3]->logicalMax);

  // Negating -512 would give 512, one count outside the -512..511 range.
  if (invertLeftY_)  s.ly = static_cast<int16_t>(s.ly == -512 ? 511 : -s.ly);
  if (invertRightY_) s.ry = static_cast<int16_t>(s.ry == -512 ? 511 : -s.ry);

  applyDeadzone(s.lx, s.ly, deadzone_);
  applyDeadzone(s.rx, s.ry, deadzone_);

  // Triggers
  if (axes_[4]) s.l2 = scaleTrigger(hidExtract(payload, len, *axes_[4]), axes_[4]->logicalMin, axes_[4]->logicalMax);
  if (axes_[5]) s.r2 = scaleTrigger(hidExtract(payload, len, *axes_[5]), axes_[5]->logicalMin, axes_[5]->logicalMax);
  if (s.l2 > trigThreshold_) s.buttons |= MOMO_BTN_L2;
  if (s.r2 > trigThreshold_) s.buttons |= MOMO_BTN_R2;

  // D-pad
  if (hat_) {
    s.dpad = hatToDpad(hidExtract(payload, len, *hat_), hat_->logicalMin, hat_->logicalMax);
  }

  // Buttons
  if (buttons_) {
    for (size_t i = 0; i < parser_->fieldCount(); ++i) {
      const HidField& f = parser_->field(i);
      if (f.reportId != inputReportId_ || f.usagePage != kPageButton) continue;
      if (hidExtract(payload, len, f) == 0) continue;
      const uint16_t hidBtn = f.usage;
      for (uint8_t m = 0; m < profile_->buttonCount; ++m) {
        if (profile_->buttons[m].hidButton == hidBtn) {
          s.buttons |= profile_->buttons[m].momoBit;
          break;
        }
      }
    }
  }

  // Consumer-page keys (Home / Back) shipped in a separate report on some
  // controllers are merged by MomoJoy::onReport(), not here.
  out = s;
  return true;
}

}  // namespace massmore
