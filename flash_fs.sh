#!/usr/bin/env sh
# Build the LittleFS image from the sketch's data/ folder and flash it.
# Usage: ./flash_fs.sh [port]     (default port /dev/ttyACM0)
# Override with env: SKETCH=... PORT=... 
set -e
SKETCH="${SKETCH:-$HOME/projects/arduino/GuardianX}"
PORT="${PORT:-${1:-/dev/ttyACM0}}"
MKL="$HOME/.arduino15/packages/esp32/tools/mklittlefs/4.0.2-db0513a/mklittlefs"
ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool"
OFFSET=0x290000   # 'spiffs' partition in the 'default' ESP32-S3 partition scheme
SIZE=0x160000
IMG="$(mktemp)"

echo "Packing $SKETCH/data -> $IMG"
"$MKL" -c "$SKETCH/data" -b 4096 -p 256 -s "$SIZE" "$IMG"
echo "Flashing LittleFS to $PORT at $OFFSET"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 write-flash "$OFFSET" "$IMG"
rm -f "$IMG"
echo "Done. Open http://192.168.4.1 and http://192.168.4.1/admin"
