/*
 * Massmore_MomoJoy - 05_Robot4WD_ESP32S3
 * ---------------------------------------------------------------------------
 * Four wheel omnidirectional robot on the MOMO ESP32-S3 board.
 * หุ่นยนต์ 4 ล้อ วิ่งได้รอบทิศ บนบอร์ด MOMO ESP32-S3
 *
 * Hardware on the board / อุปกรณ์บนบอร์ด
 *   4 x motor driver, two logic pins each (IN1 / IN2), sign-magnitude PWM
 *   4 x quadrature encoder
 *   1 x active buzzer
 *
 *   M1 = rear-right   IN1 21  IN2 38   ENC A 1   B 2
 *   M2 = front-right  IN1 39  IN2 40   ENC A 16  B 17
 *   M3 = front-left   IN1 41  IN2 42   ENC A 7   B 8
 *   M4 = rear-left    IN1 47  IN2 48   ENC A 9   B 10
 *   Buzzer 45
 *
 * Controls / การบังคับ  -  one stick does all the movement
 *   LEFT stick        push it where you want to go: forward, back, sideways
 *                     or any diagonal. Speed follows how far you push, 0-100%.
 *                     ก้านซ้ายก้านเดียว ดันไปทางไหนรถไปทางนั้น เดินหน้า สไลด์ข้าง เฉียง
 *   D-pad             drive straight along that arrow, fixed speed
 *                     ปุ่มลูกศร วิ่งตรงตามทิศนั้นทันที
 *   RIGHT stick X     spin the robot on the spot   (หมุนตัว)
 *   R3 hold           horn                          (แตร)
 *   Y hold            turbo, unlocks full speed
 *   A                 cruise control: hold the current speed hands-free
 *   X                 auto-drive 1 m forward using the encoders
 *   B                 emergency stop, cancels cruise and auto-drive
 *   L1 L2 R1 R2       bench test: spin one wheel while held
 *   SELECT / START    print / reset the encoder counts
 *   HOME hold 2 s     erase the BLE pairing
 *
 * SAFETY / ความปลอดภัย
 *   - every motor pin is parked LOW before anything else runs, so a floating
 *     driver input can never spin a wheel at boot
 *     ปักขามอเตอร์เป็น LOW ก่อนเสมอ ไม่ให้ขาลอยจนมอเตอร์หมุนเองตอนบูต
 *   - motors stop the moment the BLE link drops or the joystick goes quiet
 *   - speed ramps up slowly and drops quickly, so the robot never jerks off
 *     the line but still stops the instant you let go
 *     เร่งค่อย ๆ แต่ผ่อนแล้วหยุดทันที
 *
 * TOO FAST OR TOO SLOW? / แรงไปหรือช้าไป?
 *   SPEED_NORMAL   top speed of the stick, 110 of 255 by default
 *   SLEW_UP        how quickly it picks up speed, lower = gentler
 *   EXPO_AMOUNT    how soft the middle of the stick feels, higher = softer
 *   MOTOR_MIN_DUTY raise it if the robot buzzes but does not creep
 *   Y button       unlocks SPEED_TURBO while held
 */
#include <Arduino.h>
#include <Massmore_MomoJoy.h>

using namespace massmore;

// ===========================================================================
//  1. Board pins / ขาต่าง ๆ บนบอร์ด
// ===========================================================================
#define M1_IN1 21   // rear-right
#define M1_IN2 38
#define M2_IN1 39   // front-right
#define M2_IN2 40
#define M3_IN1 41   // front-left
#define M3_IN2 42
#define M4_IN1 47   // rear-left
#define M4_IN2 48
#define BUZZER_PIN 45

#define M1_ENCA 1
#define M1_ENCB 2
#define M2_ENCA 16
#define M2_ENCB 17
#define M3_ENCA 7
#define M3_ENCB 8
#define M4_ENCA 9
#define M4_ENCB 10

// Motor index: 0 = RR, 1 = FR, 2 = FL, 3 = RL
#define MOT_RR 0
#define MOT_FR 1
#define MOT_FL 2
#define MOT_RL 3

// ===========================================================================
//  2. Tuning / ค่าปรับแต่ง
// ===========================================================================
// Mecanum wheels can move sideways. Set this to 0 if the robot has ordinary
// wheels - the left stick then only drives forward / back.
// ล้อ mecanum สไลด์ข้างได้ ถ้าใช้ล้อธรรมดาให้ตั้งเป็น 0
#define MECANUM_WHEELS   1

#define PWM_FREQ_HZ  20000        // 20 kHz, above hearing - no motor whine
#define PWM_RES_BIT      8        // duty 0..255
#define PWM_MAX        255

// --- speed --------------------------------------------------------------
// Deliberately gentle. Geared motors are strong: full PWM launches the robot
// across the room. Raise SPEED_NORMAL once you know how it behaves.
// ตั้งไว้ให้นุ่ม ๆ ก่อน มอเตอร์เกียร์แรงมาก ถ้าอยากเร็วขึ้นค่อยเพิ่ม SPEED_NORMAL
#define SPEED_NORMAL   110        // default top speed  (of 255)
#define SPEED_TURBO    220        // while Y is held
#define SPEED_DPAD     110        // fixed speed in D-pad mode
#define SPEED_TEST     150        // single-wheel bench test

// --- feel ----------------------------------------------------------------
// Below MOTOR_MIN_DUTY a geared motor only buzzes and does not turn, so any
// non-zero command is lifted to at least this much. Set 0 to disable.
// ต่ำกว่าค่านี้มอเตอร์จะแค่สั่นแต่ไม่หมุน จึงยกค่าคำสั่งขึ้นมาให้ถึงขั้นต่ำ
#define MOTOR_MIN_DUTY  40
// Expo curve: 0 = linear, higher = softer around the centre of the stick.
// ยิ่งมากยิ่งนุ่มช่วงกลางก้าน บังคับละเอียดง่ายขึ้น
#define EXPO_AMOUNT   0.60f
// Rotation is the twitchiest axis, so give it less gain than translation.
// การหมุนตัวไวที่สุด จึงหรี่ลงกว่าการเคลื่อนที่
#define ROT_GAIN      0.60f
// Ramp: accelerate slowly, slow down quickly. PWM change per 20 ms tick.
// SLEW_UP 6 -> about 0.8 s from standstill to full speed.
// เร่งช้า ๆ แต่ผ่อนเร็ว ปลอดภัยกว่า
#define SLEW_UP          6
#define SLEW_DOWN       24

#define STICK_DEADZONE  40        // library dead zone, output units
#define CONTROL_TICK_MS 20        // control loop period -> 50 Hz
#define LINK_QUIET_MS  400        // no report for this long = treat as lost

#define PULSES_PER_METER 660.0f   // measured on the robot
#define AUTO_DRIVE_PULSES 615     // X button target, about 1 m
#define AUTO_SLOW_PULSES  200     // start slowing down this far from target
#define AUTO_SPEED_FAST   150
#define AUTO_SPEED_SLOW    80
#define AUTO_TIMEOUT_MS  8000UL

// ===========================================================================
//  3. State / ตัวแปรสถานะ
// ===========================================================================
struct MotorPins { uint8_t in1; uint8_t in2; const char* name; };
static const MotorPins kMotor[4] = {
    {M1_IN1, M1_IN2, "M1 rear-right"},
    {M2_IN1, M2_IN2, "M2 front-right"},
    {M3_IN1, M3_IN2, "M3 front-left"},
    {M4_IN1, M4_IN2, "M4 rear-left"},
};

volatile long encCount[4] = {0, 0, 0, 0};

static bool cruiseControl = false;
static bool autoDriving   = false;
static long autoTarget    = 0;
static uint32_t autoStart = 0;

// Ramped output actually sent to the motors.
static int outFL = 0, outFR = 0, outRL = 0, outRR = 0;

void setup();
void loop();
void parkMotorPins();
void motorsBegin();
void setMotor(uint8_t index, int speed);
void motorsStop();
void driveMix(int fwd, int strafe, int rot);
int   slew(int current, int target);
float expoCurve(float m);
float magToSpeed(float mag, int maxSpeed);
long encAverage();
void printEncoders();
void onConnected(const char* name, const char* address);
void onDisconnected();
void onButtons(uint32_t pressed, uint32_t released);

// ===========================================================================
//  4. Encoders / อ่านค่า encoder
// ===========================================================================
void IRAM_ATTR isrM1() { if (digitalRead(M1_ENCB)) encCount[MOT_RR]--; else encCount[MOT_RR]++; }
void IRAM_ATTR isrM2() { if (digitalRead(M2_ENCB)) encCount[MOT_FR]++; else encCount[MOT_FR]--; }
void IRAM_ATTR isrM3() { if (digitalRead(M3_ENCB)) encCount[MOT_FL]--; else encCount[MOT_FL]++; }
void IRAM_ATTR isrM4() { if (digitalRead(M4_ENCB)) encCount[MOT_RL]++; else encCount[MOT_RL]--; }

// Average of all four wheels: far more stable than trusting a single encoder.
// เฉลี่ยทั้ง 4 ล้อ แม่นกว่าการอ่านล้อเดียว
long encAverage() {
  noInterrupts();
  const long a = encCount[0], b = encCount[1], c = encCount[2], d = encCount[3];
  interrupts();
  return (a + b + c + d) / 4;
}

void printEncoders() {
  noInterrupts();
  const long a = encCount[0], b = encCount[1], c = encCount[2], d = encCount[3];
  interrupts();
  Serial.printf("Encoder  RR:%ld  FR:%ld  FL:%ld  RL:%ld\n", a, b, c, d);
  Serial.printf("Distance %.2f m  (%.0f pulses/m)\n",
                ((a + b + c + d) / 4.0f) / PULSES_PER_METER, PULSES_PER_METER);
}

// ===========================================================================
//  5. Motors / ควบคุมมอเตอร์
// ===========================================================================

// Called FIRST in setup(), before anything can drive a wheel.
// เรียกเป็นอย่างแรกใน setup() ก่อนที่อะไรจะสั่งล้อหมุนได้
void parkMotorPins() {
  static const uint8_t kAll[] = {M1_IN1, M1_IN2, M2_IN1, M2_IN2,
                                 M3_IN1, M3_IN2, M4_IN1, M4_IN2};
  for (uint8_t i = 0; i < sizeof(kAll); ++i) {
    pinMode(kAll[i], OUTPUT);
    digitalWrite(kAll[i], LOW);
  }
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void motorsBegin() {
  for (uint8_t i = 0; i < 4; ++i) {
    // Both pins are PWM capable: that is what makes sign-magnitude possible.
    ledcAttach(kMotor[i].in1, PWM_FREQ_HZ, PWM_RES_BIT);
    ledcAttach(kMotor[i].in2, PWM_FREQ_HZ, PWM_RES_BIT);
    ledcWrite(kMotor[i].in1, 0);
    ledcWrite(kMotor[i].in2, 0);
  }
}

// speed -255..255 ; 0 = coast, negative = reverse
void setMotor(uint8_t index, int speed) {
  if (index > 3) return;
  speed = constrain(speed, -PWM_MAX, PWM_MAX);
  if (speed >= 0) {
    ledcWrite(kMotor[index].in2, 0);
    ledcWrite(kMotor[index].in1, speed);
  } else {
    ledcWrite(kMotor[index].in1, 0);
    ledcWrite(kMotor[index].in2, -speed);
  }
}

void motorsStop() {
  outFL = outFR = outRL = outRR = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    ledcWrite(kMotor[i].in1, 0);
    ledcWrite(kMotor[i].in2, 0);
  }
}

// Ramp toward the target. Speeding up is deliberately slow so the robot
// creeps away smoothly; slowing down is fast, because stopping should feel
// immediate. เร่งช้า ๆ แต่ผ่อนและหยุดให้เร็ว
int slew(int current, int target) {
  const int step = (abs(target) > abs(current)) ? SLEW_UP : SLEW_DOWN;
  if (target > current) return min(current + step, target);
  if (target < current) return max(current - step, target);
  return current;
}

// Expo curve on a 0..1 magnitude. Small stick movement -> small speed.
// เส้นโค้ง expo ขยับก้านนิดเดียวรถค่อย ๆ ไป ดันสุดถึงได้ความเร็วเต็ม
float expoCurve(float m) {
  if (m < 0.0f) m = 0.0f;
  if (m > 1.0f) m = 1.0f;
  return EXPO_AMOUNT * m * m * m + (1.0f - EXPO_AMOUNT) * m;
}

// Turn a 0..1 stick magnitude into a motor speed. The whole usable range sits
// between MOTOR_MIN_DUTY (the slowest the motor will actually creep) and
// maxSpeed, so every millimetre of stick travel does something - no dead band
// at the bottom, no sudden jump.
// แปลงระยะดันก้าน 0..1 เป็นความเร็ว โดยไล่ต่อเนื่องจาก MOTOR_MIN_DUTY ถึง maxSpeed
// ทุกมิลลิเมตรของก้านจึงมีผล ไม่มีช่วงตาย และไม่กระโดด
float magToSpeed(float mag, int maxSpeed) {
  if (mag < 0.02f) return 0.0f;
  return MOTOR_MIN_DUTY + expoCurve(mag) * (maxSpeed - MOTOR_MIN_DUTY);
}

// ===========================================================================
//  6. Mecanum mixing / สมการผสมกำลังล้อ mecanum
// ===========================================================================
//   fwd    forward (+) / backward (-)
//   strafe right   (+) / left     (-)
//   rot    clockwise (+) / counter-clockwise (-)
//   each is -255..255
void driveMix(int fwd, int strafe, int rot) {
#if MECANUM_WHEELS
  int fl = fwd + strafe + rot;
  int fr = fwd - strafe - rot;
  int rl = fwd - strafe + rot;
  int rr = fwd + strafe - rot;
#else
  (void)strafe;                     // ordinary wheels cannot move sideways
  int fl = fwd + rot, rl = fwd + rot;
  int fr = fwd - rot, rr = fwd - rot;
#endif

  // Scale instead of clipping: full speed plus full rotation keeps the
  // direction of travel correct.
  // ปรับสเกลแทนการตัดค่า ทิศทางการวิ่งจึงไม่เพี้ยนเวลาเร่งสุดพร้อมหมุนสุด
  const int peak = max(max(abs(fl), abs(fr)), max(abs(rl), abs(rr)));
  if (peak > PWM_MAX) {
    fl = fl * PWM_MAX / peak;
    fr = fr * PWM_MAX / peak;
    rl = rl * PWM_MAX / peak;
    rr = rr * PWM_MAX / peak;
  }

  outFL = slew(outFL, fl);
  outFR = slew(outFR, fr);
  outRL = slew(outRL, rl);
  outRR = slew(outRR, rr);
  setMotor(MOT_FL, outFL);
  setMotor(MOT_FR, outFR);
  setMotor(MOT_RL, outRL);
  setMotor(MOT_RR, outRR);
}

// ===========================================================================
//  7. Joystick events / เหตุการณ์จากจอย
// ===========================================================================
void onConnected(const char* name, const char* address) {
  Serial.printf("\n>>> connected: \"%s\" [%s]  rssi %d dBm\n",
                name, address, MomoJoy.rssi());
  Serial.println("    push the LEFT stick to move, D-pad for straight lines,");
  Serial.println("    RIGHT stick X to spin. A cruise, X auto 1 m, B stop.");
}

void onDisconnected() {
  Serial.println(">>> disconnected - stopping the robot");
  autoDriving   = false;
  cruiseControl = false;
  motorsStop();
  digitalWrite(BUZZER_PIN, LOW);
}

void onButtons(uint32_t pressed, uint32_t released) {
  // --- bench test: spin one wheel while its button is held ----------------
  if (pressed  & MOMO_BTN_L1) setMotor(MOT_FL, SPEED_TEST);
  if (pressed  & MOMO_BTN_L2) setMotor(MOT_RL, SPEED_TEST);
  if (pressed  & MOMO_BTN_R1) setMotor(MOT_FR, SPEED_TEST);
  if (pressed  & MOMO_BTN_R2) setMotor(MOT_RR, SPEED_TEST);
  if (released & MOMO_BTN_L1) setMotor(MOT_FL, 0);
  if (released & MOMO_BTN_L2) setMotor(MOT_RL, 0);
  if (released & MOMO_BTN_R1) setMotor(MOT_FR, 0);
  if (released & MOMO_BTN_R2) setMotor(MOT_RR, 0);

  // --- encoders ------------------------------------------------------------
  if (pressed & MOMO_BTN_SELECT) printEncoders();
  if (pressed & MOMO_BTN_START) {
    noInterrupts();
    encCount[0] = encCount[1] = encCount[2] = encCount[3] = 0;
    interrupts();
    Serial.println("Encoders reset to 0");
  }

  // --- smart functions -----------------------------------------------------
  if (pressed & MOMO_BTN_A) {
    cruiseControl = !cruiseControl;
    if (cruiseControl) autoDriving = false;
    Serial.printf("Cruise control %s\n", cruiseControl ? "ON" : "OFF");
  }
  if (pressed & MOMO_BTN_X) {
    autoDriving   = true;
    cruiseControl = false;
    autoTarget    = encAverage() + AUTO_DRIVE_PULSES;
    autoStart     = millis();
    Serial.printf("Auto drive: %.2f m ahead, target %ld pulses\n",
                  AUTO_DRIVE_PULSES / PULSES_PER_METER, autoTarget);
  }
  if (pressed & MOMO_BTN_B) {
    autoDriving   = false;
    cruiseControl = false;
    motorsStop();
    Serial.println("EMERGENCY STOP");
  }
}

// ===========================================================================
//  8. setup / loop
// ===========================================================================
void setup() {
  parkMotorPins();              // FIRST: no wheel can spin from a floating pin
  Serial.begin(115200);
  delay(300);
  Serial.println("\nMassmore MomoJoy - 05 Robot 4WD (MOMO ESP32-S3)");
#if MECANUM_WHEELS
  Serial.println("wheels: MECANUM - the left stick moves in any direction");
#else
  Serial.println("wheels: standard - the left stick drives forward / back");
#endif

  motorsBegin();

  pinMode(M1_ENCA, INPUT_PULLUP); pinMode(M1_ENCB, INPUT_PULLUP);
  pinMode(M2_ENCA, INPUT_PULLUP); pinMode(M2_ENCB, INPUT_PULLUP);
  pinMode(M3_ENCA, INPUT_PULLUP); pinMode(M3_ENCB, INPUT_PULLUP);
  pinMode(M4_ENCA, INPUT_PULLUP); pinMode(M4_ENCB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(M1_ENCA), isrM1, RISING);
  attachInterrupt(digitalPinToInterrupt(M2_ENCA), isrM2, RISING);
  attachInterrupt(digitalPinToInterrupt(M3_ENCA), isrM3, RISING);
  attachInterrupt(digitalPinToInterrupt(M4_ENCA), isrM4, RISING);

  MomoJoyOptions opt;
  opt.localName        = "MOMO-ROBOT";
  opt.deadzone         = STICK_DEADZONE;
  opt.autoReconnect    = true;
  opt.clearBondsOnBoot = false;
  opt.verbose          = false;

  MomoJoy.onConnect(onConnected);
  MomoJoy.onDisconnect(onDisconnected);
  MomoJoy.onButton(onButtons);

  if (!MomoJoy.begin(opt)) Serial.println("[ERROR] BLE init failed");
}

void loop() {
  MomoJoy.update();

  // ---- fail safe ----------------------------------------------------------
  // No controller, or no report for a while: stop. Covers the case where the
  // link is technically up but the pad has stopped talking.
  // ไม่มีจอย หรือจอยเงียบไปนาน ให้หยุดทันที
  if (!MomoJoy.isConnected() || MomoJoy.msSinceReport() > LINK_QUIET_MS) {
    motorsStop();
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  // ---- horn: R3 held ------------------------------------------------------
  digitalWrite(BUZZER_PIN, MomoJoy.pressed(MOMO_BTN_R3) ? HIGH : LOW);

  // ---- HOME held 2 s: erase the pairing -----------------------------------
  if (MomoJoy.longPressed(MOMO_BTN_HOME, 2000)) {
    motorsStop();
    Serial.println("Erasing BLE bonds - re-pair the joystick");
    MomoJoy.forgetBonds();
    MomoJoy.disconnect();
    delay(500);
    return;
  }

  // ---- fixed rate control loop -------------------------------------------
  static uint32_t lastTick = 0;
  if (millis() - lastTick < CONTROL_TICK_MS) return;
  lastTick = millis();

  // Bench test has priority: while one of those buttons is held, onButtons()
  // owns the motors and the mixer must keep its hands off.
  if (MomoJoy.pressed(MOMO_BTN_L1 | MOMO_BTN_L2 | MOMO_BTN_R1 | MOMO_BTN_R2)) {
    outFL = outFR = outRL = outRR = 0;   // resume from standstill afterwards
    return;
  }

  // ---- auto drive ---------------------------------------------------------
  if (autoDriving) {
    const long remain = autoTarget - encAverage();
    if (millis() - autoStart > AUTO_TIMEOUT_MS) {
      autoDriving = false;
      motorsStop();
      Serial.println("Auto drive TIMEOUT - check the encoders and the wheels");
    } else if (remain > AUTO_SLOW_PULSES) {
      driveMix(AUTO_SPEED_FAST, 0, 0);
    } else if (remain > 0) {
      driveMix(AUTO_SPEED_SLOW, 0, 0);      // ease into the target
    } else {
      autoDriving = false;
      motorsStop();
      Serial.printf("Auto drive finished (%.2f m)\n",
                    AUTO_DRIVE_PULSES / PULSES_PER_METER);
    }
    return;
  }

  // ---- driving ------------------------------------------------------------
  const int maxSpeed = MomoJoy.pressed(MOMO_BTN_Y) ? SPEED_TURBO : SPEED_NORMAL;
  int fwd = 0, strafe = 0, rot = 0;

  if (MomoJoy.dpad()) {
    // D-pad: straight lines at a fixed speed. Nothing to tune, nothing to
    // learn - press the arrow, the robot goes that way.
    // ปุ่มลูกศร วิ่งตรงตามทิศ ความเร็วคงที่ กดแล้วไปเลย
    if (MomoJoy.dpadUp())         fwd    =  SPEED_DPAD;
    else if (MomoJoy.dpadDown())  fwd    = -SPEED_DPAD;
    if (MomoJoy.dpadRight())      strafe =  SPEED_DPAD;
    else if (MomoJoy.dpadLeft())  strafe = -SPEED_DPAD;
  } else {
    // One stick, any direction. Push it where you want to go - forward,
    // sideways or diagonal - and the speed follows how far you pushed.
    // ก้านเดียว ดันไปทางไหนไปทางนั้น เร็วตามระยะที่ดัน
    //
    // The expo curve is applied to the LENGTH of the stick vector, not to each
    // axis separately, so a diagonal keeps its exact direction and only the
    // speed is softened.
    // ใส่ expo ที่ "ความยาว" ของเวกเตอร์ ไม่ใช่แยกแกน ทิศเฉียงจึงตรงเป๊ะ
    const float sx  = MomoJoy.lxf();          // right is +
    const float sy  = -MomoJoy.lyf();         // forward is +
    float mag = sqrtf(sx * sx + sy * sy);
    if (mag > 1.0f) mag = 1.0f;

    if (mag > 0.02f) {
      const float gain = magToSpeed(mag, maxSpeed) / mag;
      strafe = (int)(sx * gain);
      fwd    = (int)(sy * gain);
    }
    if (cruiseControl && fwd == 0 && strafe == 0) fwd = maxSpeed;
  }

  // Rotation always comes from the right stick, so it can be mixed with any
  // of the above. หมุนตัวใช้ก้านขวา ผสมกับการเคลื่อนที่ข้างบนได้ตลอด
  {
    // ROT_GAIN scales the stick, not the output, so slow rotation still gets
    // enough duty to actually turn the robot.
    // หรี่ที่ค่าก้าน ไม่ใช่ที่เอาต์พุต การหมุนช้า ๆ จึงยังมีแรงพอให้หมุนจริง
    const float rxv = MomoJoy.rxf() * ROT_GAIN;
    rot = (int)((rxv < 0 ? -1.0f : 1.0f) * magToSpeed(fabsf(rxv), maxSpeed));
  }

  driveMix(fwd, strafe, rot);

  // ---- telemetry ----------------------------------------------------------
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 500) {
    lastLog = millis();
    Serial.printf("%s fwd=%4d strafe=%4d rot=%4d  out FL%4d FR%4d RL%4d RR%4d"
                  "  %.2f m  batt %u%%\n",
                  MomoJoy.dpad() ? "DPAD " : "STICK",
                  fwd, strafe, rot, outFL, outFR, outRL, outRR,
                  encAverage() / PULSES_PER_METER, MomoJoy.battery());
  }
}
