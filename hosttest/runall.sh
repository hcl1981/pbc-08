#!/bin/bash
# runs every cart in bbs/ for N frames with some button presses
cd "$(dirname "$0")/bbs"
N=${N:-900}
K="60:32,70:0,120:16,130:0,180:32,190:0,240:16,250:0,300:8,400:40,420:8,500:4,560:36,600:0,650:32,660:0,700:16,710:0,800:9,850:34"
for c in ${CARTS:-*.p8.png *.p8}; do
  [ -f "$c" ] || continue
  out=$(PF_ROOT=. FRAMES=$N KEYS=$K SHOTS=$N HEAP_TOTAL=${HEAP_TOTAL:-0} timeout 120 ../pico8_host "$c" 2>&1)
  st=$?
  mv -f shot_$(printf %04d $N).ppm "shots/${c%%.*}.ppm" 2>/dev/null
  err=$(echo "$out" | grep -iE "error|syntax|attempt|OUT OF" | head -2 | tr '\n' ' ' | cut -c1-150)
  sum=$(echo "$out" | grep -E "^frames|^lua now" | tr '\n' ' ')
  printf "%-28s rc=%d %s %s\n" "$c" $st "$sum" "$err"
done
