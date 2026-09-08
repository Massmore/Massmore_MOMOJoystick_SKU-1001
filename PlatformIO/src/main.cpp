// =============================================================================
//  Massmore_MomoJoy - PlatformIO entry point
//
//  Pick which example to build by changing ONE number below, then press Build.
//  The example files live in ../ArduinoIDE/libraries/Massmore_MomoJoy/examples/
//  so PlatformIO and the Arduino IDE always compile identical code.
//
//  เลือกตัวอย่างโดยแก้เลข MOMO_EXAMPLE ด้านล่าง (1-6) แล้วกด Build ได้เลย
//
//   1  Pairing              pair the joystick, show link status    <- start here
//   2  ReadAllSerial        print every value + calibration dump
//   3  PairByAddress       lock onto one joystick by MAC (several pads nearby)
//   4  Robot2WD_ESP32       2-wheel robot on the ESP32 robot board
//   5  Robot4WD_ESP32S3     4-wheel omni robot on the MOMO ESP32-S3 board
//   6  FactoryTest          guided production self-test (#RESULT / #VERDICT)
//
//  Board reminder: examples 4 and 6 default to the ESP32-S3 environment,
//  example 4 needs env:esp32. เลือก environment ให้ตรงกับบอร์ดด้วย
// =============================================================================
#ifndef MOMO_EXAMPLE
#define MOMO_EXAMPLE 6
#endif

#include <Arduino.h>
#include <Massmore_MomoJoy.h>

// The .ino files are plain C++ and declare their own prototypes, so they can
// be included directly. ไฟล์ .ino เป็น C++ ปกติ include ตรง ๆ ได้เลย

#if   MOMO_EXAMPLE == 1
  #include "../../ArduinoIDE/libraries/Massmore_MomoJoy/examples/01_Pairing/01_Pairing.ino"
#elif MOMO_EXAMPLE == 2
  #include "../../ArduinoIDE/libraries/Massmore_MomoJoy/examples/02_ReadAllSerial/02_ReadAllSerial.ino"
#elif MOMO_EXAMPLE == 3
  #include "../../ArduinoIDE/libraries/Massmore_MomoJoy/examples/03_PairByAddress/03_PairByAddress.ino"
#elif MOMO_EXAMPLE == 4
  #include "../../ArduinoIDE/libraries/Massmore_MomoJoy/examples/04_Robot2WD_ESP32/04_Robot2WD_ESP32.ino"
#elif MOMO_EXAMPLE == 5
  #include "../../ArduinoIDE/libraries/Massmore_MomoJoy/examples/05_Robot4WD_ESP32S3/05_Robot4WD_ESP32S3.ino"
#elif MOMO_EXAMPLE == 6
  #include "../../ArduinoIDE/libraries/Massmore_MomoJoy/examples/06_FactoryTest/06_FactoryTest.ino"
#else
  #error "MOMO_EXAMPLE must be a number from 1 to 6"
#endif
