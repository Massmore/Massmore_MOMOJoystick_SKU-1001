# Factory Test Firmware

**MOMO Joystick Controller · SKU-1001** — production self-test for the ESP32-S3
(and classic ESP32) host board.
เฟิร์มแวร์ทดสอบสายการผลิต ใช้ตรวจว่าบอร์ดและจอยทำงานครบทุกฟังก์ชันก่อนส่งมอบ

The source is example **06_FactoryTest** in the library, so what you flash here
is exactly what you can open and edit in the Arduino IDE or PlatformIO.

---

## 1. Files / ไฟล์ในโฟลเดอร์นี้

| File | Offset | Purpose |
| --- | --- | --- |
| **`MomoJoy_FactoryTest_esp32s3_merged.bin`** | **`0x0`** | **one complete image — use this one** |
| `MomoJoy_FactoryTest_esp32s3.bin` | `0x10000` | application only |
| `bootloader_esp32s3.bin` | `0x0` | bootloader |
| `partitions_esp32s3.bin` | `0x8000` | partition table |
| `build_firmware.sh` | — | rebuild every binary from source |
| `flash.sh` | — | flash without installing PlatformIO |

### Which file goes to the web flasher? / อัพไฟล์ไหนขึ้นเว็บ

**`MomoJoy_FactoryTest_esp32s3_merged.bin`** — that is the merged image. It is
the file the ESP32 Arduino core writes as `firmware.factory.bin` inside
`PlatformIO/.pio/build/<env>/`, copied here under a clearer name. It already
contains everything, laid out from address `0x0`:

```
 0x0000   bootloader.bin
 0x8000   partitions.bin
 0xe000   boot_app0.bin      <- OTA selector
 0x10000  firmware.bin       <- the application
```

So the web flasher writes it at **offset `0x0`** and nothing else is needed.
If the site asks for a fixed name such as `merged-firmware.bin`, just rename
this file — the contents are what matter.

ไฟล์ที่ต้องอัพขึ้นเว็บคือ **`MomoJoy_FactoryTest_esp32s3_merged.bin`** ซึ่งก็คือ
`firmware.factory.bin` ที่ core สร้างให้ใน `.pio/build/<env>/` นั่นเอง แค่เปลี่ยนชื่อให้อ่านง่ายขึ้น
ข้างในรวม bootloader + partition + boot_app0 + แอปไว้ครบ เขียนที่ offset `0x0` อย่างเดียวจบ
ถ้าเว็บบังคับชื่อไฟล์เป็น `merged-firmware.bin` ก็เปลี่ยนชื่อได้เลย เนื้อในเหมือนกัน

> **Do not** upload `firmware.bin` on its own — it is the application only and
> the board will not boot from it without the bootloader and partition table.
> อย่าอัพ `firmware.bin` เดี่ยว ๆ เพราะเป็นแค่ตัวแอป ไม่มี bootloader บอร์ดจะบูตไม่ขึ้น

> If the `.bin` files are missing, run `./build_firmware.sh` once — it builds
> them from the same source tree.
> ถ้ายังไม่มีไฟล์ .bin ให้รัน `./build_firmware.sh` หนึ่งครั้ง

---

## 2. Flash it / วิธีอัพโหลด

### Option A — the script (macOS / Linux)

```bash
ls /dev/cu.*                                   # find your port on macOS
./flash.sh /dev/cu.usbmodem1101 esp32s3
```

If the upload fails halfway, retry at a lower speed:

```bash
BAUD=512000 ./flash.sh /dev/cu.usbmodem1101 esp32s3
```

ถ้าอัพโหลดหลุดกลางทาง ให้ลดความเร็วเป็น 512000 แล้ว 460800 ตามลำดับ

### Option B — esptool directly

```bash
pip install -U esptool
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 921600 \
           write_flash -z 0x0 MomoJoy_FactoryTest_esp32s3_merged.bin
```

### Option D — a web flasher

Upload `MomoJoy_FactoryTest_esp32s3_merged.bin` and have it written at offset
`0x0`. อัพไฟล์ *_merged.bin แล้วให้เขียนที่ offset 0x0

### Option C — rebuild and upload from source

```bash
cd ../PlatformIO
pio run -e factorytest_esp32s3 -t upload -t monitor
```

Then open a serial monitor at **115200 8N1**.

---

## 3. What happens after upload / อัพโหลดแล้วจะเกิดอะไรขึ้น

Only two things are needed: the board and the joystick. Nothing else has to be
connected. ต้องมีแค่บอร์ดกับจอย ไม่ต้องต่ออุปกรณ์อื่น

The test is **guided**: the serial monitor asks for one thing at a time and
waits for it. It also **clears every stored BLE pairing on boot**, so each unit
is tested from a clean state.
เฟิร์มแวร์จะไล่ทีละขั้น บอกให้กดปุ่มอะไรแล้วรอ และล้าง bond ทั้งหมดตอนบูตทุกครั้ง

**Board check (instant)** — chip model, revision, cores, CPU MHz, flash size,
free heap, then BLE init.

```
#RESULT CHIP=ESP32-S3
#RESULT FLASH_MB=16
#RESULT BLE_INIT=PASS
```

**STEP 0 — pairing (up to 60 s)** — slide the joystick mode switch to **`D`**,
hold **HOME** about 3 s until the LED blinks fast. It bonds, then reads and
parses the HID report map.

```
STEP 0/18  pair the joystick
           -> connected
#RESULT PAIR=PASS
#RESULT NAME=MOMO Controller
#RESULT HID=PASS
```

**STEP 1..18 — guided input test** — one prompt at a time, 20 s each:

```
STEP 1/18  press  A
           -> PASS
#RESULT BTN_A=PASS

STEP 7/18  pull   L2  fully (left trigger)
            953 / 1023
           -> PASS
#RESULT TRIG_L2=PASS

STEP 15/18 rotate the LEFT stick fully around the edge
           X span  1019   Y span  1020
           -> PASS
#RESULT STICK_LEFT=PASS
```

The order is: A, B, X, Y, L1, R1, L2, R2, L3, R3, D-pad UP / DOWN / LEFT /
RIGHT, left stick, right stick, SELECT, START.

**HOME is not tested.** On most pads HOME is the power / pairing button: a short
press is handled inside the controller and never reaches the host, so the step
could only ever time out. Set `TEST_HOME_BUTTON` to `1` in the sketch if your
controller really does report it as a normal button.
**ไม่ทดสอบปุ่ม HOME** เพราะจอยส่วนใหญ่ใช้เป็นปุ่มเปิดปิด/จับคู่ กดสั้นแล้วไม่ส่งค่าออกมา
ถ้าจอยรุ่นไหนส่งค่าจริง ให้ตั้ง `TEST_HOME_BUTTON` เป็น 1 ในไฟล์ตัวอย่าง

Press the wrong button and it says so and keeps waiting:

```
           (that was B - press the one above)
```

**Operator keys in the serial monitor / คีย์ลัดในหน้า Serial**

| Type | Effect |
| --- | --- |
| `s` + Enter | skip the current step, marked `SKIP` (ข้ามขั้นตอนนี้) |
| `r` + Enter | re-run the whole test on the same joystick (ทดสอบซ้ำกับจอยตัวเดิม) |

A step that gets no input for 20 s is marked `FAIL` and the test moves on.
ขั้นตอนไหนไม่มีการกดภายใน 20 วินาที จะนับเป็น FAIL แล้วไปขั้นถัดไป

---

## 4. The report / รายงานผล

At the end the firmware prints a full report, then the verdict.

```
========================================
  TEST REPORT
========================================
  controller : MOMO Controller [e4:17:d8:xx:xx:xx]
  battery    : 87 %
  rssi best  : -44 dBm
  rate max   : 66 Hz
  packets    : 3812
  link drop  : no
  --------------------------------------
  BTN_A        PASS
  BTN_B        PASS
  ...
  BTN_START    PASS
  --------------------------------------
  stick left  X:1019  Y:1020   (need 600)
  stick right X:1017  Y:1015
  trigger L2:1023  R2:1023      (need 800)
  --------------------------------------
  pass 18   fail 0   skip 0   of 18
========================================
#VERDICT PASS
========================================
```

**Pass criteria / เกณฑ์ผ่าน**

| Check | Passes when |
| --- | --- |
| `BLE_INIT` | the BLE stack starts |
| `PAIR` | the joystick bonds within 60 s |
| `HID` | report map read and at least one input field parsed |
| `BTN_*` / `DPAD_*` | that button was pressed within its 20 s window (HOME is not tested) |
| `TRIG_L2` / `TRIG_R2` | the trigger reaches 800 of 1023 |
| `STICK_LEFT` / `STICK_RIGHT` | both axes travel at least 600 of 1024 counts |
| `LINK` | report rate peaks at 5 Hz or more and the link never dropped |

The verdict is `PASS` only when nothing failed. A `SKIP` does not fail the unit,
but it does show up in the report as `skip N`, so a skipped step is visible.
ขั้นตอนที่ข้าม (SKIP) ไม่ทำให้ตก แต่จะขึ้นในรายงานให้เห็นว่าข้ามไปกี่ขั้น

Type `r` + Enter to re-run the steps on the same joystick. For a **different**
joystick press **RESET** on the board first, so the stored pairing is cleared and
the unit is tested from a clean state.
จอยตัวใหม่ให้กดรีเซ็ตบอร์ดก่อน เพื่อให้ล้าง bond แล้วเริ่มทดสอบจากสภาพใหม่

---

## 5. Output format for the Web Serial Monitor

| Line | Meaning |
| --- | --- |
| `#TEST <name> START` | a phase began |
| `#RESULT <KEY>=<value>` | one measured value — split on the first `=` |
| `#VERDICT PASS` / `#VERDICT FAIL` | final decision, always the last line |

Per-step keys are `BTN_A` … `BTN_START`, `DPAD_UP` … `DPAD_RIGHT`,
`TRIG_L2`, `TRIG_R2`, `STICK_LEFT`, `STICK_RIGHT`, each `PASS` / `FAIL` / `SKIP`.
Summary keys: `STEPS_PASS`, `STEPS_FAIL`, `STEPS_SKIP`, `STICK_SPAN`,
`TRIGGER_MAX`, `RATE_HZ`, `RSSI`, `BATTERY`, `PACKETS`, `LINK_DROP`, `LINK`.

Everything else on the port is human-readable log and can be ignored by the
parser. บรรทัดอื่นเป็น log สำหรับคนอ่าน ตัว parser ข้ามได้เลย

---

## 6. Troubleshooting / แก้ปัญหาเมื่อใช้งานไม่ได้

| Symptom | Cause / fix |
| --- | --- |
| Serial monitor is blank | ESP32-S3 needs **USB CDC On Boot = Disabled**; monitor at `115200`; on some boards you must press RESET once after opening the port |
| Upload fails, `Timed out waiting for packet header` | lower the baud: `BAUD=512000 ./flash.sh …`, then `460800`; try a different USB cable (data, not charge-only) |
| `A fatal error occurred: Failed to connect` | hold **BOOT**, tap **RESET**, release **BOOT**, then start the upload |
| `#RESULT PAIR=FAIL timeout` | controller was not in pairing mode: mode switch must be on **`D`**, hold **HOME** ~3 s until the LED blinks fast. Charge it if the LED does not light |
| Pairs, then drops instantly | a stale bond on the controller side — clear the pairing on the pad itself, then reset the board |
| `#RESULT HID=FAIL` | the controller is in a non-BLE mode (Bluetooth Classic / 2.4 GHz dongle). Only BLE HID mode works |
| A `BTN_*` step fails even though you pressed it | that button is numbered differently on this unit — open example 02 with `SHOW_RAW_REPORTS 1` to see the real HID button numbers, then adjust the `MomoProfile` |
| `STICK_*=FAIL` with a small span | rotate the stick all the way around the outer edge, not just up and down; a small span on one axis only points at a worn potentiometer |
| `TRIG_*=FAIL` | the trigger was not pulled all the way, or this pad reports digital-only triggers (the live number on screen shows how far it got) |
| Verdict FAIL but every step passed | check `#RESULT LINK_DROP=1` — the link dropped mid-test; move away from Wi-Fi routers / USB 3 ports, which are noisy at 2.4 GHz |
| Typing `s` or `r` does nothing | the serial monitor must send a newline — in PlatformIO's monitor just type the letter and press Enter |
| Sketch does not fit (classic ESP32) | partition scheme must be **Huge APP** (`huge_app.csv`) |

---

Made by **Massmore Biz Co., Ltd.** · [massmore.shop](https://massmore.shop) · MIT
