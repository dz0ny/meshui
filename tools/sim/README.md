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
waypoint_detail provision`. Sample data (messages, waypoints, clock, battery)
comes from `model::sim_seed()` in `sim_stubs.cpp` — tweak it to preview other
states.

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
