// Native screen simulator for the meshui mono (Wio) UI.
//
// Builds a real screen with the real ui::kit mono engine and the real screen
// code, rendering into an in-memory buffer that is saved as a PNG — no hardware,
// no flashing. Lets us (and Claude) actually see a screen and iterate the layout.
//
//   ./sim <screen> [out.png]      e.g.  ./sim chat chat.png
#include "sim_arduino.h"
#include "../../src/ui/kit/ui_kit.h"
#include "../../src/ui/kit/ui_kit_mono.h"
#include "../../src/ui/ui_screen_mgr.h"
#include "../../src/model.h"
#include "../../src/mesh/mesh_task.h"
#include "glcdfont.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace model { void sim_seed(); }
using namespace ui::kit;

// Forward-declare each screen's lifecycle (defined in its own .cpp). Avoids
// pulling every screen header (and their transitive includes) into the harness.
namespace ui { namespace screen {
#define SIM_SCREEN(ns) namespace ns { extern screen_lifecycle_t lifecycle; }
    SIM_SCREEN(chat) SIM_SCREEN(quick_reply) SIM_SCREEN(status) SIM_SCREEN(settings)
    SIM_SCREEN(gps) SIM_SCREEN(mesh_settings) SIM_SCREEN(set_gps) SIM_SCREEN(set_mesh)
    SIM_SCREEN(set_display) SIM_SCREEN(set_sound) SIM_SCREEN(set_privacy) SIM_SCREEN(set_ble)
    SIM_SCREEN(compass) SIM_SCREEN(trail) SIM_SCREEN(battery) SIM_SCREEN(team)
    SIM_SCREEN(waypoints) SIM_SCREEN(waypoint_detail) SIM_SCREEN(provision)
#undef SIM_SCREEN
}}

// Mirror of main_wio.cpp draw_statusbar so the sim chrome matches the device.
static void draw_statusbar(int w, int h) {
    (void)h;
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(mono::fg());
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", model::clock.hour, model::clock.minute);
    display.setCursor(2, 3); display.print(t);
    char g[12];
    if (model::gps.has_fix) snprintf(g, sizeof(g), "GPS %d", (int)model::gps.satellites);
    else                    snprintf(g, sizeof(g), "GPS --");
    char r[24];
    snprintf(r, sizeof(r), "%s  %d%%", g, model::battery.percent);
    display.setCursor(w - (int)strlen(r) * 6 - 2, 3); display.print(r);
}

// ---- screen registry -------------------------------------------------------
// Some screens early-return unless a selection was set first; the builder seeds
// that here so they have something to draw.
namespace ui { namespace screen { namespace waypoint_detail { void set_index(int); } } }

struct Entry { const char* name; screen_lifecycle_t* life; void (*pre)(); };
#define S(ns) { #ns, &ui::screen::ns::lifecycle, nullptr }
static void pre_waypoint_detail() { ui::screen::waypoint_detail::set_index(0); }
static const Entry SCREENS[] = {
    S(chat), S(quick_reply), S(status), S(settings), S(gps), S(mesh_settings),
    S(set_gps), S(set_mesh), S(set_display), S(set_sound), S(set_privacy), S(set_ble),
    S(compass), S(trail), S(battery), S(team), S(waypoints),
    { "waypoint_detail", &ui::screen::waypoint_detail::lifecycle, pre_waypoint_detail },
    S(provision),
};
#undef S

static screen_lifecycle_t* g_life = nullptr;
static void build_current() { if (g_life && g_life->create) g_life->create(screen_root()); }

static void render_screen(const Entry& e) {
    if (e.pre) e.pre();
    g_life = e.life;
    mono::go(build_current);
}

// ---- montage: tile every screen into one labelled contact sheet ------------
static void blit_text(std::vector<uint8_t>& img, int W, int x, int y, const char* s) {
    for (; *s; s++, x += 6) {
        unsigned char c = (unsigned char)*s;
        for (int i = 0; i < 5; i++) {
            unsigned char line = glcd_font[c * 5 + i];
            for (int j = 0; j < 8; j++, line >>= 1)
                if (line & 1) { int px = x + i, py = y + j; if (px >= 0 && py >= 0 && px < W) img[py * W + px] = 0; }
        }
    }
}

static int montage() {
    const int N = (int)(sizeof(SCREENS) / sizeof(SCREENS[0]));
    const int cols = 3, lbl = 12, gap = 4;
    const int sw = display.width(), sh = display.height();
    const int cellw = sw + gap, cellh = sh + lbl + gap;
    const int rows = (N + cols - 1) / cols;
    const int W = cols * cellw + gap, H = rows * cellh + gap;
    std::vector<uint8_t> img(W * H, 200);   // gray backdrop

    for (int i = 0; i < N; i++) {
        render_screen(SCREENS[i]);
        int cx = gap + (i % cols) * cellw, cy = gap + (i / cols) * cellh;
        blit_text(img, W, cx + 1, cy + 2, SCREENS[i].name);
        const uint8_t* px = display.pixels();
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
                img[(cy + lbl + y) * W + (cx + x)] = px[y * sw + x];
        // 1px frame around each screen
        for (int x = 0; x < sw; x++) { img[(cy + lbl) * W + cx + x] = 0; img[(cy + lbl + sh - 1) * W + cx + x] = 0; }
        for (int y = 0; y < sh; y++) { img[(cy + lbl + y) * W + cx] = 0; img[(cy + lbl + y) * W + cx + sw - 1] = 0; }
    }
    if (!write_gray_png("all.png", W, H, img.data())) { fprintf(stderr, "montage write failed\n"); return 1; }
    printf("wrote all.png (%dx%d, %d screens)\n", W, H, N);
    return 0;
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "chat";
    const char* out   = argc > 2 ? argv[2] : "sim.png";

    model::sim_seed();
    sim_set_millis(1);
    mono::set_statusbar(14, draw_statusbar);

    if (strcmp(which, "all") == 0) return montage();

    const Entry* sel = nullptr;
    for (const auto& e : SCREENS) if (strcmp(e.name, which) == 0) sel = &e;
    if (!sel) {
        fprintf(stderr, "unknown screen '%s'. available:", which);
        for (const auto& e : SCREENS) fprintf(stderr, " %s", e.name);
        fprintf(stderr, "\n");
        return 2;
    }

    render_screen(*sel);        // reset + build tree + render to the buffer

    if (!display.savePng(out)) { fprintf(stderr, "failed to write %s\n", out); return 1; }
    printf("wrote %s (%dx%d) for screen '%s'\n", out, display.width(), display.height(), which);
    return 0;
}
