/*
 * Massmore_MomoJoy - 03_PairByAddress
 * ---------------------------------------------------------------------------
 * Several joysticks switched on in the same room? Lock the board onto exactly
 * one of them by its MAC address, so it connects on its own at power-up with
 * nobody pressing anything.
 * มีจอยเปิดอยู่หลายตัวพร้อมกัน? ล็อกบอร์ดไว้กับจอยตัวเดียวด้วย MAC address
 * เปิดเครื่องปุ๊บต่อเองเลย ไม่ต้องกดปุ่มอะไร
 *
 * WHY / ทำไมต้องล็อก
 *   By default the library connects to the FIRST BLE HID gamepad it sees. With
 *   two pads awake that is a coin toss, and on a production line or in a
 *   classroom every board would fight over the same controller.
 *   ปกติไลบรารีจะต่อกับจอย "ตัวแรกที่เจอ" ถ้ามีหลายตัวจะสุ่มเอา ในสายการผลิตหรือห้องเรียน
 *   บอร์ดทุกตัวจะแย่งจอยตัวเดียวกัน จึงต้องล็อกด้วย MAC
 *
 * HOW TO USE / วิธีใช้ - สองรอบ
 *   1) Leave TARGET_ADDRESS empty, upload, open the monitor at 115200.
 *      The board lists every gamepad in range with its MAC and signal.
 *      รอบแรก ปล่อย TARGET_ADDRESS ว่างไว้ บอร์ดจะพิมพ์รายชื่อจอยทั้งหมดพร้อม MAC
 *
 *   2) Copy the MAC of the pad you want into TARGET_ADDRESS, upload again.
 *      From now on this board only ever talks to that one controller.
 *      รอบสอง ก็อป MAC ของตัวที่ต้องการมาใส่ แล้วอัพใหม่ จบ - ต่อเองทุกครั้งที่เปิดเครื่อง
 *
 * The MAC is printed by every example on connect, e.g.
 *   >>> connected: "GamepadSpace-Q34U" [03:52:01:09:61:b5]
 */
#include <Arduino.h>
#include <Massmore_MomoJoy.h>

using namespace massmore;

// ===========================================================================
//  SET THIS / ตั้งค่าตรงนี้
// ===========================================================================
// The controller this board belongs to. Empty = scan and list only.
// MAC ของจอยที่จะจับคู่ ปล่อยว่าง = สแกนดูรายชื่ออย่างเดียว
#define TARGET_ADDRESS ""        // e.g. "03:52:01:09:61:b5"

// No MAC yet and you only have one pad on the bench? Set this to 1 and the
// board takes the strongest signal automatically - still no button pressing.
// ถ้ามีจอยตัวเดียวและขี้เกียจใส่ MAC ตั้งเป็น 1 บอร์ดจะเลือกตัวที่สัญญาณแรงสุดให้เอง
#define AUTO_PICK_STRONGEST 0

#define SCAN_MS 6000             // how long each discovery scan runs

// ---------------------------------------------------------------------------
//  Robot board safety / ความปลอดภัยของบอร์ดหุ่นยนต์
//  The robot boards carry motor drivers on fixed pins. Left floating, a driver
//  input can latch high and a wheel spins for the whole test, so every example
//  parks them LOW before doing anything else. This example does not use the
//  motors at all - it only makes sure they stay still.
//  บอร์ดหุ่นยนต์มีไดรเวอร์มอเตอร์ต่ออยู่กับขาที่กำหนดไว้ ถ้าปล่อยขาลอย มอเตอร์อาจหมุนค้าง
//  ตลอดการทดสอบ ทุกตัวอย่างจึงปักขาเป็น LOW ก่อนเสมอ ตัวอย่างนี้ไม่ได้ใช้มอเตอร์
//  Set MOMO_BOARD to 0 on a plain dev board with nothing attached.
// ---------------------------------------------------------------------------
#define MOMO_BOARD 1

void parkMotorPins();

#if MOMO_BOARD
void parkMotorPins() {
#if CONFIG_IDF_TARGET_ESP32S3
  // MOMO ESP32-S3 board: 4 motor drivers (IN1/IN2) + buzzer
  static const uint8_t kPins[] = {21, 38, 39, 40, 41, 42, 47, 48, 45};
#else
  // ESP32 robot board: 2 motor drivers (IN1/IN2) + 3 servo outputs
  static const uint8_t kPins[] = {27, 26, 16, 17, 19, 32, 33};
#endif
  for (uint8_t i = 0; i < sizeof(kPins); ++i) {
    pinMode(kPins[i], OUTPUT);
    digitalWrite(kPins[i], LOW);
  }
}
#else
void parkMotorPins() {}         // nothing attached, nothing to park
#endif

void setup();
void loop();
void listGamepads();
void onConnected(const char* name, const char* address);
void onDisconnected();

// ---------------------------------------------------------------------------
void onConnected(const char* name, const char* address) {
  Serial.println();
  Serial.println("=========================================");
  Serial.printf(" LOCKED ON\n");
  Serial.printf("   name    : %s\n", name);
  Serial.printf("   address : %s\n", address);
  Serial.printf("   rssi    : %d dBm\n", MomoJoy.rssi());
  Serial.printf("   battery : %u %%\n", MomoJoy.battery());
  Serial.println("=========================================");
  Serial.println("Press any button - only THIS controller can drive this board.");
}

void onDisconnected() {
  Serial.println(">>> disconnected - waiting for the same controller again");
}

// Scan and print every gamepad in range, strongest first is not guaranteed -
// the RSSI column tells you which one is closest.
// สแกนแล้วพิมพ์จอยทุกตัวที่เจอ ดูคอลัมน์ RSSI ว่าตัวไหนอยู่ใกล้ที่สุด
void listGamepads() {
  const uint8_t n = MomoJoy.scanDevices(SCAN_MS);

  Serial.println();
  Serial.println("=========================================");
  if (n == 0) {
    Serial.println(" no BLE gamepad found");
    Serial.println(" - switch the pad on, mode switch to D");
    Serial.println(" - hold HOME ~3 s until the LED blinks fast");
    Serial.println("=========================================");
    return;
  }

  Serial.printf(" %u gamepad(s) in range\n", n);
  Serial.println(" #  rssi   address             name");
  Serial.println(" -----------------------------------------");
  for (uint8_t i = 0; i < n; ++i) {
    const MomoFoundDevice& d = MomoJoy.foundDevice(i);
    Serial.printf(" %u  %4d   %-18s  %s\n",
                  i + 1, d.rssi, d.address,
                  d.name[0] ? d.name : "(no name)");
  }
  Serial.println("=========================================");
  // Serial output is kept ASCII-only so it renders in any monitor.
  // ข้อความที่พิมพ์ออก Serial เป็น ASCII ล้วน จะได้ไม่เพี้ยนในมอนิเตอร์ทุกตัว
  // (แปล: ก็อป address ที่ต้องการไปใส่ใน TARGET_ADDRESS ด้านบน แล้วอัพโหลดใหม่)
  Serial.println(" Copy the address you want into TARGET_ADDRESS");
  Serial.println(" at the top of this sketch, then upload again.");
  Serial.println("=========================================");

#if AUTO_PICK_STRONGEST
  uint8_t best = 0;
  for (uint8_t i = 1; i < n; ++i) {
    if (MomoJoy.foundDevice(i).rssi > MomoJoy.foundDevice(best).rssi) best = i;
  }
  const MomoFoundDevice& pick = MomoJoy.foundDevice(best);
  Serial.printf(">>> auto-picking the strongest: %s [%s] %d dBm\n",
                pick.name[0] ? pick.name : "(no name)", pick.address, pick.rssi);
  MomoJoy.connectToAddress(pick.address);
#endif
}

// ---------------------------------------------------------------------------
void setup() {
  parkMotorPins();   // keep every motor output at 0 V
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Massmore MomoJoy - 03 Pair By Address");
  Serial.printf("library v%s\n", MomoJoy.version());

  MomoJoyOptions opt;
  opt.localName     = "MOMO";
  opt.autoReconnect = true;
  opt.verbose       = true;

  // An empty string means "no filter", so guard it.
  const char* target = TARGET_ADDRESS;
  if (target[0] != 0) {
    opt.addressFilter = target;   // <- only this controller, nothing else
    opt.autoStart     = true;     // start looking for it immediately
    Serial.printf("locked to %s - connecting on its own, no button needed\n", target);
  } else {
    opt.autoStart = false;        // do not grab the first pad we see
    Serial.println("no TARGET_ADDRESS set - scanning to list what is around");
  }

  if (!MomoJoy.begin(opt)) {
    Serial.println("[ERROR] BLE init failed");
    return;
  }

  MomoJoy.onConnect(onConnected);
  MomoJoy.onDisconnect(onDisconnected);

  if (target[0] == 0) listGamepads();
}

void loop() {
  MomoJoy.update();

  const char* target = TARGET_ADDRESS;

  // ---- no MAC configured: keep listing what is in range -------------------
  if (target[0] == 0 && !MomoJoy.isConnected()) {
    static uint32_t lastScan = 0;
    if (millis() - lastScan > 10000) {
      lastScan = millis();
      listGamepads();
    }
    return;
  }

  if (!MomoJoy.isConnected()) return;

  // ---- connected: prove it is the right one -------------------------------
  for (uint8_t b = 0; b < 32; ++b) {
    const uint32_t bit = 1u << b;
    if (MomoJoy.justPressed(bit)) {
      Serial.printf("button: %-8s  from %s\n",
                    MomoJoyClass::buttonName(bit), MomoJoy.peerAddress());
    }
  }

  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.printf("linked to %s  %u Hz  rssi %d dBm  battery %u%%\n",
                  MomoJoy.peerAddress(), MomoJoy.reportRateHz(),
                  MomoJoy.rssi(), MomoJoy.battery());
  }
}
