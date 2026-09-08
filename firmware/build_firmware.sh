#!/usr/bin/env bash
# =============================================================================
#  Massmore_MomoJoy - build the Factory Test firmware binaries
#  สคริปต์ build ไฟล์ .bin ของ Factory Test
#
#  Usage:   ./build_firmware.sh            # ESP32-S3 (default)
#           ./build_firmware.sh esp32      # classic ESP32
#
#  Requirements: PlatformIO Core.  pip install -U platformio
#  Everything else (toolchain, NimBLE) is downloaded or bundled automatically.
#
#  The single most important output is *_merged.bin - one image that starts at
#  offset 0x0 and already contains the bootloader, the partition table, the
#  OTA selector and the application. That is the file a web flasher wants.
#  ไฟล์สำคัญที่สุดคือ *_merged.bin เป็นอิมเมจก้อนเดียวเริ่มที่ 0x0 รวมทุกอย่างไว้แล้ว
#  ใช้อัพขึ้นเว็บ flasher ได้เลย
# =============================================================================
set -euo pipefail

TARGET="${1:-esp32s3}"
ENVNAME="factorytest_${TARGET}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="${HERE}/../PlatformIO"
OUT="${HERE}"

command -v pio >/dev/null 2>&1 || { echo "ERROR: PlatformIO not found. pip install -U platformio"; exit 1; }

echo "==> building ${ENVNAME}"
pio run -d "${PROJ}" -e "${ENVNAME}"

BUILD="${PROJ}/.pio/build/${ENVNAME}"
[ -f "${BUILD}/firmware.bin" ] || { echo "ERROR: build produced no firmware.bin"; exit 1; }

# The ESP32 Arduino core 3.x build already produces firmware.factory.bin:
# bootloader 0x0 + partitions 0x8000 + boot_app0 0xe000 + application 0x10000,
# with the flash size and mode taken from the environment. Use it as-is rather
# than re-merging by hand - a hand-rolled merge is easy to get wrong.
# core 3.x สร้าง firmware.factory.bin ให้อยู่แล้ว รวมครบทุกส่วนและตั้งค่า flash ถูกต้อง
# จึงใช้ไฟล์นี้ตรง ๆ ดีกว่าไป merge เองซึ่งพลาดง่าย
if [ -f "${BUILD}/firmware.factory.bin" ]; then
  cp "${BUILD}/firmware.factory.bin" "${OUT}/MomoJoy_FactoryTest_${TARGET}_merged.bin"
else
  echo "note: firmware.factory.bin not found, merging manually"
  if command -v esptool.py >/dev/null 2>&1; then ESPTOOL=esptool.py; else ESPTOOL="python3 -m esptool"; fi
  CHIP="esp32s3"; BOOT_OFFSET="0x0"; FLASH_SIZE="16MB"
  if [ "${TARGET}" = "esp32" ]; then CHIP="esp32"; BOOT_OFFSET="0x1000"; FLASH_SIZE="4MB"; fi
  BOOTAPP0="$(find "${HOME}/.platformio/packages/framework-arduinoespressif32" -name boot_app0.bin 2>/dev/null | head -1)"
  ${ESPTOOL} --chip "${CHIP}" merge_bin -o "${OUT}/MomoJoy_FactoryTest_${TARGET}_merged.bin" \
    --flash_mode dio --flash_freq 80m --flash_size "${FLASH_SIZE}" \
    "${BOOT_OFFSET}" "${BUILD}/bootloader.bin" \
    0x8000  "${BUILD}/partitions.bin" \
    ${BOOTAPP0:+0xe000 "${BOOTAPP0}"} \
    0x10000 "${BUILD}/firmware.bin"
fi

# The separate parts, for anyone who prefers to flash them at their own offsets.
cp "${BUILD}/firmware.bin"   "${OUT}/MomoJoy_FactoryTest_${TARGET}.bin"
cp "${BUILD}/bootloader.bin" "${OUT}/bootloader_${TARGET}.bin"
cp "${BUILD}/partitions.bin" "${OUT}/partitions_${TARGET}.bin"

echo
echo "==> done, files in ${OUT}:"
ls -lh "${OUT}"/*.bin
echo
echo "Upload MomoJoy_FactoryTest_${TARGET}_merged.bin to the web flasher."
echo "อัพไฟล์ *_merged.bin ขึ้นเว็บ flasher ได้เลย"
