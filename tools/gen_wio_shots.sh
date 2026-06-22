#!/usr/bin/env bash
# Regenerate the Wio mono screen images used by the web flasher slideshow.
# Renders real screens via the native simulator (tools/sim) into assets/wio-*.png.
set -euo pipefail
cd "$(dirname "$0")/.."

tools/sim/build.sh

SCREENS=(chat team waypoints gps compass settings keyboard)
for s in "${SCREENS[@]}"; do
    SIM_LANG=en tools/sim/sim "$s" "assets/wio-$s.png" >/dev/null
    echo "assets/wio-$s.png"
done
echo "done — ${#SCREENS[@]} screens"
