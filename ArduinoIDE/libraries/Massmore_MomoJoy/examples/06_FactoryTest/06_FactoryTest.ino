/*
 * Massmore_MomoJoy - 06_FactoryTest
 * ---------------------------------------------------------------------------
 * Guided production self-test: ESP32 / ESP32-S3 board + BLE joystick, nothing
 * else attached. HOME is skipped by default - see TEST_HOME_BUTTON below. The serial monitor walks the operator through one step at a
 * time ("press A", "press D-pad UP", "rotate the left stick"), then prints a
 * full report at the end.
 * ทดสอบแบบไล่ทีละขั้น ต่อแค่บอร์ดกับจอย จอจะบอกให้กดปุ่มอะไรทีละปุ่ม
 * แล้วสรุปผลเป็นรายงานตอนจบ
 *
 * Serial Monitor : 115200 8N1
 * Skip a step    : type  s  + Enter        (ข้ามขั้นตอนนี้)
 * Re-run          : type  r  + Enter        (ทดสอบซ้ำกับจอยตัวเดิม)
 * New unit        : press RESET on the board (จอยตัวใหม่ให้กดรีเซ็ต bond จะถูกล้าง)
 *
 * Machine readable output for the Massmore Web Serial Monitor:
 *   #TEST  <name> START            a step began
 *   #RESULT <KEY>=<value>          one measured result
 *   #VERDICT PASS | #VERDICT FAIL  final decision, always the last line
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

#define FW_NAME    "MOMO Joystick Factory Test"
#define FW_VERSION "2.0.1"
#define SKU        "SKU-1001"

#define PAIR_TIMEOUT_MS  60000UL   // how long to wait for the joystick
#define STEP_TIMEOUT_MS  20000UL   // per step, then the step is marked FAIL
#define MIN_STICK_SPAN     600     // required travel per axis (of 1024)
#define MIN_TRIGGER        800     // required trigger travel (of 1023)
#define MIN_REPORT_RATE      5     // Hz, link considered alive above this

// Many pads use HOME purely as the power / pairing button: a short press is
// handled inside the controller and never reaches the host, so the step can
// only ever time out. It is therefore NOT part of the test by default.
// Set to 1 if your controller really does report HOME as a normal button.
// จอยหลายรุ่นใช้ปุ่ม HOME เป็นปุ่มเปิดปิด/จับคู่ กดสั้นจะไม่ส่งค่าออกมา
// จึงไม่รวมในการทดสอบ ตั้งเป็น 1 ถ้าจอยของคุณส่งค่า HOME ออกมาจริง
#define TEST_HOME_BUTTON     0

// ---- result codes ----------------------------------------------------------
#define R_PASS  1
#define R_FAIL  0
#define R_SKIP (-1)

// ---- the guided sequence ---------------------------------------------------
// kind: 0 = button, 1 = D-pad, 2 = stick, 3 = trigger
struct Step {
  uint8_t     kind;
  uint32_t    mask;        // button bit, or D-pad bit
  const char* key;         // #RESULT key
  const char* prompt;      // what the operator must do
};

static const Step kSteps[] = {
  {0, MOMO_BTN_A,      "BTN_A",      "press  A"},
  {0, MOMO_BTN_B,      "BTN_B",      "press  B"},
  {0, MOMO_BTN_X,      "BTN_X",      "press  X"},
  {0, MOMO_BTN_Y,      "BTN_Y",      "press  Y"},
  {0, MOMO_BTN_L1,     "BTN_L1",     "press  L1  (left shoulder)"},
  {0, MOMO_BTN_R1,     "BTN_R1",     "press  R1  (right shoulder)"},
  {3, 0,               "TRIG_L2",    "pull   L2  fully (left trigger)"},
  {3, 1,               "TRIG_R2",    "pull   R2  fully (right trigger)"},
  {0, MOMO_BTN_L3,     "BTN_L3",     "click  L3  (push the LEFT stick down)"},
  {0, MOMO_BTN_R3,     "BTN_R3",     "click  R3  (push the RIGHT stick down)"},
  {1, MOMO_DPAD_UP,    "DPAD_UP",    "press  D-pad UP"},
  {1, MOMO_DPAD_DOWN,  "DPAD_DOWN",  "press  D-pad DOWN"},
  {1, MOMO_DPAD_LEFT,  "DPAD_LEFT",  "press  D-pad LEFT"},
  {1, MOMO_DPAD_RIGHT, "DPAD_RIGHT", "press  D-pad RIGHT"},
  {2, 0,               "STICK_LEFT", "rotate the LEFT stick fully around the edge"},
  {2, 1,               "STICK_RIGHT","rotate the RIGHT stick fully around the edge"},
  {0, MOMO_BTN_SELECT, "BTN_SELECT", "press  SELECT / View"},
  {0, MOMO_BTN_START,  "BTN_START",  "press  START / Menu"},
#if TEST_HOME_BUTTON
  {0, MOMO_BTN_HOME,   "BTN_HOME",   "press  HOME  (short press)"},
#endif
};
static const uint8_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// ---- measured results ------------------------------------------------------
static int8_t   results[kStepCount];
static int16_t  spanLX = 0, spanLY = 0, spanRX = 0, spanRY = 0;
static uint16_t maxL2 = 0, maxR2 = 0;
static uint16_t rateMax = 0;
static int      rssiBest = -127;
static bool     hidOk = false, paired = false, linkDropped = false;
static bool     restartRequested = false;

void setup();
void loop();
void banner();
bool waitForPairing();
void readHidInfo();
int8_t runStep(uint8_t index);
int8_t stepButton(const Step& s);
int8_t stepDpad(const Step& s);
int8_t stepTrigger(const Step& s);
int8_t stepStick(const Step& s);
char   pollSerial();
bool   linkAlive();
void   trackLink();
void   report();
const char* stateText(int8_t v);

// ---------------------------------------------------------------------------
void banner() {
  Serial.println();
  Serial.println("========================================");
  Serial.printf(" %s\n", FW_NAME);
  Serial.printf(" SKU      : %s\n", SKU);
  Serial.printf(" Firmware : v%s\n", FW_VERSION);
  Serial.printf(" Library  : Massmore_MomoJoy v%s\n", MomoJoy.version());
  Serial.printf(" Chip     : %s rev%d, %d core(s), %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), getCpuFrequencyMhz());
  Serial.printf(" Flash    : %u MB\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf(" Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("========================================");
  Serial.println(" type  s + Enter = skip a step");
  Serial.println(" type  r + Enter = re-run the test");
  Serial.println(" press RESET     = next unit (clears the pairing)");
  Serial.println("========================================");

  Serial.println("#TEST CHIP START");
  Serial.printf("#RESULT CHIP=%s\n", ESP.getChipModel());
  Serial.printf("#RESULT FLASH_MB=%u\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("#RESULT HEAP=%u\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("#RESULT FW=%s\n", FW_VERSION);
}

// Returns 's' to skip, 'r' to restart, 0 for nothing.
char pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 's' || c == 'S') return 's';
    if (c == 'r' || c == 'R') return 'r';
  }
  return 0;
}

bool linkAlive() {
  if (!MomoJoy.isConnected()) { linkDropped = true; return false; }
  return true;
}

void trackLink() {
  if (MomoJoy.reportRateHz() > rateMax) rateMax = MomoJoy.reportRateHz();
  int r = MomoJoy.rssi();
  if (r != 0 && r > rssiBest) rssiBest = r;
}

// ---- step 1: pairing -------------------------------------------------------
bool waitForPairing() {
  Serial.println();
  Serial.println("#TEST PAIR START");
  Serial.printf("STEP 0/%u  pair the joystick\n", kStepCount);
  Serial.println("           slide the mode switch to D, then hold HOME about");
  Serial.println("           3 s until the LED blinks fast.");

  const uint32_t t0 = millis();
  uint32_t lastDot = 0;
  while (millis() - t0 < PAIR_TIMEOUT_MS) {
    MomoJoy.update();
    if (MomoJoy.isConnected()) {
      Serial.println();
      Serial.println("           -> connected");
      Serial.println("#RESULT PAIR=PASS");
      Serial.printf("#RESULT NAME=%s\n", MomoJoy.peerName());
      Serial.printf("#RESULT ADDR=%s\n", MomoJoy.peerAddress());
      Serial.printf("#RESULT RSSI_PAIR=%d\n", MomoJoy.rssi());
      paired = true;
      return true;
    }
    if (pollSerial() == 'r') { restartRequested = true; return false; }
    if (millis() - lastDot > 1000) { lastDot = millis(); Serial.print('.'); }
    delay(5);
  }
  Serial.println();
  Serial.println("#RESULT PAIR=FAIL timeout");
  return false;
}

// ---- step 2: HID descriptor ------------------------------------------------
void readHidInfo() {
  Serial.println("#TEST HID START");
  const size_t fields  = MomoJoy.descriptor().fieldCount();
  const size_t reports = MomoJoy.descriptor().reportCount();
  hidOk = (MomoJoy.reportMapLen() > 0) && (fields > 0);
  Serial.printf("#RESULT HID=%s\n", hidOk ? "PASS" : "FAIL");
  Serial.printf("#RESULT MAP_BYTES=%u\n", (unsigned)MomoJoy.reportMapLen());
  Serial.printf("#RESULT HID_FIELDS=%u\n", (unsigned)fields);
  Serial.printf("#RESULT HID_REPORTS=%u\n", (unsigned)reports);
  Serial.printf("           HID map %u bytes, %u fields, %u report id(s)\n",
                (unsigned)MomoJoy.reportMapLen(), (unsigned)fields, (unsigned)reports);
}

// ---- one button ------------------------------------------------------------
int8_t stepButton(const Step& s) {
  const uint32_t t0 = millis();
  while (millis() - t0 < STEP_TIMEOUT_MS) {
    MomoJoy.update();
    if (!linkAlive()) return R_FAIL;
    trackLink();

    if (MomoJoy.justPressed(s.mask)) return R_PASS;

    // Tell the operator when the wrong button was pressed.
    for (uint8_t b = 0; b < 32; ++b) {
      const uint32_t bit = 1u << b;
      if (bit != s.mask && MomoJoy.justPressed(bit)) {
        Serial.printf("           (that was %s - press the one above)\n",
                      MomoJoyClass::buttonName(bit));
      }
    }

    const char c = pollSerial();
    if (c == 's') return R_SKIP;
    if (c == 'r') { restartRequested = true; return R_SKIP; }
    delay(2);
  }
  return R_FAIL;
}

// ---- one D-pad direction ---------------------------------------------------
int8_t stepDpad(const Step& s) {
  const uint32_t t0 = millis();
  while (millis() - t0 < STEP_TIMEOUT_MS) {
    MomoJoy.update();
    if (!linkAlive()) return R_FAIL;
    trackLink();

    if (MomoJoy.dpad() & s.mask) return R_PASS;

    const char c = pollSerial();
    if (c == 's') return R_SKIP;
    if (c == 'r') { restartRequested = true; return R_SKIP; }
    delay(2);
  }
  return R_FAIL;
}

// ---- one analog trigger (mask 0 = L2, 1 = R2) ------------------------------
int8_t stepTrigger(const Step& s) {
  const bool isLeft = (s.mask == 0);
  uint16_t   peak   = 0;
  const uint32_t t0 = millis();
  uint32_t lastShow = 0;

  while (millis() - t0 < STEP_TIMEOUT_MS) {
    MomoJoy.update();
    if (!linkAlive()) return R_FAIL;
    trackLink();

    const uint16_t v = isLeft ? MomoJoy.l2() : MomoJoy.r2();
    if (v > peak) peak = v;
    if (isLeft) { if (peak > maxL2) maxL2 = peak; }
    else        { if (peak > maxR2) maxR2 = peak; }

    if (millis() - lastShow > 300) {
      lastShow = millis();
      Serial.printf("           %4u / 1023\r", peak);
    }
    if (peak >= MIN_TRIGGER) { Serial.printf("           %4u / 1023\n", peak); return R_PASS; }

    const char c = pollSerial();
    if (c == 's') { Serial.println(); return R_SKIP; }
    if (c == 'r') { restartRequested = true; Serial.println(); return R_SKIP; }
    delay(2);
  }
  Serial.printf("           %4u / 1023  (needs %u)\n", peak, MIN_TRIGGER);
  return R_FAIL;
}

// ---- one stick (mask 0 = left, 1 = right) ----------------------------------
int8_t stepStick(const Step& s) {
  const bool isLeft = (s.mask == 0);
  int16_t xMin = 0, xMax = 0, yMin = 0, yMax = 0;
  const uint32_t t0 = millis();
  uint32_t lastShow = 0;

  while (millis() - t0 < STEP_TIMEOUT_MS) {
    MomoJoy.update();
    if (!linkAlive()) return R_FAIL;
    trackLink();

    const int16_t x = isLeft ? MomoJoy.lx() : MomoJoy.rx();
    const int16_t y = isLeft ? MomoJoy.ly() : MomoJoy.ry();
    if (x < xMin) xMin = x;
    if (x > xMax) xMax = x;
    if (y < yMin) yMin = y;
    if (y > yMax) yMax = y;

    const int spanX = xMax - xMin;
    const int spanY = yMax - yMin;

    if (millis() - lastShow > 300) {
      lastShow = millis();
      Serial.printf("           X span %4d   Y span %4d   (need %d)\r",
                    spanX, spanY, MIN_STICK_SPAN);
    }

    if (spanX >= MIN_STICK_SPAN && spanY >= MIN_STICK_SPAN) {
      Serial.printf("           X span %4d   Y span %4d\n", spanX, spanY);
      if (isLeft) { spanLX = spanX; spanLY = spanY; }
      else        { spanRX = spanX; spanRY = spanY; }
      return R_PASS;
    }

    const char c = pollSerial();
    if (c == 's' || c == 'r') {
      if (c == 'r') restartRequested = true;
      Serial.println();
      if (isLeft) { spanLX = spanX; spanLY = spanY; }
      else        { spanRX = spanX; spanRY = spanY; }
      return R_SKIP;
    }
    delay(2);
  }
  const int spanX = xMax - xMin, spanY = yMax - yMin;
  Serial.printf("           X span %4d   Y span %4d  (needs %d)\n",
                spanX, spanY, MIN_STICK_SPAN);
  if (isLeft) { spanLX = spanX; spanLY = spanY; }
  else        { spanRX = spanX; spanRY = spanY; }
  return R_FAIL;
}

int8_t runStep(uint8_t index) {
  const Step& s = kSteps[index];
  Serial.printf("STEP %u/%u  %s\n", index + 1, kStepCount, s.prompt);

  int8_t r;
  switch (s.kind) {
    case 0:  r = stepButton(s);  break;
    case 1:  r = stepDpad(s);    break;
    case 2:  r = stepStick(s);   break;
    default: r = stepTrigger(s); break;
  }

  Serial.printf("           -> %s\n", stateText(r));
  Serial.printf("#RESULT %s=%s\n", s.key, stateText(r));

  // Wait for the button to be released so the next step does not see it.
  if (s.kind == 0 || s.kind == 1) {
    const uint32_t t0 = millis();
    while (millis() - t0 < 800) { MomoJoy.update(); delay(5); }
  }
  return r;
}

const char* stateText(int8_t v) {
  return v == R_PASS ? "PASS" : (v == R_SKIP ? "SKIP" : "FAIL");
}

// ---- final report ----------------------------------------------------------
void report() {
  uint8_t nPass = 0, nFail = 0, nSkip = 0;
  for (uint8_t i = 0; i < kStepCount; ++i) {
    if (results[i] == R_PASS) nPass++;
    else if (results[i] == R_SKIP) nSkip++;
    else nFail++;
  }

  const bool linkOk = (rateMax >= MIN_REPORT_RATE) && !linkDropped;

  Serial.println();
  Serial.println("========================================");
  Serial.println("  TEST REPORT");
  Serial.println("========================================");
  Serial.printf("  controller : %s [%s]\n", MomoJoy.peerName(), MomoJoy.peerAddress());
  Serial.printf("  battery    : %u %%\n", MomoJoy.battery());
  Serial.printf("  rssi best  : %d dBm\n", rssiBest);
  Serial.printf("  rate max   : %u Hz\n", rateMax);
  Serial.printf("  packets    : %lu\n", (unsigned long)MomoJoy.packets());
  Serial.printf("  link drop  : %s\n", linkDropped ? "YES" : "no");
  Serial.println("  --------------------------------------");
  for (uint8_t i = 0; i < kStepCount; ++i) {
    Serial.printf("  %-12s %s\n", kSteps[i].key, stateText(results[i]));
  }
  Serial.println("  --------------------------------------");
  Serial.printf("  stick left  X:%d  Y:%d   (need %d)\n", spanLX, spanLY, MIN_STICK_SPAN);
  Serial.printf("  stick right X:%d  Y:%d\n", spanRX, spanRY);
  Serial.printf("  trigger L2:%u  R2:%u      (need %d)\n", maxL2, maxR2, MIN_TRIGGER);
  Serial.println("  --------------------------------------");
  Serial.printf("  pass %u   fail %u   skip %u   of %u\n", nPass, nFail, nSkip, kStepCount);
  Serial.println("========================================");

  Serial.printf("#RESULT STEPS_PASS=%u\n", nPass);
  Serial.printf("#RESULT STEPS_FAIL=%u\n", nFail);
  Serial.printf("#RESULT STEPS_SKIP=%u\n", nSkip);
  Serial.printf("#RESULT STICK_SPAN=LX:%d,LY:%d,RX:%d,RY:%d\n", spanLX, spanLY, spanRX, spanRY);
  Serial.printf("#RESULT TRIGGER_MAX=L2:%u,R2:%u\n", maxL2, maxR2);
  Serial.printf("#RESULT RATE_HZ=%u\n", rateMax);
  Serial.printf("#RESULT RSSI=%d\n", rssiBest);
  Serial.printf("#RESULT BATTERY=%u\n", MomoJoy.battery());
  Serial.printf("#RESULT PACKETS=%lu\n", (unsigned long)MomoJoy.packets());
  Serial.printf("#RESULT LINK_DROP=%d\n", linkDropped ? 1 : 0);
  Serial.printf("#RESULT LINK=%s\n", linkOk ? "PASS" : "FAIL");

  const bool pass = paired && hidOk && linkOk && (nFail == 0);
  Serial.printf("#VERDICT %s\n", pass ? "PASS" : "FAIL");
  Serial.println("========================================");
  Serial.println("Type r + Enter to re-run these steps on the SAME joystick.");
  Serial.println("Press RESET before testing a DIFFERENT joystick, so the");
  Serial.println("pairing starts from a clean state.");
}

// ---------------------------------------------------------------------------
void setup() {
  parkMotorPins();   // keep every motor output at 0 V
  Serial.begin(115200);
  delay(400);
  banner();

  Serial.println("#TEST BLE_INIT START");
  MomoJoyOptions opt;
  opt.localName        = "MOMO-FACTORY";
  opt.deadzone         = 16;     // small: we want to measure the real travel
  opt.autoReconnect    = true;
  opt.clearBondsOnBoot = true;   // every unit starts from a clean pairing
  opt.verbose          = true;

  const bool ok = MomoJoy.begin(opt);
  Serial.printf("#RESULT BLE_INIT=%s\n", ok ? "PASS" : "FAIL");
  if (!ok) {
    Serial.println("#VERDICT FAIL");
    while (true) delay(1000);
  }
}

void loop() {
  // ---- one full test run --------------------------------------------------
  restartRequested = false;
  linkDropped = false;
  paired = false;
  hidOk = false;
  rateMax = 0;
  rssiBest = -127;
  maxL2 = maxR2 = 0;
  spanLX = spanLY = spanRX = spanRY = 0;
  for (uint8_t i = 0; i < kStepCount; ++i) results[i] = R_FAIL;

  if (!waitForPairing()) {
    if (!restartRequested) {
      Serial.println("#VERDICT FAIL");
      Serial.println("Type r + Enter to try again.");
      while (pollSerial() != 'r') { MomoJoy.update(); delay(10); }
    }
    return;   // loop() runs again = a fresh test run
  }

  readHidInfo();
  Serial.println("#TEST INPUT START");
  Serial.println("----------------------------------------");

  for (uint8_t i = 0; i < kStepCount && !restartRequested; ++i) {
    results[i] = runStep(i);
    if (!MomoJoy.isConnected()) {
      Serial.println("!! joystick disconnected - the remaining steps fail");
      linkDropped = true;
      break;
    }
  }

  if (restartRequested) {
    Serial.println(">>> restarting the test");
    return;
  }

  report();

  // Idle until the operator asks for another run (or resets the board).
  while (pollSerial() != 'r') { MomoJoy.update(); delay(20); }
  Serial.println(">>> new run");
}
