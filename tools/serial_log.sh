#!/bin/bash
# Logs the debug firmware (./build.sh -DPBC_SERIAL_LOG=ON) over USB-CDC with
# timestamps, across reconnects. FLASH=1: first copy the seriallog UF2 as soon
# as a BOOTSEL drive shows up.
#   tools/serial_log.sh [logfile]     (default: build/serial.log)
# Stop it with: pkill -f '^/bin/bash .*tools/serial_log.sh'
cd "$(dirname "$0")/.."
LOG=${1:-build/serial.log}
UF2=dist/pbc08_seriallog.uf2
BOOT=/run/media/$USER/RP2350
flashed=$([ "${FLASH:-0}" = 1 ] && echo 0 || echo 1)
mkdir -p "$(dirname "$LOG")"
while true; do
  if [ $flashed = 0 ] && [ -d $BOOT ]; then
    cp "$UF2" $BOOT/ && sync && flashed=1 && echo "$(date +%T.%3N) == geflasht" >> "$LOG"
  fi
  for t in /dev/ttyACM*; do
    [ -e "$t" ] || continue
    echo "$(date +%T.%3N) == verbunden $t" >> "$LOG"
    stty -F "$t" raw -echo 115200 2>>"$LOG"
    while IFS= read -r line; do echo "$(date +%T.%3N) $line" >> "$LOG"; done < "$t"
    echo "$(date +%T.%3N) == getrennt $t" >> "$LOG"
  done
  sleep 0.2
done
