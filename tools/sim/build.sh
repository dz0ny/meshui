#!/usr/bin/env bash
# Build the native mono-UI screen simulator. Output: tools/sim/sim
# Usage: tools/sim/build.sh && tools/sim/sim chat chat.png
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..

CXX=${CXX:-c++}
SCREENS=(
    chat quick_reply status settings gps mesh_settings
    set_gps set_mesh set_display set_sound set_privacy set_ble
    compass trail battery team waypoints waypoint_detail provision
)
SRC=()
for s in "${SCREENS[@]}"; do SRC+=("$ROOT/src/ui/screens/$s.cpp"); done

$CXX -std=c++17 -O1 -g -DMESHUI_SIM -DBOARD_WIO_L1 \
    -I. -I"$ROOT/src" \
    sim_main.cpp \
    sim_display.cpp \
    sim_stubs.cpp \
    "$ROOT/src/ui/kit/ui_kit_mono.cpp" \
    "${SRC[@]}" \
    -o sim
echo "built tools/sim/sim"
