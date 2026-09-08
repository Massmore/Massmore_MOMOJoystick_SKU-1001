/*
 * Massmore_MomoJoy - 04_Robot2WD_ESP32
 * ---------------------------------------------------------------------------
 * Two wheel robot on the classic ESP32 robot board.
 * หุ่นยนต์ 2 ล้อ บนบอร์ดหุ่นยนต์ ESP32 ตัวธรรมดา
 *
 * Board pins used / ขาที่ใช้บนบอร์ด
 *   M1  IN1 27  IN2 26      left  motor   (มอเตอร์ซ้าย)
 *   M2  IN1 16  IN2 17      right motor   (มอเตอร์ขวา)
 *   Servo1 19   Servo2 32   Servo3 33
 *   VR  34  (speed knob)    SW 36  (button)
 *   I2C SDA 21  SCL 22      LCD CLK 18  MOSI 23  RES 4  DC 2
 *
 * Controls / การบังคับ  -  one stick does everything
 *   LEFT stick        push it where you want to go: forward, back, turn,
 *                     or anything in between. 0-100% by how far you push.
 *                     ก้านซ้ายก้านเดียว ดันไปทางไหนรถไปทางนั้น แรงตามระยะที่ดัน
 *   D-pad             drive straight in that arrow's direction, fixed speed
 *                     ปุ่มลูกศร วิ่งตามทิศนั้นทันที
 *   R2 (analog)       extra throttle on top of the stick
 *   Y (hold)          turbo, unlocks full speed
 *   B                 emergency stop
 *   L1 / R1           bench test: spin the left / right wheel while held
 *   A                 sweep Servo1 to the next position
 *   HOME (hold 2 s)   erase the BLE pairing
 *
 * The VR knob on GPIO34 sets the top speed, so there is nothing to edit in
 * code to make the robot slower or faster.
 * ปุ่มหมุน VR บนบอร์ดใช้ตั้งความเร็วสูงสุด ไม่ต้องแก้โค้ด
 *
 * SAFETY: motor pins are parked LOW first, and the motors stop the instant
 * the BLE link drops or the joystick goes quiet.
 *
 * TOO FAST OR TOO SLOW? / แรงไปหรือช้าไป?
 *   the VR knob     sets the top speed, no code change needed
 *   SLEW_UP         how quickly it picks up speed, lower = gentler
 *   EXPO_AMOUNT     how soft the middle of the stick feels, higher = softer
 *   MOTOR_MIN_DUTY  raise it if the robot buzzes but does not creep
 *   Y button        unlocks SPEED_TURBO while held
 */
#include <Arduino.h>
#include <Massmore_MomoJoy.h>

using namespace massmore;

// ===========================================================================
//  1. Board pins / ขาบนบอร์ด
// ===========================================================================
#define M1_IN1 27      // left motor
#define M1_IN2 26
#define M2_IN1 16      // right motor
#define M2_IN2 17

#define SERVO1_PIN 19
#define SERVO2_PIN 32
#define SERVO3_PIN 33

#define VR_PIN 34      // speed knob, analog in
#define SW_PIN 36      // on-board button, analog-capable input pin

// ===========================================================================
//  2. Tuning / ค่าปรับแต่ง
// ===========================================================================
#define PWM_FREQ_HZ  20000     // 20 kHz, above hearing - no motor whine
#define PWM_RES_BIT      8     // duty 0..255
#define PWM_MAX        255

// --- speed ---------------------------------------------------------------
// Gentle by default: geared motors are strong and full PWM launches the robot
// across the room. The VR knob sets the top speed between these two.
// ตั้งไว้ให้นุ่ม ๆ ก่อน มอเตอร์เกียร์แรงมาก ปุ่มหมุน VR ใช้เลือกความเร็วสูงสุด
#define SPEED_MIN       70     // knob fully counter-clockwise
#define SPEED_MAX      200     // knob fully clockwise
#define SPEED_TURBO    255     // while Y is held
#define SPEED_DPAD     110     // fixed speed in D-pad mode
#define SPEED_TEST     150     // single-wheel bench test

// --- feel ----------------------------------------------------------------
// Below MOTOR_MIN_DUTY a geared motor only buzzes and does not turn, so any
// non-zero command is lifted to at least this much. Set 0 to disable.
// ต่ำกว่าค่านี้มอเตอร์จะแค่สั่นแต่ไม่หมุน จึงยกค่าคำสั่งขึ้นมาให้ถึงขั้นต่ำ
#define MOTOR_MIN_DUTY  40
// Expo curve: 0 = linear, higher = softer around the centre of the stick.
// ยิ่งมากยิ่งนุ่มช่วงกลางก้าน บังคับละเอียดง่ายขึ้น
#define EXPO_AMOUNT   0.60f
// Turning is twitchier than driving, so give it less gain.
// การเลี้ยวไวกว่าการเดินหน้า จึงหรี่ลง
#define TURN_GAIN     0.65f
// Ramp: accelerate slowly, slow down quickly. PWM change per 20 ms tick.
// SLEW_UP 6 -> about 0.8 s from standstill to full speed.
// เร่งช้า ๆ แต่ผ่อนเร็ว ปลอดภัยกว่า
#define SLEW_UP          6
#define SLEW_DOWN       24

#define CONTROL_TICK_MS 20     // control loop period -> 50 Hz
#define LINK_QUIET_MS  400     // no report for this long = treat as lost

#define SERVO_FREQ_HZ   50
#define SERVO_RES_BIT   16

// ===========================================================================
//  3. State / ตัวแปรสถานะ
// ===========================================================================
static int outLeft = 0, outRight = 0;
static uint8_t servoStep = 0;

void setup();
void loop();
void parkMotorPins();
void motorsBegin();
void setMotor(uint8_t in1, uint8_t in2, int speed);
void motorsStop();
void drive(int fwd, int turn);
int  slew(int current, int target);
int  expoSpeed(float norm, int maxSpeed);
int  knobSpeed();
void servoBegin();
void servoWrite(uint8_t pin, int angle);
void onConnected(const char* name, const char* address);
void onDisconnected();
void onButtons(uint32_t pressed, uint32_t released);

// ===========================================================================
//  4. Safety / ปักขาให้ปลอดภัยก่อน
// ===========================================================================
// Called FIRST in setup(). A floating driver input latches the H-bridge on and
// a wheel spins for the whole session - this makes sure that cannot happen.
// เรียกเป็นอย่างแรก ถ้าขาลอย ไดรเวอร์จะค้างและมอเตอร์หมุนเอง บรรทัดนี้กันไว้
void parkMotorPins() {
  static const uint8_t kOutPins[] = {
      M1_IN1, M1_IN2, M2_IN1, M2_IN2,
      SERVO1_PIN, SERVO2_PIN, SERVO3_PIN};
  for (uint8_t i = 0; i < sizeof(kOutPins); ++i) {
    pinMode(kOutPins[i], OUTPUT);
    digitalWrite(kOutPins[i], LOW);
  }
}

// ===========================================================================
//  5. Motors / มอเตอร์
// ===========================================================================
void motorsBegin() {
  // Both pins of each driver are PWM: that is what sign-magnitude drive needs.
  ledcAttach(M1_IN1, PWM_FREQ_HZ, PWM_RES_BIT);
  ledcAttach(M1_IN2, PWM_FREQ_HZ, PWM_RES_BIT);
  ledcAttach(M2_IN1, PWM_FREQ_HZ, PWM_RES_BIT);
  ledcAttach(M2_IN2, PWM_FREQ_HZ, PWM_RES_BIT);
  motorsStop();
}

// speed -255..255 ; 0 = coast, negative = reverse
void setMotor(uint8_t in1, uint8_t in2, int speed) {
  speed = constrain(speed, -PWM_MAX, PWM_MAX);
  if (speed >= 0) {
    ledcWrite(in2, 0);
    ledcWrite(in1, speed);
  } else {
    ledcWrite(in1, 0);
    ledcWrite(in2, -speed);
  }
}

void motorsStop() {
  outLeft = outRight = 0;
  ledcWrite(M1_IN1, 0); ledcWrite(M1_IN2, 0);
  ledcWrite(M2_IN1, 0); ledcWrite(M2_IN2, 0);
}

// Ramp toward the target. Speeding up is deliberately slow so the robot creeps
// away smoothly; slowing down is fast, because stopping should feel immediate.
// เร่งช้า ๆ แต่ผ่อนและหยุดให้เร็ว
int slew(int current, int target) {
  const int step = (abs(target) > abs(current)) ? SLEW_UP : SLEW_DOWN;
  if (target > current) return min(current + step, target);
  if (target < current) return max(current - step, target);
  return current;
}

// Turn a -1..1 stick value into a motor speed. The expo curve softens the
// middle of the stick, and the whole usable range sits between MOTOR_MIN_DUTY
// (the slowest the motor will actually creep) and maxSpeed - so every
// millimetre of stick travel does something, with no dead band and no jump.
// แปลงค่าก้าน -1..1 เป็นความเร็ว ใช้ expo ให้ช่วงกลางนุ่ม และไล่ต่อเนื่องจาก
// MOTOR_MIN_DUTY ถึง maxSpeed ทุกมิลลิเมตรของก้านจึงมีผล ไม่มีช่วงตาย
int expoSpeed(float norm, int maxSpeed) {
  if (norm >  1.0f) norm =  1.0f;
  if (norm < -1.0f) norm = -1.0f;
  const float m = fabsf(norm);
  if (m < 0.02f) return 0;
  const float shaped = EXPO_AMOUNT * m * m * m + (1.0f - EXPO_AMOUNT) * m;
  const float speed  = MOTOR_MIN_DUTY + shaped * (maxSpeed - MOTOR_MIN_DUTY);
  return (int)(norm < 0 ? -speed : speed);
}

// Two wheels cannot strafe, so one stick means: Y drives, X turns.
// รถ 2 ล้อสไลด์ข้างไม่ได้ ก้านเดียวจึงเป็น แกน Y เดินหน้า แกน X เลี้ยว
void drive(int fwd, int turn) {
  int left  = fwd + turn;
  int right = fwd - turn;

  // Scale instead of clipping, so full throttle plus full turn keeps the
  // heading correct. ปรับสเกลแทนการตัดค่า ทิศทางจึงไม่เพี้ยน
  const int peak = max(abs(left), abs(right));
  if (peak > PWM_MAX) {
    left  = left  * PWM_MAX / peak;
    right = right * PWM_MAX / peak;
  }

  outLeft  = slew(outLeft,  left);
  outRight = slew(outRight, right);
  setMotor(M1_IN1, M1_IN2, outLeft);
  setMotor(M2_IN1, M2_IN2, outRight);
}

// ===========================================================================
//  6. Servos / เซอร์โว   (LEDC, no extra library needed)
// ===========================================================================
void servoBegin() {
  ledcAttach(SERVO1_PIN, SERVO_FREQ_HZ, SERVO_RES_BIT);
  ledcAttach(SERVO2_PIN, SERVO_FREQ_HZ, SERVO_RES_BIT);
  ledcAttach(SERVO3_PIN, SERVO_FREQ_HZ, SERVO_RES_BIT);
  servoWrite(SERVO1_PIN, 90);
  servoWrite(SERVO2_PIN, 90);
  servoWrite(SERVO3_PIN, 90);
}

// angle 0..180 -> 500..2500 us pulse inside a 20000 us frame
void servoWrite(uint8_t pin, int angle) {
  angle = constrain(angle, 0, 180);
  const uint32_t us   = 500 + (uint32_t)angle * 2000 / 180;
  const uint32_t duty = us * 65535UL / 20000UL;
  ledcWrite(pin, duty);
}

// ===========================================================================
//  7. Speed knob / ปุ่มหมุนตั้งความเร็ว
// ===========================================================================
// VR on GPIO34, 12 bit ADC. Turn it down for a classroom, up for a race.
int knobSpeed() {
  const int raw = analogRead(VR_PIN);              // 0..4095
  return SPEED_MIN + (raw * (SPEED_MAX - SPEED_MIN)) / 4095;
}

// ===========================================================================
//  8. Joystick events / เหตุการณ์จากจอย
// ===========================================================================
void onConnected(const char* name, const char* address) {
  Serial.printf("\n>>> connected: \"%s\" [%s]  rssi %d dBm\n",
                name, address, MomoJoy.rssi());
  Serial.println("    push the LEFT stick to drive, D-pad for straight lines");
}

void onDisconnected() {
  Serial.println(">>> disconnected - stopping the robot");
  motorsStop();
}

void onButtons(uint32_t pressed, uint32_t released) {
  // bench test: spin one wheel while its button is held
  if (pressed  & MOMO_BTN_L1) setMotor(M1_IN1, M1_IN2, SPEED_TEST);
  if (pressed  & MOMO_BTN_R1) setMotor(M2_IN1, M2_IN2, SPEED_TEST);
  if (released & MOMO_BTN_L1) setMotor(M1_IN1, M1_IN2, 0);
  if (released & MOMO_BTN_R1) setMotor(M2_IN1, M2_IN2, 0);

  if (pressed & MOMO_BTN_B) {
    motorsStop();
    Serial.println("EMERGENCY STOP");
  }

  // A steps Servo1 through three positions - a gripper or a pan mount.
  if (pressed & MOMO_BTN_A) {
    static const int kAngles[] = {0, 90, 180};
    servoStep = (servoStep + 1) % 3;
    servoWrite(SERVO1_PIN, kAngles[servoStep]);
    Serial.printf("Servo1 -> %d deg\n", kAngles[servoStep]);
  }
}

// ===========================================================================
//  9. setup / loop
// ===========================================================================
void setup() {
  parkMotorPins();              // FIRST: nothing can spin from a floating pin
  Serial.begin(115200);
  delay(300);
  Serial.println("\nMassmore MomoJoy - 04 Robot 2WD (ESP32)");
  Serial.println("LEFT stick drives, D-pad = straight lines, VR knob = top speed");

  motorsBegin();
  servoBegin();
  pinMode(SW_PIN, INPUT);

  MomoJoyOptions opt;
  opt.localName        = "MOMO-ROBOT";
  opt.deadzone         = 40;    // robots need a wider dead zone than games
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

  // ---- fail safe: no joystick, or the joystick went quiet -> stop ---------
  if (!MomoJoy.isConnected() || MomoJoy.msSinceReport() > LINK_QUIET_MS) {
    motorsStop();
    return;
  }

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

  // While a bench-test button is held, onButtons() owns the motors.
  if (MomoJoy.pressed(MOMO_BTN_L1 | MOMO_BTN_R1)) {
    outLeft = outRight = 0;
    return;
  }

  // Top speed: the VR knob, or full scale while Y is held.
  const int maxSpeed = MomoJoy.pressed(MOMO_BTN_Y) ? SPEED_TURBO : knobSpeed();

  int fwd = 0, turn = 0;

  if (MomoJoy.dpad()) {
    // ---- D-pad: straight lines, fixed speed, nothing to tune -------------
    if (MomoJoy.dpadUp())         fwd  =  SPEED_DPAD;
    else if (MomoJoy.dpadDown())  fwd  = -SPEED_DPAD;
    if (MomoJoy.dpadLeft())       turn = -SPEED_DPAD;
    else if (MomoJoy.dpadRight()) turn =  SPEED_DPAD;
  } else {
    // ---- one stick: push it where you want to go -------------------------
    // ly() is negative when the stick is pushed forward, so invert it.
    // The expo curve keeps the first half of the stick travel gentle, which is
    // what makes a strong geared motor controllable.
    // expo ทำให้ครึ่งแรกของก้านนุ่ม คุมมอเตอร์แรง ๆ ได้ง่ายขึ้น
    fwd  = expoSpeed(-MomoJoy.lyf(), maxSpeed);
    turn = expoSpeed(MomoJoy.lxf() * TURN_GAIN, maxSpeed);

    // R2 adds throttle on top, handy for long straights.
    const int boost = (int)(MomoJoy.r2f() * (PWM_MAX - maxSpeed));
    if (fwd > 0) fwd += boost;
  }

  drive(fwd, turn);

  // ---- telemetry ----------------------------------------------------------
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 500) {
    lastLog = millis();
    Serial.printf("%s  fwd=%4d turn=%4d  out L%4d R%4d  max=%3d  batt %u%%\n",
                  MomoJoy.dpad() ? "DPAD  " : "STICK ",
                  fwd, turn, outLeft, outRight, maxSpeed, MomoJoy.battery());
  }
}
