#!/bin/bash
# Copies the UF2 onto the BOOTSEL drive (BOOTSEL held while plugging in),
# waiting up to 5 minutes for it to appear.   tools/flash.sh [file.uf2]
cd "$(dirname "$0")/.."
UF2=${1:-dist/pbc_pico8.uf2}
BOOT=/run/media/$USER/RP2350
for i in $(seq 300); do
  if [ -d $BOOT ]; then cp "$UF2" $BOOT/ && sync && echo "geflasht: $UF2"; exit; fi
  sleep 1
done
echo "kein BOOTSEL-Laufwerk gefunden" >&2; exit 1
