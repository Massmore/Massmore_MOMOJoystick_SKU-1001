/*
 * Massmore_MomoJoy - 02_ReadAllSerial
 * ---------------------------------------------------------------------------
 * Prints everything the joystick sends: sticks, analog triggers, D-pad,
 * buttons and battery - only when a value actually changes, so the console
 * stays readable.
 * พิมพ์ค่าทุกอย่างที่จอยส่งมา เฉพาะตอนที่ค่าเปลี่ยน
 *
 * Also the calibration tool: set SHOW_RAW_REPORTS to 1 to see the raw HID
 * bytes and the parsed descriptor. That tells you which HID button number
 * belongs to which physical button on an unknown controller.
 * ตั้ง SHOW_RAW_REPORTS = 1 เพื่อดู byte ดิบและตาราง descriptor สำหรับคาลิเบรต
 *
 * Output formats you can switch on:
 *   printStateChanges() human readable   (default)
 *   printCsv()          lx,ly,rx,ry,l2,r2,dpad,buttons,battery
 *   printJson()         one JSON line, for the Massmore Web Serial Monitor
 */
#include <Arduino.h>
#include <Massmore_MomoJoy.h>

using namespace massmore;

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

#define SHOW_RAW_REPORTS 0     // 1 = also dump raw HID bytes (calibration)
#define OUTPUT_FORMAT    0     // 0 = human, 1 = CSV, 2 = JSON

void setup();
void loop();
void onConnected(const char* name, const char* address);
void onDisconnected();
void onRaw(uint8_t reportId, const uint8_t* data, size_t len);

void onConnected(const char* name, const char* address) {
  Serial.printf("\n>>> connected: \"%s\" [%s]\n", name, address);
  MomoJoy.dumpDescriptorFields(Serial);      // what this pad really reports
#if SHOW_RAW_REPORTS
  MomoJoy.dumpReportMap(Serial);             // the raw HID report map bytes
  Serial.println("Press ONE button at a time and watch which bit changes.");
#endif
}

void onDisconnected() {
  Serial.println(">>> disconnected, scanning again...");
}

void onRaw(uint8_t reportId, const uint8_t* data, size_t len) {
  Serial.printf("RPT id=%u len=%u :", reportId, (unsigned)len);
  for (size_t i = 0; i < len; ++i) Serial.printf(" %02X", data[i]);
  Serial.println();
}

void setup() {
  parkMotorPins();   // keep every motor output at 0 V
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=============================================");
  Serial.println(" Massmore MomoJoy - 02 Read All");
  Serial.printf(" library v%s\n", MomoJoy.version());
  Serial.println("=============================================");

  MomoJoyOptions opt;
  opt.deadzone      = 24;        // ignore stick drift around the centre
  opt.autoReconnect = true;
  opt.verbose       = true;

  MomoJoy.onConnect(onConnected);
  MomoJoy.onDisconnect(onDisconnected);
#if SHOW_RAW_REPORTS
  MomoJoy.onRawReport(onRaw);
#endif

  if (!MomoJoy.begin(opt)) Serial.println("[ERROR] BLE init failed");
}

void loop() {
  MomoJoy.update();
  if (!MomoJoy.isConnected()) return;

#if OUTPUT_FORMAT == 1
  static uint32_t last = 0;
  if (millis() - last >= 50) { last = millis(); MomoJoy.printCsv(Serial); }
#elif OUTPUT_FORMAT == 2
  static uint32_t last = 0;
  if (millis() - last >= 50) { last = millis(); MomoJoy.printJson(Serial); }
#else
  MomoJoy.printStateChanges(Serial);
#endif
}
