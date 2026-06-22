#!/usr/bin/env bash
# Build the interactive WASM mono-UI simulator. Output: tools/sim/web/sim.{js,wasm}
# Then:  python3 -m http.server -d tools/sim/web 8000  &&  open http://localhost:8000
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..

if ! command -v em++ >/dev/null 2>&1; then
    echo "error: em++ (Emscripten) not found on PATH." >&2
    echo "  macOS:  brew install emscripten" >&2
    echo "  or:     git clone https://github.com/emscripten-core/emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh" >&2
    exit 1
fi

# Emscripten needs Python >= 3.10, but Xcode's /usr/bin/python3 is often 3.9 and
# wins on PATH. Prepend a newer interpreter's dir if the default is too old.
if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' 2>/dev/null; then
    found=
    for p in python3.14 python3.13 python3.12 python3.11 python3.10; do
        if command -v "$p" >/dev/null 2>&1; then
            PATH="$(dirname "$(command -v "$p")"):$PATH"; found="$p"; break
        fi
    done
    [ -n "$found" ] && echo "using $found for Emscripten" || \
        { echo "error: need Python >= 3.10 for Emscripten (have $(python3 --version))" >&2; exit 1; }
fi

# Same screen set as the native sim (build.sh), minus nothing — all navigable.
SCREENS=(
    chat quick_reply status settings gps mesh_settings
    set_gps set_mesh set_display set_sound set_privacy set_ble
    compass trail battery team waypoints waypoint_detail provision
)
SRC=()
for s in "${SCREENS[@]}"; do SRC+=("$ROOT/src/ui/screens/$s.cpp"); done

mkdir -p web

em++ -std=c++17 -O2 -DMESHUI_SIM -DBOARD_WIO_L1 -DSIM_WEB \
    -I. -I"$ROOT/src" \
    sim_web.cpp \
    sim_display.cpp \
    sim_stubs.cpp \
    "$ROOT/src/ui/kit/ui_kit_mono.cpp" \
    "$ROOT/src/ui/ui_screen_mgr_mono.cpp" \
    "${SRC[@]}" \
    -sMODULARIZE=1 -sEXPORT_NAME=SimModule \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
    -sEXPORTED_FUNCTIONS=_main,_sim_boot,_sim_key,_sim_tick,_sim_goto,_sim_set_invert,_sim_pixels,_sim_w,_sim_h \
    -o web/sim.js

echo "built tools/sim/web/sim.js (+ sim.wasm)"
echo "run:  python3 -m http.server -d $(pwd)/web 8000   then open http://localhost:8000"
