#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo ""
echo "ESP32-S3 -> Switch Pro Controller"
echo "================================="
echo ""

find_idf_export() {
  local candidates=(
    "$HOME/esp/esp-idf/export.sh"
    "$HOME/esp/v5.5/esp-idf/export.sh"
    "$HOME/.espressif/esp-idf/export.sh"
    "/opt/esp/esp-idf/export.sh"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  if [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/export.sh" ]]; then
    echo "$IDF_PATH/export.sh"
    return 0
  fi
  return 1
}

EXPORT_SH="$(find_idf_export || true)"
if [[ -z "${EXPORT_SH}" ]]; then
  echo "ESP-IDF is not installed yet."
  echo "Go back to the README and finish Step 1 (Mac/Linux), then run this script again."
  exit 1
fi

echo "Loading tools from: $EXPORT_SH"
# shellcheck disable=SC1090
source "$EXPORT_SH"

if ! command -v idf.py >/dev/null 2>&1; then
  echo "Could not load ESP-IDF tools. Open a new terminal and try again."
  exit 1
fi

echo ""
echo "Serial ports that look usable:"
mapfile -t PORTS < <(
  {
    ls /dev/cu.* 2>/dev/null || true
    ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
  } | grep -v -E 'Bluetooth|wlan|debug-console' || true
)

if [[ ${#PORTS[@]} -eq 0 ]]; then
  echo "  (none) - plug the board into the computer, then run this script again."
  exit 1
fi

i=1
for p in "${PORTS[@]}"; do
  echo "  [$i] $p"
  i=$((i + 1))
done
echo ""

if [[ ${#PORTS[@]} -eq 1 ]]; then
  PORT="${PORTS[0]}"
  echo "Using $PORT"
else
  read -r -p "Type the number of the port to use: " choice
  if ! [[ "$choice" =~ ^[0-9]+$ ]] || (( choice < 1 || choice > ${#PORTS[@]} )); then
    echo "That doesn't look like a valid choice."
    exit 1
  fi
  PORT="${PORTS[choice-1]}"
fi

echo ""
echo "Building (first time can take several minutes)..."
idf.py set-target esp32s3
idf.py build

echo ""
echo "Flashing to $PORT ..."
idf.py -p "$PORT" flash

echo ""
echo "Done."
echo "1. Unplug the board from the computer if you want."
echo "2. Plug the board port labeled USB into the Switch."
echo "3. On the Switch: Controllers -> Change Grip/Order"
echo ""
