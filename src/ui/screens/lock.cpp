#include <Arduino.h>
#include "lock.h"
#include "../ui_theme.h"          // SCREEN_* ids + UI_LOCK_* layout
#include "../ui_screen_mgr.h"
#include "../ui_port.h"
#include "../kit/ui_kit.h"
#include "../components/statusbar.h"
#include "../../model.h"
#include "../../board.h"

// Lock screen — ported to the ui::kit facade.

namespace ui::screen::lock {

using namespace ui::kit;

static Handle lbl_node_name = nullptr;
static Handle lbl_time = nullptr;
static Handle lbl_date = nullptr;
static Handle lbl_unread = nullptr;
static Handle lbl_info = nullptr;
static char cached_node_name[64] = {};
static char cached_time[8] = {};
static char cached_date[16] = {};
static char cached_unread[96] = {};
static char cached_info[16] = {};

static void set_label_text(Handle lbl, char* cached, size_t cached_size, const char* text) {
    if (!lbl || !text) return;
    if (strcmp(cached, text) != 0) {
        strncpy(cached, text, cached_size - 1);
        cached[cached_size - 1] = 0;
        set_text(lbl, cached);
    }
}

static void on_unread_click(void*) {
    model::touch_activity();
    model::clear_unread_messages();
    ui::statusbar::show();
    ui::screen_mgr::pop(false);
    ui::screen_mgr::switch_to(SCREEN_HOME, false);
    ui::screen_mgr::push(SCREEN_CHAT, false);
}

static void create(Handle parent) {
    free_layout(parent);

    lbl_node_name = label(parent, model::mesh.node_name ? model::mesh.node_name : T_PAPER_HW_VERSION);
    font(lbl_node_name, Font::Title);
    align(lbl_node_name, Align::TopMid, 0, UI_LOCK_NODE_Y);

    lbl_time = label(parent, "");
    font(lbl_time, Font::ClockLg);
    align(lbl_time, Align::TopMid, 0, UI_LOCK_CLOCK_Y);

    lbl_date = label(parent, "");
    font(lbl_date, Font::Title);
    align(lbl_date, Align::TopMid, 0, UI_LOCK_DATE_Y);

    // Unread messages — tappable, jumps directly to chat
    lbl_unread = label(parent, "");
    font(lbl_unread, Font::Title);
    align(lbl_unread, Align::Center, 0, 30);
    on_click(lbl_unread, on_unread_click, nullptr);
    focusable(lbl_unread);

    lbl_info = label(parent, "");
    font(lbl_info, Font::Body);
    align(lbl_info, Align::BottomMid, 0, -30);
}

void update(uint32_t flags) {
    char buf[96];

    if (flags & model::DIRTY_MESH) {
        set_label_text(lbl_node_name, cached_node_name, sizeof(cached_node_name),
                       model::mesh.node_name ? model::mesh.node_name : "LilyGo T5 ePaper S3 Pro");
    }

    if (flags & model::DIRTY_CLOCK) {
        snprintf(buf, sizeof(buf), "%02d:%02d", model::clock.hour, model::clock.minute);
        set_label_text(lbl_time, cached_time, sizeof(cached_time), buf);

        snprintf(buf, sizeof(buf), "%02d/%02d/20%02d",
            model::clock.day, model::clock.month, model::clock.year);
        set_label_text(lbl_date, cached_date, sizeof(cached_date), buf);
    }

    if (flags & model::DIRTY_SLEEP) {
        if (model::sleep_cfg.unread_messages > 0) {
            if (model::sleep_cfg.last_sender[0]) {
                snprintf(buf, sizeof(buf), "%d new: %s",
                    model::sleep_cfg.unread_messages, model::sleep_cfg.last_sender);
            } else {
                snprintf(buf, sizeof(buf), "%d new messages", model::sleep_cfg.unread_messages);
            }
        } else {
            buf[0] = 0;
        }
        hidden(lbl_unread, buf[0] == 0);
        set_label_text(lbl_unread, cached_unread, sizeof(cached_unread), buf);
    }

    if (flags & model::DIRTY_BATTERY) {
        snprintf(buf, sizeof(buf), "BAT %d%%", model::battery.percent);
        set_label_text(lbl_info, cached_info, sizeof(cached_info), buf);
    }
}

static void entry() {
    ui::port::set_backlight(0);
    ui::statusbar::hide();
    update(model::DIRTY_CLOCK | model::DIRTY_BATTERY | model::DIRTY_MESH | model::DIRTY_SLEEP);
}

static void exit_fn() {}

static void destroy() {
    lbl_node_name = lbl_time = lbl_date = lbl_unread = lbl_info = nullptr;
    cached_node_name[0] = 0;
    cached_time[0] = 0;
    cached_date[0] = 0;
    cached_unread[0] = 0;
    cached_info[0] = 0;
}

void show() {
    if (ui::screen_mgr::top_id() == SCREEN_LOCK) return;
    model::update_clock();
    model::update_battery();
    model::update_mesh();
    ui::screen_mgr::push(SCREEN_LOCK, false);
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::lock
