// =============================================================================
//  Massmore_MomoJoy - BLE HID host implementation
//  NimBLE-Arduino 2.x  /  arduino-esp32 core 3.x  /  ESP32 + ESP32-S3
// =============================================================================
#include "Massmore_MomoJoy.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <strings.h>   // strcasecmp

using namespace massmore;

massmore::MomoJoyClass MomoJoy;

namespace {

// ---- BLE UUIDs -------------------------------------------------------------
const uint16_t kSvcHid          = 0x1812;
const uint16_t kSvcBattery      = 0x180F;
const uint16_t kChrReportMap    = 0x2A4B;
const uint16_t kChrReport       = 0x2A4D;
const uint16_t kChrProtocolMode = 0x2A4E;
const uint16_t kChrHidControl   = 0x2A4C;
const uint16_t kChrBatteryLevel = 0x2A19;
const uint16_t kDscReportRef    = 0x2908;

const uint16_t kAppearanceGamepad = 0x03C4;
const uint16_t kAppearanceHidMask = 0x03C0;

// ---- Report queue (NimBLE host task -> loop()) -----------------------------
struct RawReport {
  uint8_t id;
  uint8_t len;
  uint8_t data[24];
};

QueueHandle_t g_queue = nullptr;

// ---- BLE globals -----------------------------------------------------------
// While true the scan callback only observes: scanDevices() is listing what is
// in range and must not connect to anything.
// ระหว่าง true คือกำลังสแกนดูรายชื่อ ห้ามเชื่อมต่ออัตโนมัติ
volatile bool    g_discovering  = false;

NimBLEClient*    g_client       = nullptr;
NimBLEAddress    g_target;
bool             g_haveTarget   = false;
volatile uint8_t g_batteryLevel = 0xFF;

// Map: characteristic handle -> HID report ID (input reports only).
struct ReportBinding {
  uint16_t handle;
  uint8_t  reportId;
};
ReportBinding g_bindings[8];
uint8_t       g_bindingCount = 0;

uint8_t reportIdForHandle(uint16_t handle) {
  for (uint8_t i = 0; i < g_bindingCount; ++i) {
    if (g_bindings[i].handle == handle) return g_bindings[i].reportId;
  }
  return 0;
}

void notifyCb(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool) {
  if (chr == nullptr) return;
  if (chr->getUUID() == NimBLEUUID(kChrBatteryLevel)) {
    if (len > 0) g_batteryLevel = data[0];
    return;
  }
  MomoJoy._pushReport(reportIdForHandle(chr->getHandle()), data, len);
}

// ---- Scan callbacks (NimBLE 2.x API) ---------------------------------------
class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  const MomoJoyOptions* opts = nullptr;

  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (dev == nullptr || opts == nullptr) return;
    if (g_discovering) return;                // scanDevices() is just looking
    if (g_haveTarget) return;                 // already picked one

    const std::string name = dev->getName();
    const std::string addr = dev->getAddress().toString();

    if (opts->addressFilter != nullptr) {
      if (strcasecmp(opts->addressFilter, addr.c_str()) != 0) return;
    }
    if (opts->nameFilter != nullptr) {
      if (name.find(opts->nameFilter) == std::string::npos) return;
    } else {
      const bool     advHid = dev->isAdvertisingService(NimBLEUUID(kSvcHid));
      const uint16_t app    = dev->getAppearance();
      const bool     hidLooking = (app == kAppearanceGamepad) ||
                                  ((app & 0xFFC0) == kAppearanceHidMask);
      if (!advHid && !hidLooking) return;
    }

    g_target     = dev->getAddress();
    g_haveTarget = true;
    NimBLEDevice::getScan()->stop();
    MomoJoy._onDeviceFound(name.c_str(), addr.c_str());
  }

  void onScanEnd(const NimBLEScanResults&, int) override {
    // Nothing to do: update() restarts the scan when it needs to.
  }
};

ScanCallbacks g_scanCb;

// ---- Client callbacks (NimBLE 2.x API) -------------------------------------
class ClientCallbacks : public NimBLEClientCallbacks {
 public:
  bool verbose = true;

  void onConnect(NimBLEClient* c) override {
    // Fast, stable link: 15 ms interval, 3 s supervision timeout.
    c->updateConnParams(12, 12, 0, 300);
    MomoJoy._onBleConnect();
  }

  void onDisconnect(NimBLEClient*, int) override {
    MomoJoy._onBleDisconnect();
  }

  void onConnectFail(NimBLEClient*, int reason) override {
    if (verbose) Serial.printf("[MomoJoy] connect failed, reason 0x%02X\n", reason);
    MomoJoy._onBleDisconnect();
  }

  bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params* p) override {
    return p->itvl_min >= 6 && p->itvl_max <= 400;
  }

  // "Just Works" pairing: no display, no keyboard on the ESP32 side.
  void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
    NimBLEDevice::injectPassKey(connInfo, 0);
  }

  void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t) override {
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (!connInfo.isEncrypted() && verbose) {
      // Almost always a stale bond on one of the two sides. Clearing the bond
      // on the ESP32 and re-pairing the controller fixes it.
      Serial.println(F("[MomoJoy] encryption failed - try MomoJoy.forgetBonds()"));
    }
  }
};

ClientCallbacks g_clientCb;

}  // namespace

// =============================================================================
namespace massmore {

// ---- lifecycle -------------------------------------------------------------
bool MomoJoyClass::begin(const MomoJoyOptions& options) {
  if (started_) return true;
  opts_ = options;

  mapper_.setProfile(opts_.profile);
  mapper_.setDeadzone(opts_.deadzone);
  mapper_.setTriggerThreshold(opts_.triggerThreshold);

  if (g_queue == nullptr) {
    g_queue = xQueueCreate(16, sizeof(RawReport));
    if (g_queue == nullptr) return false;
  }

  NimBLEDevice::init(opts_.localName ? opts_.localName : "MOMO");
  NimBLEDevice::setPower(opts_.txPowerDbm);
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  if (opts_.clearBondsOnBoot) NimBLEDevice::deleteAllBonds();

  g_scanCb.opts      = &opts_;
  g_clientCb.verbose = opts_.verbose;
  started_           = true;

  // Copy the filters in, so the caller may pass a temporary string.
  if (opts_.addressFilter) setAddressFilter(opts_.addressFilter);
  if (opts_.nameFilter)    setNameFilter(opts_.nameFilter);

  if (opts_.verbose) {
    Serial.printf("[MomoJoy] Massmore_MomoJoy v%s - BLE HID host started\n",
                  MASSMORE_MOMOJOY_VERSION);
    if (opts_.addressFilter) {
      Serial.printf("[MomoJoy] locked to address %s\n", opts_.addressFilter);
    }
  }

  if (!opts_.autoStart) {
    if (opts_.verbose) Serial.println(F("[MomoJoy] idle - call scanDevices() or rescan()"));
    return true;
  }
  return startScan_();
}

bool MomoJoyClass::begin(const char* nameFilter) {
  MomoJoyOptions o;
  o.nameFilter = nameFilter;
  return begin(o);
}

void MomoJoyClass::end() {
  if (!started_) return;
  disconnect();
  NimBLEDevice::deinit(true);
  started_ = false;
  connected_ = false;
  scanning_  = false;
}

// ---- scanning --------------------------------------------------------------
bool MomoJoyClass::startScan_() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCb, false);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setActiveScan(true);
  scan->clearResults();
  // NimBLE 2.x: duration is in MILLISECONDS, 0 = scan until stopped.
  scanning_     = scan->start(0, false, true);
  lastScanKick_ = millis();
  if (opts_.verbose && scanning_) {
    Serial.println(F("[MomoJoy] scanning for a BLE HID gamepad..."));
  }
  return scanning_;
}

bool MomoJoyClass::rescan() {
  if (!started_) return false;
  g_discovering = false;
  g_haveTarget  = false;
  return startScan_();
}

// ---- events from the NimBLE task ------------------------------------------
void MomoJoyClass::_onDeviceFound(const char* name, const char* address) {
  strncpy(peerName_, name ? name : "", sizeof(peerName_) - 1);
  peerName_[sizeof(peerName_) - 1] = 0;
  strncpy(peerAddr_, address ? address : "", sizeof(peerAddr_) - 1);
  peerAddr_[sizeof(peerAddr_) - 1] = 0;
  scanning_    = false;
  wantConnect_ = true;
}

void MomoJoyClass::_onBleConnect()    { needDiscover_ = true; }
void MomoJoyClass::_onBleDisconnect() { connected_ = false; lostLink_ = true; }

void MomoJoyClass::_pushReport(uint8_t reportId, const uint8_t* data, size_t len) {
  if (g_queue == nullptr || data == nullptr) return;
  RawReport r;
  r.id  = reportId;
  r.len = static_cast<uint8_t>(len > sizeof(r.data) ? sizeof(r.data) : len);
  memcpy(r.data, data, r.len);
  // Called from the NimBLE host task (not an ISR): never block the stack.
  if (xQueueSend(g_queue, &r, 0) != pdTRUE) {
    RawReport drop;
    xQueueReceive(g_queue, &drop, 0);   // drop the oldest, keep the newest
    xQueueSend(g_queue, &r, 0);
  }
}

// ---- connect ---------------------------------------------------------------
bool MomoJoyClass::connectToPeer_() {
  if (!g_haveTarget) return false;

  if (g_client == nullptr) {
    g_client = NimBLEDevice::createClient();
    if (g_client == nullptr) return false;
    g_client->setClientCallbacks(&g_clientCb, false);
    g_client->setConnectionParams(12, 12, 0, 300);
  }
  // NimBLE 2.x: connect timeout is in milliseconds.
  g_client->setConnectTimeout(opts_.connectTimeoutMs);

  if (opts_.verbose) {
    Serial.printf("[MomoJoy] connecting to \"%s\" [%s]\n", peerName_, peerAddr_);
  }

  if (!g_client->connect(g_target, true, false, true)) {
    if (opts_.verbose) Serial.println(F("[MomoJoy] connect failed"));
    return false;
  }
  needDiscover_ = true;   // belt and braces; onConnect() also sets it
  return true;
}

// ---- HID discovery ---------------------------------------------------------
bool MomoJoyClass::discoverHid_() {
  if (g_client == nullptr || !g_client->isConnected()) return false;

  // Bond / encrypt first: most controllers hide their reports otherwise.
  if (!g_client->secureConnection()) {
    if (opts_.verbose) Serial.println(F("[MomoJoy] pairing failed"));
    return false;
  }

  NimBLERemoteService* hid = g_client->getService(NimBLEUUID(kSvcHid));
  if (hid == nullptr) {
    if (opts_.verbose) Serial.println(F("[MomoJoy] no HID service (0x1812) on this device"));
    return false;
  }

  // 1) Report protocol (not boot protocol).
  NimBLERemoteCharacteristic* pm = hid->getCharacteristic(NimBLEUUID(kChrProtocolMode));
  if (pm != nullptr) {
    const uint8_t mode = 0x01;
    pm->writeValue(&mode, 1, false);
  }

  // 2) Wake the controller up if it advertised as suspended.
  NimBLERemoteCharacteristic* cp = hid->getCharacteristic(NimBLEUUID(kChrHidControl));
  if (cp != nullptr) {
    const uint8_t exitSuspend = 0x00;
    cp->writeValue(&exitSuspend, 1, false);
  }

  // 3) Report Map -> descriptor parser.
  NimBLERemoteCharacteristic* rm = hid->getCharacteristic(NimBLEUUID(kChrReportMap));
  if (rm == nullptr) {
    if (opts_.verbose) Serial.println(F("[MomoJoy] no Report Map characteristic (0x2A4B)"));
    return false;
  }
  NimBLEAttValue map = rm->readValue();
  reportMapLen_ = map.length() > sizeof(reportMap_) ? sizeof(reportMap_) : map.length();
  memcpy(reportMap_, map.data(), reportMapLen_);

  parser_.parse(reportMap_, reportMapLen_);
  mapper_.setParser(&parser_);

  if (opts_.verbose) {
    Serial.printf("[MomoJoy] report map: %u bytes, %u input fields, %u report id(s)\n",
                  static_cast<unsigned>(reportMapLen_),
                  static_cast<unsigned>(parser_.fieldCount()),
                  static_cast<unsigned>(parser_.reportCount()));
  }

  // 4) Subscribe to every INPUT report characteristic.
  g_bindingCount = 0;
  uint8_t subscribed = 0;
  for (auto* chr : hid->getCharacteristics(true)) {
    if (chr == nullptr) continue;
    if (chr->getUUID() != NimBLEUUID(kChrReport)) continue;
    if (!chr->canNotify()) continue;

    uint8_t reportId = 0;
    NimBLERemoteDescriptor* ref = chr->getDescriptor(NimBLEUUID(kDscReportRef));
    if (ref != nullptr) {
      NimBLEAttValue v = ref->readValue();
      if (v.length() >= 2) {
        if (v[1] != 0x01) continue;   // 1 = Input, 2 = Output, 3 = Feature
        reportId = v[0];
      }
    }

    if (chr->subscribe(true, notifyCb)) {
      if (g_bindingCount < 8) {
        g_bindings[g_bindingCount].handle   = chr->getHandle();
        g_bindings[g_bindingCount].reportId = reportId;
        g_bindingCount++;
      }
      subscribed++;
    }
  }

  if (opts_.verbose) {
    Serial.printf("[MomoJoy] subscribed to %u input report(s)\n", subscribed);
  }
  if (subscribed == 0) return false;

  // 5) Battery level (optional).
  NimBLERemoteService* bat = g_client->getService(NimBLEUUID(kSvcBattery));
  if (bat != nullptr) {
    NimBLERemoteCharacteristic* lvl = bat->getCharacteristic(NimBLEUUID(kChrBatteryLevel));
    if (lvl != nullptr) {
      if (lvl->canRead()) {
        NimBLEAttValue v = lvl->readValue();
        if (v.length() > 0) g_batteryLevel = v[0];
      }
      if (lvl->canNotify()) lvl->subscribe(true, notifyCb);
    }
  }

  resetState_();
  connected_    = true;
  lastReportMs_ = millis();
  rateWindowMs_ = millis();

  if (connectCb_) connectCb_(peerName_, peerAddr_);
  if (opts_.verbose) Serial.println(F("[MomoJoy] ready"));
  return true;
}

// ---- per-loop work ---------------------------------------------------------
void MomoJoyClass::resetState_() {
  state_        = MomoGamepadState();
  prevButtons_  = 0;
  prevDpad_     = 0;
  edgePressed_  = 0;
  edgeReleased_ = 0;
  dpadChanged_  = false;
  packets_      = 0;
  rateCount_    = 0;
  rateHz_       = 0;
  for (uint8_t i = 0; i < 32; ++i) pressStartMs_[i] = 0;
}

void MomoJoyClass::processQueue_() {
  RawReport r;
  edgePressed_  = 0;
  edgeReleased_ = 0;
  dpadChanged_  = false;

  while (g_queue != nullptr && xQueueReceive(g_queue, &r, 0) == pdTRUE) {
    if (rawCb_) rawCb_(r.id, r.data, r.len);

    MomoGamepadState next = state_;
    if (!mapper_.decode(r.id, r.data, r.len, next)) continue;

    next.battery = g_batteryLevel;
    state_       = next;

    const uint32_t now     = millis();
    const uint32_t changed = state_.buttons ^ prevButtons_;
    edgePressed_  |= changed & state_.buttons;
    edgeReleased_ |= changed & prevButtons_;

    for (uint8_t b = 0; b < 32; ++b) {
      const uint32_t bit = 1u << b;
      if (changed & bit) pressStartMs_[b] = (state_.buttons & bit) ? now : 0;
    }

    if (state_.dpad != prevDpad_) dpadChanged_ = true;
    prevButtons_  = state_.buttons;
    prevDpad_     = state_.dpad;
    packets_++;
    rateCount_++;
    lastReportMs_ = now;
  }

  // Report rate, refreshed once per second.
  const uint32_t now = millis();
  if (now - rateWindowMs_ >= 1000) {
    rateHz_       = rateCount_;
    rateCount_    = 0;
    rateWindowMs_ = now;
  }

  if (buttonCb_ && (edgePressed_ || edgeReleased_)) buttonCb_(edgePressed_, edgeReleased_);
  if (dpadCb_ && dpadChanged_) dpadCb_(state_.dpad);
}

void MomoJoyClass::update() {
  if (!started_) return;

  if (lostLink_) {
    lostLink_      = false;
    connected_     = false;
    g_haveTarget   = false;
    g_bindingCount = 0;
    g_batteryLevel = 0xFF;
    resetState_();
    if (opts_.verbose) Serial.println(F("[MomoJoy] disconnected"));
    if (disconnectCb_) disconnectCb_();
    if (opts_.autoReconnect) startScan_();
    return;
  }

  if (wantConnect_) {
    wantConnect_ = false;
    if (!connectToPeer_()) {
      g_haveTarget = false;
      if (opts_.autoReconnect) startScan_();
    }
    return;
  }

  if (needDiscover_) {
    needDiscover_ = false;
    if (!discoverHid_()) {
      if (g_client && g_client->isConnected()) g_client->disconnect();
      g_haveTarget = false;
      if (opts_.autoReconnect) startScan_();
    }
    return;
  }

  if (connected_) {
    processQueue_();
    return;
  }

  // Watchdog: restart scanning if it stalled.
  if (opts_.autoReconnect && !scanning_ && !g_haveTarget &&
      (millis() - lastScanKick_) > 5000) {
    startScan_();
  }
}

// ---- helpers ---------------------------------------------------------------
uint32_t MomoJoyClass::heldMs(uint32_t mask) const {
  uint32_t oldest = 0;
  const uint32_t now = millis();
  for (uint8_t b = 0; b < 32; ++b) {
    const uint32_t bit = 1u << b;
    if (!(mask & bit) || !(state_.buttons & bit) || pressStartMs_[b] == 0) continue;
    const uint32_t held = now - pressStartMs_[b];
    if (held > oldest) oldest = held;
  }
  return oldest;
}

int MomoJoyClass::rssi() const {
  if (g_client == nullptr || !g_client->isConnected()) return 0;
  return g_client->getRssi();
}

void MomoJoyClass::forgetBonds() {
  NimBLEDevice::deleteAllBonds();
  if (opts_.verbose) Serial.println(F("[MomoJoy] all bonds deleted"));
}

void MomoJoyClass::disconnect() {
  if (g_client && g_client->isConnected()) g_client->disconnect();
}


// ---- choosing between several controllers ----------------------------------
const MomoFoundDevice& MomoJoyClass::foundDevice(uint8_t index) const {
  static const MomoFoundDevice kEmpty = {};
  if (index >= foundCount_) return kEmpty;
  return found_[index];
}

void MomoJoyClass::setAddressFilter(const char* address) {
  if (address == nullptr || address[0] == 0) {
    addrFilterBuf_[0] = 0;
    opts_.addressFilter = nullptr;
    return;
  }
  strncpy(addrFilterBuf_, address, sizeof(addrFilterBuf_) - 1);
  addrFilterBuf_[sizeof(addrFilterBuf_) - 1] = 0;
  opts_.addressFilter = addrFilterBuf_;
}

void MomoJoyClass::setNameFilter(const char* name) {
  if (name == nullptr || name[0] == 0) {
    nameFilterBuf_[0] = 0;
    opts_.nameFilter = nullptr;
    return;
  }
  strncpy(nameFilterBuf_, name, sizeof(nameFilterBuf_) - 1);
  nameFilterBuf_[sizeof(nameFilterBuf_) - 1] = 0;
  opts_.nameFilter = nameFilterBuf_;
}

bool MomoJoyClass::connectToAddress(const char* address) {
  setAddressFilter(address);
  return rescan();
}

// Blocking discovery scan. Lists every BLE HID gamepad in range without
// connecting to any of them.
uint8_t MomoJoyClass::scanDevices(uint32_t ms) {
  if (!started_) return 0;

  foundCount_ = 0;
  g_discovering = true;

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scanning_ = false;
  scan->setActiveScan(true);      // ask for the scan response -> we get names
  scan->setInterval(45);
  scan->setWindow(15);
  scan->clearResults();

  if (opts_.verbose) {
    Serial.printf("[MomoJoy] scanning %lu ms for gamepads...\n",
                  static_cast<unsigned long>(ms));
  }

  NimBLEScanResults results = scan->getResults(ms, false);

  for (int i = 0; i < results.getCount() && foundCount_ < kMaxFound; ++i) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(i);
    if (dev == nullptr) continue;

    // Same filter the auto-connect path uses: HID service or gamepad appearance.
    const bool     advHid = dev->isAdvertisingService(NimBLEUUID(kSvcHid));
    const uint16_t app    = dev->getAppearance();
    const bool     hidLooking = (app == kAppearanceGamepad) ||
                                ((app & 0xFFC0) == kAppearanceHidMask);
    if (!advHid && !hidLooking) continue;

    MomoFoundDevice& out = found_[foundCount_];
    const std::string name = dev->getName();
    const std::string addr = dev->getAddress().toString();
    strncpy(out.name, name.c_str(), sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = 0;
    strncpy(out.address, addr.c_str(), sizeof(out.address) - 1);
    out.address[sizeof(out.address) - 1] = 0;
    out.rssi       = dev->getRSSI();
    out.appearance = app;
    foundCount_++;
  }

  scan->clearResults();
  g_discovering = false;

  if (opts_.verbose) {
    Serial.printf("[MomoJoy] found %u gamepad(s)\n", foundCount_);
  }
  return foundCount_;
}

}  // namespace massmore

#endif  // ARDUINO_ARCH_ESP32
