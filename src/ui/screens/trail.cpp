#include <cstring>
#include <cstdio>
#include <cmath>
#include "trail.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../components/toast.h"
#include "../../model.h"
#include "../../trail_store.h"
#ifdef BOARD_WIO_L1
  // The mono panel has no ui_theme.h (it is LVGL-only). Local map-button metrics
  // sized for the 250x122 e-ink.
  #define UI_MAP_BTN_W 56
  #define UI_MAP_BTN_H 16
#else
  #include "../ui_theme.h"        // UI_MAP_BTN_* layout
#endif

// GPS breadcrumb trail viewer — ported to the ui::kit facade. The map drawing
// goes through kit::canvas_* (L8 on LVGL, 1-bit on the mono backend), so the
// whole screen is panel-agnostic.

namespace ui::screen::trail {

using namespace ui::kit;

static Handle cv = nullptr;          // map canvas
static Handle stats_label = nullptr;
static Handle empty_label = nullptr;
static Handle btn_toggle = nullptr;
static Timer  update_timer = nullptr;

static uint32_t last_point_count = 0xFFFFFFFF;
static bool     last_active = false;

// ---------- stats overlay ----------

static void fmt_dist(char* buf, size_t n, uint32_t meters) {
    if (meters < 1000) snprintf(buf, n, "%lum", (unsigned long)meters);
    else               snprintf(buf, n, "%.2fkm", meters / 1000.0);
}

static void update_stats() {
    if (!stats_label) return;
    auto& t = model::trail;

    const char* status = !t.isActive() ? i18n::t(i18n::T_STOPPED)
                       : (t.empty() ? i18n::t(i18n::T_WAITING_FIX) : i18n::t(i18n::T_TRACKING));

    char dist[16];
    fmt_dist(dist, sizeof(dist), t.totalDistanceMeters());

    uint32_t es = t.elapsedSeconds();
    char tbuf[12];
    if (es < 3600) snprintf(tbuf, sizeof(tbuf), "%lu:%02lu",
                            (unsigned long)(es / 60), (unsigned long)(es % 60));
    else           snprintf(tbuf, sizeof(tbuf), "%lu:%02lu",
                            (unsigned long)(es / 3600), (unsigned long)((es % 3600) / 60));

    set_textf(stats_label, "%s\nPts %d/%d\nDist %s\nTime %s\nAvg %u km/h",
              status, t.count(), TrailStore::CAPACITY, dist, tbuf, (unsigned)t.avgSpeedKmh());
}

static void update_toggle_label() {
    if (btn_toggle) button_text(btn_toggle, i18n::t(model::trail.isActive() ? i18n::T_STOP : i18n::T_START));
}

// ---------- map render ----------

static void rebuild_map() {
    if (!cv) return;
    const int map_w = canvas_w(cv), map_h = canvas_h(cv);
    if (map_w <= 0 || map_h <= 0) return;

    canvas_clear(cv, Color::Bg);

    auto& t = model::trail;
    const int n = t.count();
    bool have_gps = model::gps.has_fix;
    int32_t gps_lat = have_gps ? (int32_t)(model::gps.lat * 1e6) : 0;
    int32_t gps_lon = have_gps ? (int32_t)(model::gps.lng * 1e6) : 0;

    if (n == 0 && !have_gps) {
        if (empty_label) hidden(empty_label, false);
        canvas_flush(cv);
        return;
    }
    if (empty_label) hidden(empty_label, true);

    // Bounding box over every trail point plus the live position.
    int32_t min_lat = 0, max_lat = 0, min_lon = 0, max_lon = 0;
    bool init = false;
    auto fold = [&](int32_t la, int32_t lo) {
        if (!init) { min_lat = max_lat = la; min_lon = max_lon = lo; init = true; }
        else {
            if (la < min_lat) min_lat = la;  if (la > max_lat) max_lat = la;
            if (lo < min_lon) min_lon = lo;  if (lo > max_lon) max_lon = lo;
        }
    };
    for (int i = 0; i < n; i++) fold(t.at(i).lat_1e6, t.at(i).lon_1e6);
    if (have_gps) fold(gps_lat, gps_lon);

    const int margin = 8;
    const int area_w = map_w - 2 * margin;
    const int area_h = map_h - 2 * margin;
    if (area_w < 8 || area_h < 8) { canvas_flush(cv); return; }

    // Degenerate: everything coincident — mark the centre.
    if (min_lat == max_lat && min_lon == max_lon) {
        int ccx = map_w / 2, ccy = map_h / 2;
        for (int i = -4; i <= 4; i++) { canvas_pixel(cv, ccx + i, ccy, Color::Fg); canvas_pixel(cv, ccx, ccy + i, Color::Fg); }
        canvas_fill_circle(cv, ccx, ccy, 3, Color::Focus);
        canvas_flush(cv);
        update_stats();
        return;
    }

    float avg_lat_rad = ((min_lat + max_lat) / 2.0e6f) * (float)M_PI / 180.0f;
    float lon_scale_geo = cosf(avg_lat_rad);
    if (lon_scale_geo < 0.05f) lon_scale_geo = 0.05f;

    float lat_span = (float)(max_lat - min_lat);
    float lon_span = (float)(max_lon - min_lon) * lon_scale_geo;

    float scale_lat = (float)area_h / (lat_span > 0 ? lat_span : 1.0f);
    float scale_lon = (float)area_w / (lon_span > 0 ? lon_span : 1.0f);
    float scale     = (scale_lat < scale_lon) ? scale_lat : scale_lon;

    int used_w = (int)(lon_span * scale);
    int used_h = (int)(lat_span * scale);
    int off_x  = margin + (area_w - used_w) / 2;
    int off_y  = margin + (area_h - used_h) / 2;

    auto project = [&](int32_t la, int32_t lo, int& px, int& py) {
        px = off_x + (int)((float)(lo - min_lon) * lon_scale_geo * scale);
        py = off_y + (int)((float)(max_lat - la) * scale);
    };

    // Polyline — skip segment boundaries (stop/start gaps).
    if (n > 0) {
        int px0, py0;
        project(t.at(0).lat_1e6, t.at(0).lon_1e6, px0, py0);
        for (int i = 1; i < n; i++) {
            int px1, py1;
            project(t.at(i).lat_1e6, t.at(i).lon_1e6, px1, py1);
            if (!(t.at(i).flags & TRAIL_FLAG_SEG_START))
                canvas_line(cv, px0, py0, px1, py1, Color::Fg);
            px0 = px1; py0 = py1;
        }
        // Start marker (cross).
        int sx, sy; project(t.first().lat_1e6, t.first().lon_1e6, sx, sy);
        for (int i = -4; i <= 4; i++) { canvas_pixel(cv, sx + i, sy, Color::Fg); canvas_pixel(cv, sx, sy + i, Color::Fg); }
    }

    // Current position — live GPS if available, else the newest trail point.
    if (have_gps) {
        int mx, my; project(gps_lat, gps_lon, mx, my);
        canvas_fill_circle(cv, mx, my, 4, Color::Focus);
    } else if (n > 0) {
        int ex, ey; project(t.last().lat_1e6, t.last().lon_1e6, ex, ey);
        canvas_fill_circle(cv, ex, ey, 4, Color::Focus);
    }

    canvas_flush(cv);
    update_stats();
}

// ---------- event handlers ----------

static void on_toggle(void*) {
    model::trail.setActive(!model::trail.isActive());
    update_toggle_label();
    update_stats();
    rebuild_map();
    ui::toast::show(model::trail.isActive()
                        ? i18n::t(model::gps.has_fix ? i18n::T_TRACKING_STARTED : i18n::T_WAITING_GPS_FIX)
                        : i18n::t(i18n::T_TRACKING_STOPPED));
}

static void on_clear(void*) {
    model::trail.setActive(false);
    model::trail.clear();
    update_toggle_label();
    update_stats();
    rebuild_map();
    ui::toast::show(i18n::t(i18n::T_TRAIL_CLEARED));
}

static void update_cb(void*) {
    bool changed = (last_point_count != (uint32_t)model::trail.count()) ||
                   (last_active != model::trail.isActive());
    update_stats();
    if (changed) {
        rebuild_map();
        last_point_count = model::trail.count();
        last_active = model::trail.isActive();
    }
}

// ---------- lifecycle ----------

static void create(Handle parent) {
    Handle content = content_area(parent);
    free_layout(content);   // children below are placed by pos()/align(), not stacked
    // No border around the map — the canvas fills the content area, so the frame
    // is pure decoration and the edge pixels are better spent on the map itself.

    int w = px_width(content), h = px_height(content);
    cv = canvas(content, w, h);
    size(cv, pct(100), pct(100));
    pos(cv, 0, 0);

    stats_label = label(content, "");
    font(stats_label, Font::Small);
    card(stats_label);
    pad(stats_label, 3);
    pos(stats_label, 5, 5);

    empty_label = label(content, i18n::t(i18n::T_NO_GPS_TRAIL));
    font(empty_label, Font::Body);
    card(empty_label);
    pad(empty_label, 6);
    text_center(empty_label);
    align(empty_label, Align::Center, 0, 0);
    hidden(empty_label, true);

    btn_toggle = button(content, i18n::t(i18n::T_START), on_toggle, nullptr);
    size(btn_toggle, UI_MAP_BTN_W, UI_MAP_BTN_H);
    align(btn_toggle, Align::BottomLeft, 6, -6);

    Handle btn_clear = button(content, i18n::t(i18n::T_CLEAR), on_clear, nullptr);
    size(btn_clear, UI_MAP_BTN_W, UI_MAP_BTN_H);
    align(btn_clear, Align::BottomRight, -6, -6);

    update_toggle_label();
    update_stats();
    rebuild_map();
}

static void entry() {
    last_point_count = 0xFFFFFFFF;  // force first redraw
    last_active = !model::trail.isActive();
    update_timer = every(1500, update_cb, nullptr);
    rebuild_map();
}

static void exit_fn() {
    if (update_timer) { stop(update_timer); update_timer = nullptr; }
}

static void destroy() {
    cv = nullptr;
    stats_label = nullptr;
    empty_label = nullptr;
    btn_toggle = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::trail
