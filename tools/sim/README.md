# Mono UI screen simulator (native)

Render the **real** Wio mono screens on your host as PNGs — no hardware, no
flashing. It compiles the actual `ui::kit` mono engine (`ui_kit_mono.cpp`) and
the actual screen code, swapping only the hardware layer (GxEPD2 panel → an
in-memory buffer) and the data layer (model / mesh → small fixtures).

```sh
tools/sim/build.sh
tools/sim/sim chat chat.png      # render one screen
tools/sim/sim all                # contact sheet of every screen -> all.png
open tools/sim/all.png
```

Screens: `chat quick_reply status settings gps mesh_settings set_gps set_mesh
set_display set_sound set_privacy set_ble compass trail battery team waypoints
waypoint_detail provision keyboard`. Sample data (messages, waypoints, clock,
battery) comes from `model::sim_seed()` in `sim_stubs.cpp` — tweak it to preview
other states.

### Panel target

By default the sim renders the Wio's 250×122 panel. Set `SIM_TARGET=t5` to
preview the same mono engine on the T5 e-paper's 540×960 portrait panel with the
UI scaled up (`mono::set_ui_scale(3)`) — used to evaluate running the mono kit on
the S3 board instead of LVGL, before any on-device work:

```sh
SIM_TARGET=t5 SIM_LANG=en tools/sim/sim chat t5-chat.png
```

## Interactive browser build (WASM)

The static sim renders one screen to a PNG. The **WASM build** compiles the same
mono engine + the *real* mono screen manager (`ui_screen_mgr_mono.cpp`) and the
navigation glue from `main_wio.cpp` (home menu, dashboard, key routing), so you
can navigate the whole UI live in a browser on mock data — no flashing.

```sh
tools/sim/build_web.sh                              # -> web/sim.js + web/sim.wasm
python3 -m http.server -d tools/sim/web 8000        # serve it
open http://localhost:8000
```

Needs Emscripten (`brew install emscripten`); the build script pins a Python
≥3.10 if the default `python3` is Xcode's 3.9. Controls in the page: joystick
D-pad (or keyboard arrows / Enter / Backspace), a **panel** toggle (Wio vs T5
scaled), **lang** (EN/SL), **dark mode**, and a **jump-to-screen** picker. Any
key from the dashboard opens the menu, exactly like the device.

The page reads the engine's grayscale framebuffer (`sim_pixels`/`sim_w`/`sim_h`)
into a `<canvas>` after each `sim_key()`/`sim_tick()`; `sim_tick()` advances time
so the dashboard clock and toast banners run. Exports live in `sim_web.cpp`.

## How it works

- `sim_arduino.h` / `sim_display.cpp` — a `SimDisplay` with the exact
  Adafruit-GFX / GxEPD2 call surface the engine uses, rasterising into a 250×122
  8-bit grayscale buffer. Text uses Adafruit's classic font (`glcdfont.h`,
  derived from the installed lib) so glyphs match the panel pixel-for-pixel.
  Includes a tiny dependency-free PNG writer.
- `sim_stubs.cpp` — host definitions of `model::`, `mesh::task::`,
  `ui::screen_mgr::`, `ui::toast::` plus sample fixtures (`model::sim_seed`).
- `sim_main.cpp` — seeds data, sets the statusbar, builds the chosen screen, and
  saves the PNG.
- The engine builds for the host because `ui_kit_mono.cpp` includes
  `sim_arduino.h` under `-DMESHUI_SIM`; screens already compile clean under
  `-DBOARD_WIO_L1` (their LVGL paths are guarded out).

## Adding a screen

1. Add its `.cpp` to the `SCREENS` list in `build.sh`.
2. Add a `SIM_SCREEN(name)` forward-decl and an `S(name)` row to the `SCREENS[]`
   table in `sim_main.cpp`.
3. Build — the linker names any missing `model::`/`mesh::task::` symbols; add
   no-op stubs for them in `sim_stubs.cpp`.

Only the mono backend is simulated (the LVGL backend already runs on desktop via
its own tooling).
