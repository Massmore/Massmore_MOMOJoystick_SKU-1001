/*
 * Massmore_MomoJoy - 01_Pairing
 * ---------------------------------------------------------------------------
 * Start here. Pairs the board with the joystick and shows the link status:
 * name, MAC address, signal strength, battery and report rate.
 * เริ่มที่ตัวอย่างนี้ก่อน: จับคู่จอยกับบอร์ด แล้วดูสถานะการเชื่อมต่อ
 *
 * How to pair / วิธีจับคู่
 *   1) slide the joystick mode switch to "D"        (เลื่อนสวิตช์ไปตำแหน่ง D)
 *   2) hold HOME ~3 s until the LED blinks fast     (กด HOME ค้างจนไฟกะพริบถี่)
 *   3) reset the board - it connects and bonds by itself
 * The bond is stored in NVS, so next time it reconnects on its own.
 * bond ถูกเก็บใน NVS ครั้งต่อไปจะต่อเองอัตโนมัติ
 *
 * Press SELECT + START together for 1.5 s to erase all pairings and start over.
 * กด SELECT + START ค้างพร้อมกัน 1.5 วินาที เพื่อล้าง bond ทั้งหมด
 *
 * Serial Monitor: 115200 8N1
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

void setup();
void loop();
void onConnected(const char* name, const char* address);
void onDisconnected();

void onConnected(const char* name, const char* address) {
  Serial.println();
  Serial.println("=========================================");
  Serial.printf(" CONNECTED\n");
  Serial.printf("   name    : %s\n", name);
  Serial.printf("   address : %s\n", address);
  Serial.printf("   rssi    : %d dBm\n", MomoJoy.rssi());
  Serial.printf("   battery : %u %%\n", MomoJoy.battery());
  Serial.println("=========================================");
  Serial.println("Press any button to confirm it works.");
}

void onDisconnected() {
  Serial.println(">>> disconnected - scanning again...");
}

void setup() {
  parkMotorPins();   // keep every motor output at 0 V
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Massmore MomoJoy - 01 Pairing");
  Serial.printf("library v%s\n", MomoJoy.version());
  Serial.println("Put the joystick in pairing mode: switch to D, hold HOME 3 s.");

  MomoJoyOptions opt;
  opt.localName        = "MOMO";
  opt.autoReconnect    = true;
  opt.clearBondsOnBoot = false;   // true = forget every pairing on each boot
  opt.verbose          = true;    // print the scan / connect steps

  MomoJoy.onConnect(onConnected);
  MomoJoy.onDisconnect(onDisconnected);

  if (!MomoJoy.begin(opt)) Serial.println("[ERROR] BLE init failed");
}

void loop() {
  MomoJoy.update();

  if (!MomoJoy.isConnected()) return;

  // Any button press confirms the link is really working.
  if (MomoJoy.buttons() && (MomoJoy.buttons() != 0)) {
    static uint32_t lastBtn = 0;
    if (millis() - lastBtn > 300) {
      lastBtn = millis();
      for (uint8_t b = 0; b < 32; ++b) {
        uint32_t bit = 1u << b;
        if (MomoJoy.justPressed(bit)) {
          Serial.printf("button: %s\n", MomoJoyClass::buttonName(bit));
        }
      }
    }
  }

  // SELECT + START held 1.5 s -> erase every stored pairing.
  if (MomoJoy.pressed(MOMO_BTN_SELECT) && MomoJoy.longPressed(MOMO_BTN_START, 1500)) {
    Serial.println(">>> erasing all bonds. Re-pair the joystick now.");
    MomoJoy.forgetBonds();
    MomoJoy.disconnect();
    delay(1000);
  }

  // Link health once per second.
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("link: %u Hz  rssi %d dBm  battery %u%%  packets %lu\n",
                  MomoJoy.reportRateHz(), MomoJoy.rssi(), MomoJoy.battery(),
                  (unsigned long)MomoJoy.packets());
  }
}
