// =============================================================================
//  Massmore_MomoJoy - BLE HID gamepad host library for ESP32 / ESP32-S3
//  (c) Massmore Biz Co., Ltd.  -  MIT License  -  https://massmore.shop
//
//  Target controller : MOMO Joystick Controller, mode switch on "D" (HID & APP / Android BLE)
//  Also works with   : most BLE HID gamepads, because the HID Report Map is
//                      parsed at runtime instead of hard-coding a byte layout.
//
//  Toolchains        : Arduino IDE  - ESP32 core 3.x
//                      PlatformIO   - pioarduino platform-espressif32 (core 3.x)
//  Dependency        : NimBLE-Arduino 2.x (bundled with this project)
//
//  Value ranges are Bluepad32-compatible on purpose:
//      sticks   -512 .. 511
//      triggers     0 .. 1023
//  so an existing sketch only needs its accessors renamed.
//
//  NOTE: keep this file ASCII-only. Non-ASCII characters break the Arduino
//  automatic prototype generator. Thai explanations live in README.md.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "core/MomoGamepadState.h"
#include "core/MomoHidParser.h"
#include "core/MomoHidUsage.h"
#include "core/MomoMapper.h"

#define MASSMORE_MOMOJOY_VERSION       "2.1.0"
#define MASSMORE_MOMOJOY_VERSION_MAJOR 2
#define MASSMORE_MOMOJOY_VERSION_MINOR 1
#define MASSMORE_MOMOJOY_VERSION_PATCH 0

namespace massmore {

// ---------------------------------------------------------------------------
//  One BLE HID gamepad seen during a discovery scan (scanDevices()).
//  ข้อมูลจอยหนึ่งตัวที่เจอตอนสแกน
// ---------------------------------------------------------------------------
struct MomoFoundDevice {
  char     name[32];      // advertised name, "" when the pad does not send one
  char     address[18];   // "aa:bb:cc:dd:ee:ff"
  int8_t   rssi;          // dBm, closer to 0 = stronger
  uint16_t appearance;    // 0x03C4 = gamepad
};

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------
struct MomoJoyOptions {
  // Local BLE name of the ESP32 (shown while pairing).
  const char* localName = "MOMO";

  // Connect only to a controller whose advertised name CONTAINS this string.
  // nullptr = accept any device advertising the HID service (0x1812).
  const char* nameFilter = nullptr;

  // Connect only to this MAC ("AA:BB:CC:DD:EE:FF"). nullptr = any.
  const char* addressFilter = nullptr;

  bool     autoReconnect    = true;   // keep scanning after a disconnect
  bool     clearBondsOnBoot = false;  // forget every pairing on every boot
  uint16_t deadzone         = 24;     // radial dead zone, output units (0..512)
  uint16_t connectTimeoutMs = 10000;  // give up on a connection attempt after
  uint16_t triggerThreshold = 64;     // L2/R2 value that also sets the L2/R2 bit
  int8_t   txPowerDbm       = 9;      // BLE TX power (-12 .. +9 dBm)

  // Button numbering profile. nullptr = kProfileMomo.
  const MomoProfile* profile = &kProfileAndroidGamepad;

  // false = begin() initialises BLE but does NOT start scanning, so you can
  // call scanDevices() first and choose which controller to connect to.
  // false = begin() เตรียม BLE ไว้แต่ยังไม่สแกน เพื่อให้เรียก scanDevices() เลือกจอยเองก่อน
  bool autoStart = true;

  bool verbose = true;                // log progress to Serial
};

// ---------------------------------------------------------------------------
//  Callback types
// ---------------------------------------------------------------------------
typedef void (*MomoConnectCb)(const char* name, const char* address);
typedef void (*MomoDisconnectCb)();
typedef void (*MomoRawReportCb)(uint8_t reportId, const uint8_t* data, size_t len);
typedef void (*MomoButtonCb)(uint32_t pressedMask, uint32_t releasedMask);
typedef void (*MomoDpadCb)(uint8_t dpadMask);

// ---------------------------------------------------------------------------
//  Main class. Use the ready-made global object `MomoJoy`.
// ---------------------------------------------------------------------------
class MomoJoyClass {
 public:
  // ==== lifecycle ==========================================================
  bool begin(const MomoJoyOptions& options = MomoJoyOptions());
  bool begin(const char* nameFilter);          // shortcut: filter by name
  void end();

  // Must be called every loop(). Never blocks.
  void update();

  bool isConnected() const { return connected_; }
  bool isScanning()  const { return scanning_; }

  void forgetBonds();      // drop every stored pairing (then re-pair the pad)
  void disconnect();       // ask the controller to disconnect
  bool rescan();           // force a new scan right now

  // ==== choosing between several controllers ===============================
  // Scan WITHOUT connecting and list every BLE HID gamepad in range. Blocks
  // for `ms`, returns how many were found (at most maxFound()).
  // สแกนดูว่ามีจอยกี่ตัวรอบ ๆ โดยยังไม่เชื่อมต่อ ใช้ตอนอยากรู้ MAC ของแต่ละตัว
  uint8_t scanDevices(uint32_t ms = 5000);
  uint8_t foundCount() const { return foundCount_; }
  static uint8_t maxFound() { return kMaxFound; }
  const MomoFoundDevice& foundDevice(uint8_t index) const;

  // Lock onto exactly one controller by MAC, then call rescan().
  // Pass nullptr to accept any gamepad again.
  // ล็อกจอยด้วย MAC แล้วเรียก rescan() ส่ง nullptr เพื่อกลับไปรับจอยตัวไหนก็ได้
  void setAddressFilter(const char* address);
  void setNameFilter(const char* name);

  // Shortcut: lock onto this MAC and start looking for it right away.
  // ทางลัด: ล็อก MAC แล้วเริ่มหาเลย
  bool connectToAddress(const char* address);

  static const char* version() { return MASSMORE_MOMOJOY_VERSION; }

  // ==== raw state ==========================================================
  const MomoGamepadState& state() const { return state_; }

  // ==== buttons ============================================================
  bool pressed(uint32_t mask)      const { return (state_.buttons & mask) != 0; }
  bool justPressed(uint32_t mask)  const { return (edgePressed_  & mask) != 0; }
  bool justReleased(uint32_t mask) const { return (edgeReleased_ & mask) != 0; }
  bool anyButton()                 const { return state_.buttons != 0; }
  uint32_t buttons()               const { return state_.buttons; }

  // How long (ms) the button has been held. 0 when it is not pressed.
  uint32_t heldMs(uint32_t mask) const;
  // true once, when the button has been held for at least `ms`.
  bool longPressed(uint32_t mask, uint32_t ms) const {
    return pressed(mask) && heldMs(mask) >= ms;
  }

  // ==== sticks and triggers ================================================
  int16_t  lx() const { return state_.lx; }   // -512 .. 511
  int16_t  ly() const { return state_.ly; }
  int16_t  rx() const { return state_.rx; }
  int16_t  ry() const { return state_.ry; }
  uint16_t l2() const { return state_.l2; }   //    0 .. 1023
  uint16_t r2() const { return state_.r2; }

  // Normalised -1.0 .. +1.0 / 0.0 .. 1.0 versions, handy for motor control.
  float lxf() const { return state_.lx / 512.0f; }
  float lyf() const { return state_.ly / 512.0f; }
  float rxf() const { return state_.rx / 512.0f; }
  float ryf() const { return state_.ry / 512.0f; }
  float l2f() const { return state_.l2 / 1023.0f; }
  float r2f() const { return state_.r2 / 1023.0f; }

  // Rescale a stick into your own range, e.g. mapStick(ly(), 255) -> -255..255
  static int32_t mapStick(int16_t v, int32_t outMax) {
    return (static_cast<int32_t>(v) * outMax) / 512;
  }

  // ==== D-pad ==============================================================
  uint8_t dpad()      const { return state_.dpad; }
  bool dpadUp()       const { return state_.dpadUp(); }
  bool dpadDown()     const { return state_.dpadDown(); }
  bool dpadLeft()     const { return state_.dpadLeft(); }
  bool dpadRight()    const { return state_.dpadRight(); }
  bool dpadJustChanged() const { return dpadChanged_; }

  // ==== controller info ====================================================
  uint8_t     battery()     const { return state_.battery; }  // 0..100, 0xFF unknown
  const char* peerName()    const { return peerName_; }
  const char* peerAddress() const { return peerAddr_; }
  int         rssi()        const;                            // dBm, 0 = unknown

  // ==== link statistics (used by the Factory Test) =========================
  uint32_t packets()        const { return packets_; }
  uint16_t reportRateHz()   const { return rateHz_; }
  uint32_t msSinceReport()  const { return packets_ ? (millis() - lastReportMs_) : 0; }

  // ==== tuning =============================================================
  void setDeadzone(uint16_t dz)         { opts_.deadzone = dz; mapper_.setDeadzone(dz); }
  uint16_t deadzone() const             { return opts_.deadzone; }
  void setProfile(const MomoProfile* p) { opts_.profile = p; mapper_.setProfile(p); }
  const MomoProfile* profile() const    { return mapper_.profile(); }
  void setTriggerThreshold(uint16_t t)  { opts_.triggerThreshold = t; mapper_.setTriggerThreshold(t); }
  void setInvertLeftY(bool inv)         { mapper_.setInvertLeftY(inv); }
  void setInvertRightY(bool inv)        { mapper_.setInvertRightY(inv); }

  // ==== callbacks ==========================================================
  void onConnect(MomoConnectCb cb)       { connectCb_    = cb; }
  void onDisconnect(MomoDisconnectCb cb) { disconnectCb_ = cb; }
  void onRawReport(MomoRawReportCb cb)   { rawCb_        = cb; }
  void onButton(MomoButtonCb cb)         { buttonCb_     = cb; }
  void onDpad(MomoDpadCb cb)             { dpadCb_       = cb; }

  // ==== introspection / calibration ========================================
  const MomoHidParser& descriptor() const { return parser_; }
  const uint8_t* reportMap()        const { return reportMap_; }
  size_t         reportMapLen()     const { return reportMapLen_; }

  // ==== pretty printing (Massmore_MomoJoySerial.cpp) =======================
  void dumpReportMap(Stream& out) const;        // raw 0x2A4B bytes
  void dumpDescriptorFields(Stream& out) const; // parsed field table
  void printState(Stream& out) const;           // one human readable line
  void printStateChanges(Stream& out);          // print only when something moves
  void printCsv(Stream& out) const;             // lx,ly,rx,ry,l2,r2,dpad,buttons
  void printJson(Stream& out) const;            // {"lx":..} for the web monitor
  static const char* buttonName(uint32_t bit);  // MOMO_BTN_A -> "A"

  // ==== internal: called from the NimBLE task. Do not call directly. =======
  void _pushReport(uint8_t reportId, const uint8_t* data, size_t len);
  void _onBleConnect();
  void _onBleDisconnect();
  void _onDeviceFound(const char* name, const char* address);

 private:
  bool startScan_();
  bool connectToPeer_();
  bool discoverHid_();
  void processQueue_();
  void resetState_();

  MomoJoyOptions   opts_;
  MomoHidParser    parser_;
  MomoMapper       mapper_;
  MomoGamepadState state_;

  uint32_t prevButtons_  = 0;
  uint32_t edgePressed_  = 0;
  uint32_t edgeReleased_ = 0;
  uint8_t  prevDpad_     = 0;
  bool     dpadChanged_  = false;
  uint32_t pressStartMs_[32] = {0};

  uint8_t  reportMap_[512];
  size_t   reportMapLen_ = 0;

  static const uint8_t kMaxFound = 8;
  MomoFoundDevice found_[kMaxFound] = {};
  uint8_t         foundCount_ = 0;
  // Filters are copied in, so a caller may pass a temporary string.
  char addrFilterBuf_[18] = {0};
  char nameFilterBuf_[32] = {0};

  char peerName_[32] = {0};
  char peerAddr_[18] = {0};

  uint32_t packets_      = 0;
  uint32_t lastReportMs_ = 0;
  uint32_t rateWindowMs_ = 0;
  uint16_t rateCount_    = 0;
  uint16_t rateHz_       = 0;

  volatile bool connected_    = false;
  volatile bool scanning_     = false;
  volatile bool wantConnect_  = false;
  volatile bool needDiscover_ = false;
  volatile bool lostLink_     = false;
  uint32_t      lastScanKick_ = 0;
  bool          started_      = false;

  MomoConnectCb    connectCb_    = nullptr;
  MomoDisconnectCb disconnectCb_ = nullptr;
  MomoRawReportCb  rawCb_        = nullptr;
  MomoButtonCb     buttonCb_     = nullptr;
  MomoDpadCb       dpadCb_       = nullptr;
};

}  // namespace massmore

// Ready-made instance. Sketches normally do: using namespace massmore;
extern massmore::MomoJoyClass MomoJoy;
