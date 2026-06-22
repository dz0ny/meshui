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
#include "../../src/ui/i18n.h"
#include "glcdfont.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
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

// Title for the centered status-bar slot (sim shows the screen's name; the device
// maps screen ids to i18n titles in main_wio's title_for()).
static const char* g_title = nullptr;
static int g_sb_scale = 1;   // status-bar text scale (1=Wio, larger for the T5 preview)

// Mirror of main_wio.cpp draw_statusbar, scale-aware so it reads on a big panel.
static void draw_statusbar(int w, int h) {
    int cs = g_sb_scale, cw = 6 * cs;
    int y = (h - 8 * cs) / 2; if (y < 0) y = 0;
    display.setFont(nullptr);
    display.setTextSize(cs);
    display.setTextColor(mono::fg());
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", model::clock.hour, model::clock.minute);
    display.setCursor(2, y); display.print(t);
    char g[12];
    if (model::gps.has_fix) snprintf(g, sizeof(g), "GPS %d", (int)model::gps.satellites);
    else                    snprintf(g, sizeof(g), "GPS --");
    char r[24];
    snprintf(r, sizeof(r), "%s  %d%%", g, model::battery.percent);
    int r_x = w - (int)strlen(r) * cw - 2;
    display.setCursor(r_x, y); display.print(r);

    if (g_title && g_title[0]) {
        // Font::Small already scales with the UI scale, matching the classic
        // clock/gps size above.
        int clock_end = 2 + (int)strlen(t) * cw;
        int tw = mono::text_width(g_title, ui::kit::Font::Small);
        int tx = (w - tw) / 2;
        if (tx < clock_end + 4) tx = clock_end + 4;
        if (tx + tw > r_x - 4) tx = r_x - 4 - tw;
        if (tx >= clock_end + 4) mono::text(tx, y, g_title, ui::kit::Font::Small);
    }
}

// ---- screen registry -------------------------------------------------------
// Some screens early-return unless a selection was set first; the builder seeds
// that here so they have something to draw.
namespace ui { namespace screen { namespace waypoint_detail { void set_index(int); } } }
namespace ui { namespace screen { namespace compass { void set_target_pos(const char*, int32_t, int32_t); } } }

// The home menu has no lifecycle (main_wio builds it inline) — mirror it here so
// the main menu can be previewed too.
static void build_home() {
    using namespace ui::kit;
    Handle lst = list(screen_root());
    gap(lst, 2);
    menu_row(lst, i18n::t(i18n::T_MESSAGES),  nullptr, nullptr);
    if (model::team_count() > 0)
        menu_row(lst, i18n::t(i18n::T_TEAM),  nullptr, nullptr);
    menu_row(lst, i18n::t(i18n::T_TRAIL),     nullptr, nullptr);
    menu_row(lst, i18n::t(i18n::T_WAYPOINTS), nullptr, nullptr);
    menu_row(lst, i18n::t(i18n::T_SETTINGS),  nullptr, nullptr);
}

struct Entry { const char* name; screen_lifecycle_t* life; void (*pre)(); void (*build)(); };
#define S(ns) { #ns, &ui::screen::ns::lifecycle, nullptr, nullptr }
static void pre_waypoint_detail() { ui::screen::waypoint_detail::set_index(0); }
static void pre_compass() { ui::screen::compass::set_target_pos("Hut", 46070000, 14520000); }
static const Entry SCREENS[] = {
    { "home", nullptr, nullptr, build_home },
    S(chat), S(quick_reply), S(status), S(settings), S(gps), S(mesh_settings),
    S(set_gps), S(set_mesh), S(set_display), S(set_sound), S(set_privacy), S(set_ble),
    { "compass", &ui::screen::compass::lifecycle, pre_compass, nullptr },
    S(trail), S(battery), S(team), S(waypoints),
    { "waypoint_detail", &ui::screen::waypoint_detail::lifecycle, pre_waypoint_detail, nullptr },
    S(provision),
    { "keyboard", nullptr, nullptr, nullptr },   // on-screen keyboard modal (synthetic)
};
#undef S

static screen_lifecycle_t* g_life = nullptr;
static void build_current() { if (g_life && g_life->create) g_life->create(screen_root()); }
static void build_empty() {}
static void (*g_build)() = nullptr;
static void run_build() { if (g_build) g_build(); }

static void render_screen(const Entry& e) {
    // Home shows the node name in the status bar like the device does.
    g_title = (strcmp(e.name, "home") == 0) ? mesh::task::node_name() : e.name;
    if (e.pre) e.pre();
    if (e.build) {                       // synthetic builder (home)
        g_build = e.build;
        mono::go(run_build);
        return;
    }
    if (!e.life) {                       // keyboard demo: empty screen + modal
        mono::go(build_empty);
        ui::kit::keyboard_open("Hello", 150, nullptr, nullptr);
        return;
    }
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

    // SIM_TARGET=t5 previews the mono engine at the T5 e-paper's 540x960 portrait
    // panel with the UI scaled up; default is the native 250x122 Wio.
    const char* tgt = getenv("SIM_TARGET");
    if (tgt && strcmp(tgt, "t5") == 0) {
        display.set_size(540, 960);
        g_sb_scale = 3;
        mono::set_ui_scale(3);
        mono::set_statusbar(46, draw_statusbar);
    } else {
        display.set_size(250, 122);
        g_sb_scale = 1;
        mono::set_ui_scale(1);
        mono::set_statusbar(14, draw_statusbar);
    }

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
