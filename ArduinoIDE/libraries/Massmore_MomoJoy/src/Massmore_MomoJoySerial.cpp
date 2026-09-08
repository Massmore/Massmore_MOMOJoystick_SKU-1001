// Massmore_MomoJoy - human readable dumps for the CH343P serial console
#include "Massmore_MomoJoy.h"

#if defined(ARDUINO_ARCH_ESP32)

namespace massmore {

namespace {

const char* usageName(uint16_t page, uint16_t usage) {
  if (page == kPageButton) return "Button";
  if (page == kPageSimulation) {
    switch (usage) {
      case kUsageBrake: return "Brake (L2)";
      case kUsageAccelerator: return "Accelerator (R2)";
      default: return "Simulation";
    }
  }
  if (page == kPageConsumer) return "Consumer";
  if (page == kPageGenericDesktop) {
    switch (usage) {
      case kUsageX: return "X  (Left stick X)";
      case kUsageY: return "Y  (Left stick Y)";
      case kUsageZ: return "Z  (Right stick X)";
      case kUsageRx: return "Rx";
      case kUsageRy: return "Ry";
      case kUsageRz: return "Rz (Right stick Y)";
      case kUsageHat: return "Hat switch (D-Pad)";
      case kUsageSlider: return "Slider";
      default: return "GenericDesktop";
    }
  }
  return "Vendor/Other";
}

void printBtn(Stream& out, const char* name, bool on, bool& first) {
  if (!on) return;
  if (!first) out.print(' ');
  out.print(name);
  first = false;
}

}  // namespace

void MomoJoyClass::dumpReportMap(Stream& out) const {
  out.printf("---- HID Report Map (%u bytes) ----\n", static_cast<unsigned>(reportMapLen_));
  for (size_t i = 0; i < reportMapLen_; ++i) {
    out.printf("0x%02X, ", reportMap_[i]);
    if ((i % 12) == 11) out.println();
  }
  out.println();
  out.println(F("-----------------------------------"));
}

void MomoJoyClass::dumpDescriptorFields(Stream& out) const {
  out.println(F("---- Parsed input fields ----"));
  out.println(F("  rid  page  usage  bit-off  bits   logical range   name"));
  for (size_t i = 0; i < parser_.fieldCount(); ++i) {
    const HidField& f = parser_.field(i);
    out.printf("  %3u  0x%02X  0x%03X  %7u  %4u  %6ld..%-6ld  %s",
               f.reportId, f.usagePage, f.usage, f.bitOffset, f.bitSize,
               static_cast<long>(f.logicalMin), static_cast<long>(f.logicalMax),
               usageName(f.usagePage, f.usage));
    if (f.usagePage == kPageButton) out.printf(" %u", f.usage);
    out.println();
  }
  for (size_t i = 0; i < parser_.reportCount(); ++i) {
    const uint8_t rid = parser_.reportIdAt(i);
    out.printf("  report id %u -> %u byte payload\n", rid, parser_.reportSizeBytes(rid));
  }
  out.println(F("-----------------------------"));
}

void MomoJoyClass::printState(Stream& out) const {
  out.printf("L(%5d,%5d) R(%5d,%5d) L2=%4u R2=%4u DPAD=",
             state_.lx, state_.ly, state_.rx, state_.ry, state_.l2, state_.r2);
  out.print(state_.dpadUp() ? 'U' : '-');
  out.print(state_.dpadDown() ? 'D' : '-');
  out.print(state_.dpadLeft() ? 'L' : '-');
  out.print(state_.dpadRight() ? 'R' : '-');
  out.print(F(" BTN["));
  bool first = true;
  printBtn(out, "A", state_.pressed(MOMO_BTN_A), first);
  printBtn(out, "B", state_.pressed(MOMO_BTN_B), first);
  printBtn(out, "X", state_.pressed(MOMO_BTN_X), first);
  printBtn(out, "Y", state_.pressed(MOMO_BTN_Y), first);
  printBtn(out, "L1", state_.pressed(MOMO_BTN_L1), first);
  printBtn(out, "R1", state_.pressed(MOMO_BTN_R1), first);
  printBtn(out, "L2", state_.pressed(MOMO_BTN_L2), first);
  printBtn(out, "R2", state_.pressed(MOMO_BTN_R2), first);
  printBtn(out, "L3", state_.pressed(MOMO_BTN_L3), first);
  printBtn(out, "R3", state_.pressed(MOMO_BTN_R3), first);
  printBtn(out, "SELECT", state_.pressed(MOMO_BTN_SELECT), first);
  printBtn(out, "START", state_.pressed(MOMO_BTN_START), first);
  printBtn(out, "HOME", state_.pressed(MOMO_BTN_HOME), first);
  printBtn(out, "M1", state_.pressed(MOMO_BTN_M1), first);
  printBtn(out, "M2", state_.pressed(MOMO_BTN_M2), first);
  printBtn(out, "C", state_.pressed(MOMO_BTN_C), first);
  printBtn(out, "Z", state_.pressed(MOMO_BTN_Z), first);
  printBtn(out, "CAPTURE", state_.pressed(MOMO_BTN_CAPTURE), first);
  out.print(']');
  if (state_.battery != 0xFF) out.printf(" BAT=%u%%", state_.battery);
  out.println();
}

void MomoJoyClass::printStateChanges(Stream& out) {
  static uint32_t lastButtons = 0;
  static uint8_t  lastDpad = 0;
  static int16_t  lastLx = 0, lastLy = 0, lastRx = 0, lastRy = 0;
  static uint16_t lastL2 = 0, lastR2 = 0;

  const bool sticksMoved =
      abs(state_.lx - lastLx) > 8 || abs(state_.ly - lastLy) > 8 ||
      abs(state_.rx - lastRx) > 8 || abs(state_.ry - lastRy) > 8 ||
      abs(static_cast<int>(state_.l2) - static_cast<int>(lastL2)) > 16 ||
      abs(static_cast<int>(state_.r2) - static_cast<int>(lastR2)) > 16;

  if (state_.buttons != lastButtons || state_.dpad != lastDpad || sticksMoved) {
    printState(out);
    lastButtons = state_.buttons;
    lastDpad = state_.dpad;
    lastLx = state_.lx; lastLy = state_.ly;
    lastRx = state_.rx; lastRy = state_.ry;
    lastL2 = state_.l2; lastR2 = state_.r2;
  }
}


// ---- shared button name table ---------------------------------------------
namespace {
struct BtnName { uint32_t bit; const char* name; };
const BtnName kBtnNames[] = {
    {MOMO_BTN_A, "A"},           {MOMO_BTN_B, "B"},
    {MOMO_BTN_X, "X"},           {MOMO_BTN_Y, "Y"},
    {MOMO_BTN_L1, "L1"},         {MOMO_BTN_R1, "R1"},
    {MOMO_BTN_L2, "L2"},         {MOMO_BTN_R2, "R2"},
    {MOMO_BTN_SELECT, "SELECT"}, {MOMO_BTN_START, "START"},
    {MOMO_BTN_HOME, "HOME"},     {MOMO_BTN_L3, "L3"},
    {MOMO_BTN_R3, "R3"},         {MOMO_BTN_M1, "M1"},
    {MOMO_BTN_M2, "M2"},         {MOMO_BTN_C, "C"},
    {MOMO_BTN_Z, "Z"},           {MOMO_BTN_CAPTURE, "CAPTURE"},
};
const uint8_t kBtnNameCount = sizeof(kBtnNames) / sizeof(kBtnNames[0]);
}  // namespace

const char* MomoJoyClass::buttonName(uint32_t bit) {
  for (uint8_t i = 0; i < kBtnNameCount; ++i) {
    if (kBtnNames[i].bit == bit) return kBtnNames[i].name;
  }
  return "?";
}

// Machine readable single line: easy to plot or log from a PC script.
void MomoJoyClass::printCsv(Stream& out) const {
  out.printf("%d,%d,%d,%d,%u,%u,%u,%lu,%u\n",
             state_.lx, state_.ly, state_.rx, state_.ry,
             state_.l2, state_.r2, state_.dpad,
             static_cast<unsigned long>(state_.buttons), state_.battery);
}

// JSON line for the Massmore Web Serial Monitor.
void MomoJoyClass::printJson(Stream& out) const {
  out.printf("{\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"l2\":%u,\"r2\":%u,"
             "\"dpad\":%u,\"buttons\":%lu,\"battery\":%u,\"rate\":%u,\"btn\":[",
             state_.lx, state_.ly, state_.rx, state_.ry,
             state_.l2, state_.r2, state_.dpad,
             static_cast<unsigned long>(state_.buttons),
             state_.battery, rateHz_);
  bool first = true;
  for (uint8_t i = 0; i < kBtnNameCount; ++i) {
    if (!state_.pressed(kBtnNames[i].bit)) continue;
    if (!first) out.print(',');
    out.print('"');
    out.print(kBtnNames[i].name);
    out.print('"');
    first = false;
  }
  out.println("]}");
}

}  // namespace massmore

#endif  // ARDUINO_ARCH_ESP32
