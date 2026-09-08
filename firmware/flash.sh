#!/usr/bin/env bash
# =============================================================================
#  Flash a prebuilt Factory Test .bin without PlatformIO
#  แฟลชไฟล์ .bin ที่ build ไว้แล้ว โดยไม่ต้องใช้ PlatformIO
#
#  Usage:  ./flash.sh /dev/cu.usbmodem1101 [esp32s3|esp32]
#  macOS:  ls /dev/cu.*   to find the port
#  Requires: pip install -U esptool
# =============================================================================
set -euo pipefail

PORT="${1:-}"
TARGET="${2:-esp32s3}"
BAUD="${BAUD:-921600}"     # if the upload fails, try: BAUD=512000 ./flash.sh ...

[ -n "${PORT}" ] || { echo "Usage: ./flash.sh <serial-port> [esp32s3|esp32]"; exit 1; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if command -v esptool.py >/dev/null 2>&1; then ESPTOOL=esptool.py; else ESPTOOL="python3 -m esptool"; fi

MERGED="${HERE}/MomoJoy_FactoryTest_${TARGET}_merged.bin"
if [ -f "${MERGED}" ]; then
  echo "==> flashing merged image at 0x0"
  ${ESPTOOL} --chip "${TARGET}" --port "${PORT}" --baud "${BAUD}" \
             write_flash -z 0x0 "${MERGED}"
else
  BOOT_OFFSET="0x0"; [ "${TARGET}" = "esp32" ] && BOOT_OFFSET="0x1000"
  echo "==> flashing three-part image"
  ${ESPTOOL} --chip "${TARGET}" --port "${PORT}" --baud "${BAUD}" write_flash -z \
    "${BOOT_OFFSET}" "${HERE}/bootloader_${TARGET}.bin" \
    0x8000  "${HERE}/partitions_${TARGET}.bin" \
    0x10000 "${HERE}/MomoJoy_FactoryTest_${TARGET}.bin"
fi
echo "==> done. Open a serial monitor at 115200 to run the test."
