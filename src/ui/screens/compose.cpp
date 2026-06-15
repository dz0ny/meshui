#include <cstring>
#include "compose.h"
#include "../ui_theme.h"
#include "../ui_port.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/text_utils.h"
#include "../../board.h"
#include "../../model.h"
#include "../../sd_log.h"
#include "../../mesh/mesh_task.h"
#include <helpers/AdvertDataHelpers.h>

namespace ui::screen::compose {

static lv_obj_t* scr = NULL;
static lv_obj_t* recipient_list = NULL;
static lv_obj_t* recipient_list_area = NULL;
static lv_obj_t* recipient_card = NULL;
static lv_obj_t* lbl_to = NULL;
static lv_obj_t* lbl_hint = NULL;
static lv_obj_t* ta = NULL;
static lv_obj_t* kb = NULL;
static lv_obj_t* send_btn = NULL;
static lv_obj_t* editor_card = NULL;
static lv_obj_t* char_count = NULL;
static lv_obj_t* lbl_filter = NULL;
static lv_obj_t* picker_panel = NULL;
static lv_obj_t* editor_panel = NULL;
static lv_obj_t* picker_card = NULL;
static lv_obj_t* picker_lbl_to = NULL;
static lv_obj_t* picker_lbl_hint = NULL;
static lv_obj_t* editor_summary_card = NULL;
static lv_obj_t* editor_lbl_to = NULL;
static lv_obj_t* filter_row = NULL;
static lv_obj_t* filter_tabs[3] = {};
static lv_obj_t* filter_tab_labels[3] = {};
static lv_obj_t* first_picker_target = NULL;
static int saved_refresh_mode = UI_REFRESH_MODE_NORMAL;
static bool refresh_mode_overridden = false;

static char recipient_name[32] = {};
static bool recipient_chosen = false;
static bool recipient_is_channel = false;
static uint8_t recipient_channel_idx = 0;

enum PickFilter {
    PICK_FILTER_FAVORITES = 0,
    PICK_FILTER_CHANNELS = 1,
    PICK_FILTER_PEOPLE = 2,
};

static PickFilter current_filter = PICK_FILTER_PEOPLE;

static const char* filter_name(PickFilter filter) {
    switch (filter) {
        case PICK_FILTER_FAVORITES:
            return "Favorites";
        case PICK_FILTER_CHANNELS:
            return "Channels";
        case PICK_FILTER_PEOPLE:
        default:
            return "People";
    }
}

// Contact/channel list for picker
struct PickEntry {
    char name[32];
    bool is_channel;
    uint8_t channel_idx;
    uint8_t flags;
};
static PickEntry pick_entries[MAX_CONTACTS + MAX_GROUP_CHANNELS];
static int pick_count = 0;

void set_recipient(const char* name) {
    if (name) {
        strncpy(recipient_name, name, sizeof(recipient_name) - 1);
        recipient_name[sizeof(recipient_name) - 1] = 0;
        recipient_chosen = true;
        recipient_is_channel = false;
        recipient_channel_idx = 0;
    } else {
        recipient_name[0] = 0;
        recipient_chosen = false;
        recipient_is_channel = false;
        recipient_channel_idx = 0;
    }
}

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void show_editor();
static void show_picker();
static void render_recipient_list();
static void sync_picker_ui();

static void focus_obj(lv_obj_t* obj) {
    if (!obj || !lv_obj_is_valid(obj) || !lv_obj_is_visible(obj)) return;

    lv_group_t* group = lv_group_get_default();
    if (!group) return;

    lv_group_focus_obj(obj);
    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
}

static lv_obj_t* find_first_visible_target(lv_obj_t* parent) {
    if (!parent || !lv_obj_is_valid(parent) || !lv_obj_is_visible(parent)) return NULL;

    uint32_t child_count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (!child || !lv_obj_is_visible(child)) continue;

        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE) || lv_obj_check_type(child, &lv_textarea_class)) {
            return child;
        }

        lv_obj_t* nested = find_first_visible_target(child);
        if (nested) return nested;
    }

    return NULL;
}

static void focus_picker_target() {
    if (first_picker_target && lv_obj_is_valid(first_picker_target) && lv_obj_is_visible(first_picker_target)) {
        focus_obj(first_picker_target);
        return;
    }

#ifndef BOARD_TDECK
    for (int i = 0; i < 3; i++) {
        if (filter_tabs[i] && lv_obj_is_valid(filter_tabs[i]) && lv_obj_is_visible(filter_tabs[i])) {
            focus_obj(filter_tabs[i]);
            return;
        }
    }

    if (picker_panel) {
        focus_obj(find_first_visible_target(picker_panel));
    }
#endif
}

static void focus_editor_target() {
    if (ta && lv_obj_is_valid(ta) && lv_obj_is_visible(ta)) {
        focus_obj(ta);
        return;
    }

    if (send_btn && lv_obj_is_valid(send_btn) && lv_obj_is_visible(send_btn)) {
        focus_obj(send_btn);
    }
}

static bool entry_matches_filter(const PickEntry& entry) {
    switch (current_filter) {
        case PICK_FILTER_FAVORITES:
            return !entry.is_channel && (entry.flags & 0x01) != 0;
        case PICK_FILTER_CHANNELS:
            return entry.is_channel;
        case PICK_FILTER_PEOPLE:
        default:
            return !entry.is_channel;
    }
}

static void enable_typing_refresh_mode() {
    if (!refresh_mode_overridden) {
        saved_refresh_mode = ui::port::get_refresh_mode();
        refresh_mode_overridden = true;
    }
    ui::port::set_refresh_mode(UI_REFRESH_MODE_NORMAL);
}

static void restore_refresh_mode() {
    if (!refresh_mode_overridden) return;
    ui::port::set_refresh_mode(saved_refresh_mode);
    refresh_mode_overridden = false;
}

static void sync_filter_nav() {
#ifdef BOARD_TDECK
    if (lbl_filter) {
        lv_label_set_text(lbl_filter, filter_name(current_filter));
    }
#else
    for (int i = 0; i < 3; i++) {
        bool active = current_filter == (PickFilter)i;
        if (filter_tabs[i]) {
            lv_obj_set_style_bg_color(filter_tabs[i],
                                      lv_color_hex(active ? EPD_COLOR_TEXT : EPD_COLOR_BG), LV_PART_MAIN);
            lv_obj_set_style_border_color(filter_tabs[i], lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
        }
        if (filter_tab_labels[i]) {
            lv_obj_set_style_text_color(filter_tab_labels[i],
                                        lv_color_hex(active ? EPD_COLOR_BG : EPD_COLOR_TEXT), LV_PART_MAIN);
        }
    }
#endif
}

static void update_recipient_card() {
#ifdef BOARD_TDECK
    if (!lbl_to || !lbl_hint) return;

    if (recipient_chosen) {
        static char to_buf[40];
        snprintf(to_buf, sizeof(to_buf), "To: %s", recipient_name);
        lv_label_set_text(lbl_to, to_buf);
    } else {
        lv_label_set_text(lbl_to, "To: Choose");
    }
    lv_label_set_text(lbl_hint, "");
#else
    if (picker_lbl_to) {
        lv_label_set_text(picker_lbl_to, recipient_chosen ? recipient_name : "Choose recipient");
    }
    if (picker_lbl_hint) {
        lv_label_set_text(picker_lbl_hint, recipient_chosen ? "" : "People and channels");
    }
    if (editor_lbl_to) {
        lv_label_set_text(editor_lbl_to, recipient_chosen ? recipient_name : "Choose recipient");
    }
#endif
}

static void update_char_count() {
    if (!char_count || !ta) return;

    const char* text = lv_textarea_get_text(ta);
    size_t len = text ? strlen(text) : 0;
    lv_label_set_text_fmt(char_count, "%d/150", (int)len);
}

static void on_entry_pick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < pick_count) {
        strncpy(recipient_name, pick_entries[idx].name, sizeof(recipient_name) - 1);
        recipient_name[sizeof(recipient_name) - 1] = 0;
        recipient_is_channel = pick_entries[idx].is_channel;
        recipient_channel_idx = pick_entries[idx].channel_idx;
        recipient_chosen = true;
        show_editor();
    }
}

static void on_send(lv_event_t* e) {
    const char* text = lv_textarea_get_text(ta);
    if (!text || !text[0] || !recipient_chosen) return;

    bool sent = recipient_is_channel
        ? mesh::task::send_channel(recipient_channel_idx, text)
        : mesh::task::send_to_name(recipient_name, text);

    if (sent && model::message_count < MAX_STORED_MESSAGES) {
        auto& m = model::messages[model::message_count];
        strncpy(m.sender, mesh::task::node_name(), sizeof(m.sender) - 1);
        m.sender[sizeof(m.sender) - 1] = 0;
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.text[sizeof(m.text) - 1] = 0;
        m.hour = model::clock.hour;
        m.minute = model::clock.minute;
        m.is_self = true;
        model::message_count++;
        sd_log::mark_dirty();
    }

    ui::screen_mgr::switch_to(SCREEN_CHAT, true);
}

static void on_kb_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        on_send(e);
    }
}

static void on_ta_focus(lv_event_t* e) {
    enable_typing_refresh_mode();
    if (kb) lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if (send_btn) lv_obj_clear_flag(send_btn, LV_OBJ_FLAG_HIDDEN);
    model::touch_activity();
}

static void on_ta_blur(lv_event_t* e) {
    restore_refresh_mode();
}

static void on_ta_change(lv_event_t* e) {
    update_char_count();
    model::touch_activity();
}

static void on_recipient_click(lv_event_t* e) {
    if (recipient_chosen) {
        show_picker();
    }
}

static void set_filter(PickFilter filter) {
    current_filter = filter;
    sync_filter_nav();
    render_recipient_list();

    if (!recipient_chosen) {
        focus_picker_target();
    }
}

static void on_filter_cycle(lv_event_t* e) {
    switch (current_filter) {
        case PICK_FILTER_FAVORITES:
            set_filter(PICK_FILTER_CHANNELS);
            break;
        case PICK_FILTER_CHANNELS:
            set_filter(PICK_FILTER_PEOPLE);
            break;
        case PICK_FILTER_PEOPLE:
        default:
            set_filter(PICK_FILTER_FAVORITES);
            break;
    }
}

static void on_filter_pick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < PICK_FILTER_FAVORITES || idx > PICK_FILTER_PEOPLE) return;
    set_filter((PickFilter)idx);
}

static void show_editor() {
    update_recipient_card();
    update_char_count();
    sync_picker_ui();

#ifdef BOARD_TDECK
    if (recipient_list_area) lv_obj_add_flag(recipient_list_area, LV_OBJ_FLAG_HIDDEN);
    if (editor_card) lv_obj_clear_flag(editor_card, LV_OBJ_FLAG_HIDDEN);
    if (send_btn) lv_obj_clear_flag(send_btn, LV_OBJ_FLAG_HIDDEN);
    if (ta) lv_obj_clear_flag(ta, LV_OBJ_FLAG_HIDDEN);
#else
    if (picker_panel) lv_obj_add_flag(picker_panel, LV_OBJ_FLAG_HIDDEN);
    if (editor_panel) lv_obj_clear_flag(editor_panel, LV_OBJ_FLAG_HIDDEN);
#endif

    ui::port::keyboard_focus_invalidate();
    focus_editor_target();
    if (ta) {
        lv_obj_send_event(ta, LV_EVENT_FOCUSED, NULL);
    }
}

static void show_picker() {
    restore_refresh_mode();
    recipient_chosen = false;
    recipient_is_channel = false;
    recipient_channel_idx = 0;
    recipient_name[0] = 0;
    update_recipient_card();
    sync_picker_ui();

#ifdef BOARD_TDECK
    if (recipient_list_area) lv_obj_clear_flag(recipient_list_area, LV_OBJ_FLAG_HIDDEN);
    if (editor_card) lv_obj_add_flag(editor_card, LV_OBJ_FLAG_HIDDEN);
    if (ta) lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN);
    if (send_btn) lv_obj_add_flag(send_btn, LV_OBJ_FLAG_HIDDEN);
    if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
#else
    if (picker_panel) lv_obj_clear_flag(picker_panel, LV_OBJ_FLAG_HIDDEN);
    if (editor_panel) lv_obj_add_flag(editor_panel, LV_OBJ_FLAG_HIDDEN);
#endif

    ui::port::keyboard_focus_invalidate();
    focus_picker_target();
}

static void sync_picker_ui() {
#ifdef BOARD_TDECK
    if (recipient_chosen) {
        lbl_filter = ui::screen_mgr::set_nav_action(NULL, NULL, NULL);
    } else {
        lbl_filter = ui::screen_mgr::set_nav_action(filter_name(current_filter), on_filter_cycle, NULL);
    }
#else
    if (!filter_row) return;
    if (recipient_chosen) {
        lv_obj_add_flag(filter_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(filter_row, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

static void load_entries() {
    pick_count = 0;

    mesh::task::ChannelEntry channels[MAX_GROUP_CHANNELS] = {};
    int channel_count = mesh::task::get_channels(channels, MAX_GROUP_CHANNELS);
    for (int i = 0; i < channel_count && pick_count < (int)(sizeof(pick_entries) / sizeof(pick_entries[0])); i++) {
        strncpy(pick_entries[pick_count].name, channels[i].name, sizeof(pick_entries[pick_count].name) - 1);
        pick_entries[pick_count].name[sizeof(pick_entries[pick_count].name) - 1] = 0;
        pick_entries[pick_count].is_channel = true;
        pick_entries[pick_count].channel_idx = channels[i].idx;
        pick_entries[pick_count].flags = 0;
        pick_count++;
    }

    // Then contacts — only chat nodes and rooms, not relays/sensors
    for (int i = 0; i < model::contact_count && pick_count < (int)(sizeof(pick_entries) / sizeof(pick_entries[0])); i++) {
        const model::ContactEntry& contact = model::contacts[i];
        if (contact.type == ADV_TYPE_REPEATER || contact.type == ADV_TYPE_SENSOR) continue;
        strncpy(pick_entries[pick_count].name, contact.name, sizeof(pick_entries[pick_count].name) - 1);
        pick_entries[pick_count].name[sizeof(pick_entries[pick_count].name) - 1] = 0;
        pick_entries[pick_count].is_channel = false;
        pick_entries[pick_count].channel_idx = 0;
        pick_entries[pick_count].flags = contact.flags;
        ui::text::strip_emoji(pick_entries[pick_count].name);
        pick_count++;
    }
}

static void render_recipient_list() {
    if (!recipient_list) return;

    first_picker_target = NULL;
    lv_obj_clean(recipient_list);

    int shown = 0;
    for (int i = 0; i < pick_count; i++) {
        if (!entry_matches_filter(pick_entries[i])) continue;

        char label[40];
        if (pick_entries[i].is_channel) {
            snprintf(label, sizeof(label), "# %s", pick_entries[i].name);
        } else if ((pick_entries[i].flags & 0x01) != 0) {
            snprintf(label, sizeof(label), "\xE2\x98\x85 %s", pick_entries[i].name);
        } else {
            strncpy(label, pick_entries[i].name, sizeof(label) - 1);
            label[sizeof(label) - 1] = 0;
        }

        lv_obj_t* item = ui::nav::menu_item(recipient_list, NULL, label, on_entry_pick, (void*)(intptr_t)i);
        if (!first_picker_target) {
            first_picker_target = item;
        }
        shown++;
    }

    if (shown == 0) {
        lv_obj_t* empty = lv_label_create(recipient_list);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_flex_grow(empty, 1);
        lv_obj_set_style_text_font(empty, UI_FONT_TITLE, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        if (current_filter == PICK_FILTER_FAVORITES) {
            lv_label_set_text(empty, "\n\nNo favorites yet");
        } else if (current_filter == PICK_FILTER_CHANNELS) {
            lv_label_set_text(empty, "\n\nNo channels yet");
        } else {
            lv_label_set_text(empty, "\n\nNo people yet");
        }
    }

    ui::port::keyboard_focus_invalidate();
}

#ifndef BOARD_TDECK
static lv_obj_t* create_full_panel(lv_obj_t* parent) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(panel, 0, 0);
    // Opaque background, not transparent: the picker and editor panels overlap,
    // and on e-ink (no alpha blending, persistent pixels) a transparent panel
    // leaves the previous panel's widgets — e.g. the "Choose recipient" card —
    // showing through behind the one now on top. A solid background repaints the
    // whole area on show, clearing the panel underneath.
    lv_obj_set_style_bg_color(panel, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void style_epaper_card(lv_obj_t* card, lv_coord_t radius, lv_coord_t pad) {
    lv_obj_set_style_bg_color(card, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(card, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_coord_t compose_keyboard_height() {
#if UI_COMPOSE_SHOW_KB
    return board::peri_status[E_PERI_KEYBOARD] ? 0 : UI_COMPOSE_KB_H;
#else
    return 0;
#endif
}

static lv_coord_t compose_send_bottom() {
    lv_coord_t keyboard_height = compose_keyboard_height();
    lv_coord_t footer_gap = keyboard_height > 0 ? UI_COMPOSE_SEND_GAP : UI_COMPOSE_EDITOR_BOTTOM_PAD;
    return -(keyboard_height + footer_gap);
}

static lv_coord_t compose_send_top() {
    lv_obj_update_layout(scr);
    lv_coord_t parent_height = lv_obj_get_content_height(scr);
    return parent_height - UI_TEXT_BTN_HEIGHT + compose_send_bottom();
}

static lv_coord_t compose_textarea_height() {
    lv_coord_t height = compose_send_top() - UI_COMPOSE_TA_Y - UI_COMPOSE_TEXT_GAP;
    return height > 140 ? height : 140;
}

static lv_coord_t compose_picker_list_height() {
    lv_obj_update_layout(scr);
    lv_coord_t parent_height = lv_obj_get_content_height(scr);
    lv_coord_t height = parent_height - UI_COMPOSE_PICKER_LIST_Y - UI_COMPOSE_PICKER_BOTTOM_PAD;
    return height > 0 ? height : 0;
}

static lv_obj_t* create_filter_tab(lv_obj_t* parent, const char* text, PickFilter filter) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_height(btn, lv_pct(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_border_width(btn, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_filter_pick, LV_EVENT_CLICKED, (void*)(intptr_t)filter);
    lv_obj_set_ext_click_area(btn, UI_EXT_CLICK_ACTION);
    ui::port::keyboard_focus_register(btn);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, UI_FONT_SMALL, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(label);

    filter_tabs[filter] = btn;
    filter_tab_labels[filter] = label;
    return btn;
}

static void create_epaper_picker_panel(lv_obj_t* parent) {
    picker_panel = create_full_panel(parent);

    picker_card = lv_obj_create(picker_panel);
    lv_obj_set_size(picker_card, lv_pct(95), UI_COMPOSE_PICKER_CARD_H);
    lv_obj_align(picker_card, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_PICKER_CARD_Y);
    style_epaper_card(picker_card, 16, 16);

    lv_obj_t* recipient_title = lv_label_create(picker_card);
    lv_obj_set_style_text_font(recipient_title, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(recipient_title, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(recipient_title, "Recipient");
    lv_obj_align(recipient_title, LV_ALIGN_TOP_LEFT, 0, 0);

    picker_lbl_to = lv_label_create(picker_card);
    lv_obj_set_width(picker_lbl_to, lv_pct(100));
    lv_obj_set_style_text_font(picker_lbl_to, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(picker_lbl_to, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(picker_lbl_to, LV_LABEL_LONG_DOT);
    lv_obj_align(picker_lbl_to, LV_ALIGN_TOP_LEFT, 0, 30);

    // No "People and channels" hint here — it collided with the big "Choose
    // recipient" value above, and the Favorites/Channels/People tabs below
    // already convey it. picker_lbl_hint stays null; update_recipient_card guards it.

    filter_row = lv_obj_create(picker_panel);
    lv_obj_set_size(filter_row, lv_pct(95), UI_COMPOSE_FILTERS_H);
    lv_obj_align(filter_row, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_FILTERS_Y);
    lv_obj_set_style_bg_opa(filter_row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(filter_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(filter_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(filter_row, UI_COMPOSE_FILTERS_GAP, LV_PART_MAIN);
    lv_obj_clear_flag(filter_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(filter_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filter_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_filter_tab(filter_row, "Favorites", PICK_FILTER_FAVORITES);
    create_filter_tab(filter_row, "Channels", PICK_FILTER_CHANNELS);
    create_filter_tab(filter_row, "People", PICK_FILTER_PEOPLE);

    recipient_list_area = lv_obj_create(picker_panel);
    lv_obj_set_size(recipient_list_area, lv_pct(95), compose_picker_list_height());
    lv_obj_align(recipient_list_area, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_PICKER_LIST_Y);
    lv_obj_set_style_bg_opa(recipient_list_area, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(recipient_list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(recipient_list_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(recipient_list_area, LV_OBJ_FLAG_SCROLLABLE);

    recipient_list = lv_obj_create(recipient_list_area);
    lv_obj_set_size(recipient_list, lv_pct(100), lv_pct(100));
    lv_obj_align(recipient_list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(recipient_list, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(recipient_list, 0, LV_PART_MAIN);
    ui::theme::style_scrollbar_hint(recipient_list);
    lv_obj_set_style_pad_all(recipient_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(recipient_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(recipient_list, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM));
}

static void create_epaper_editor_panel(lv_obj_t* parent) {
    editor_panel = create_full_panel(parent);

    editor_summary_card = lv_obj_create(editor_panel);
    lv_obj_set_size(editor_summary_card, lv_pct(95), UI_COMPOSE_EDITOR_CARD_H);
    lv_obj_align(editor_summary_card, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_EDITOR_CARD_Y);
    style_epaper_card(editor_summary_card, 16, 16);
    lv_obj_add_flag(editor_summary_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(editor_summary_card, on_recipient_click, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(editor_summary_card, UI_EXT_CLICK_ACTION);
    ui::port::keyboard_focus_register(editor_summary_card);

    lv_obj_t* summary_title = lv_label_create(editor_summary_card);
    lv_obj_set_style_text_font(summary_title, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(summary_title, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(summary_title, "To");
    lv_obj_align(summary_title, LV_ALIGN_TOP_LEFT, 0, 0);

    editor_lbl_to = lv_label_create(editor_summary_card);
    lv_obj_set_width(editor_lbl_to, lv_pct(84));
    lv_obj_set_style_text_font(editor_lbl_to, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(editor_lbl_to, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(editor_lbl_to, LV_LABEL_LONG_DOT);
    lv_obj_align(editor_lbl_to, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t* summary_arrow = lv_label_create(editor_summary_card);
    lv_obj_set_style_text_font(summary_arrow, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(summary_arrow, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(summary_arrow, LV_SYMBOL_RIGHT);
    lv_obj_align(summary_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    editor_card = lv_obj_create(editor_panel);
    lv_obj_set_size(editor_card, lv_pct(95), UI_COMPOSE_MESSAGE_H);
    lv_obj_align(editor_card, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_MESSAGE_Y);
    lv_obj_set_style_bg_opa(editor_card, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(editor_card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(editor_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(editor_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* msg_title = lv_label_create(editor_card);
    lv_obj_set_style_text_font(msg_title, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_title, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(msg_title, "Message");
    lv_obj_align(msg_title, LV_ALIGN_LEFT_MID, 0, 0);

    char_count = lv_label_create(editor_card);
    lv_obj_set_style_text_font(char_count, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(char_count, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(char_count, "0/150");
    lv_obj_align(char_count, LV_ALIGN_RIGHT_MID, 0, 0);

    ta = lv_textarea_create(editor_panel);
    lv_obj_set_size(ta, lv_pct(95), compose_textarea_height());
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_TA_Y);
    lv_textarea_set_placeholder_text(ta, "Write a short message");
    lv_textarea_set_max_length(ta, 150);
    lv_textarea_set_one_line(ta, false);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, UI_BORDER_THIN, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ta, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, on_ta_blur, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(ta, on_ta_change, LV_EVENT_VALUE_CHANGED, NULL);
    ui::port::keyboard_focus_register(ta);

#if UI_COMPOSE_SHOW_KB
    if (!board::peri_status[E_PERI_KEYBOARD]) {
        kb = lv_keyboard_create(editor_panel);
        lv_keyboard_set_textarea(kb, ta);
        lv_buttonmatrix_clear_button_ctrl_all(kb, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
        lv_buttonmatrix_set_button_ctrl_all(kb, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
        lv_obj_set_size(kb, lv_pct(100), UI_COMPOSE_KB_H);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_text_font(kb, UI_FONT_TITLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(kb, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
        lv_obj_set_style_text_color(kb, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_anim_duration(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(kb, lv_color_hex(EPD_COLOR_BG), LV_PART_ITEMS);
        lv_obj_set_style_text_color(kb, lv_color_hex(EPD_COLOR_TEXT), LV_PART_ITEMS);
        lv_obj_set_style_border_color(kb, lv_color_hex(EPD_COLOR_BORDER), LV_PART_ITEMS);
        lv_obj_set_style_border_width(kb, UI_BORDER_THIN, LV_PART_ITEMS);
        lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
        lv_obj_set_style_anim_duration(kb, 0, LV_PART_ITEMS);
        lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_ALL, NULL);
    }
#endif

    send_btn = ui::nav::text_button(editor_panel, "Send", on_send, NULL);
    lv_obj_align(send_btn, LV_ALIGN_BOTTOM_MID, 0, compose_send_bottom());
}
#endif

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

#ifdef BOARD_TDECK
    recipient_card = lv_obj_create(parent);
    lv_obj_set_size(recipient_card, lv_pct(95), UI_COMPOSE_RECIPIENT_H);
    lv_obj_align(recipient_card, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_RECIPIENT_Y);
    lv_obj_set_style_bg_color(recipient_card, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(recipient_card, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(recipient_card, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(recipient_card, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(recipient_card, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(recipient_card, 4, LV_PART_MAIN);
    lv_obj_clear_flag(recipient_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(recipient_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(recipient_card, on_recipient_click, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(recipient_card, 15);
    ui::port::keyboard_focus_register(recipient_card);

    lbl_to = lv_label_create(recipient_card);
    lv_obj_set_width(lbl_to, lv_pct(85));
    lv_obj_set_style_text_font(lbl_to, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_to, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(lbl_to, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_to, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* recipient_arrow = lv_label_create(recipient_card);
    lv_obj_set_style_text_font(recipient_arrow, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(recipient_arrow, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(recipient_arrow, LV_SYMBOL_RIGHT);
    lv_obj_align(recipient_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lbl_hint = lv_label_create(recipient_card);
    lv_obj_set_width(lbl_hint, lv_pct(92));
    lv_obj_set_style_text_font(lbl_hint, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_hint, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(lbl_hint, LV_OBJ_FLAG_HIDDEN);

    recipient_list_area = lv_obj_create(parent);
    lv_obj_update_layout(parent);
    lv_coord_t picker_height = lv_obj_get_content_height(parent) - UI_COMPOSE_LIST_Y;
    if (picker_height < 0) {
        picker_height = 0;
    }
    lv_obj_set_size(recipient_list_area, lv_pct(95), picker_height);
    lv_obj_align(recipient_list_area, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_LIST_Y);
    lv_obj_set_style_bg_opa(recipient_list_area, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(recipient_list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(recipient_list_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(recipient_list_area, LV_OBJ_FLAG_SCROLLABLE);

    recipient_list = lv_obj_create(recipient_list_area);
    lv_obj_set_size(recipient_list, lv_pct(100), lv_pct(100));
    lv_obj_align(recipient_list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(recipient_list, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(recipient_list, 0, LV_PART_MAIN);
    ui::theme::style_scrollbar_hint(recipient_list);
    lv_obj_set_style_pad_all(recipient_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(recipient_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(recipient_list, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM));

    editor_card = lv_obj_create(parent);
    lv_obj_set_size(editor_card, lv_pct(95), UI_COMPOSE_EDITOR_H);
    lv_obj_align(editor_card, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_EDITOR_Y);
    lv_obj_set_style_bg_opa(editor_card, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(editor_card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(editor_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(editor_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* msg_title = lv_label_create(editor_card);
    lv_obj_set_style_text_font(msg_title, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_title, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(msg_title, "Message");
    lv_obj_align(msg_title, LV_ALIGN_LEFT_MID, 0, 0);

    char_count = lv_label_create(editor_card);
    lv_obj_set_style_text_font(char_count, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(char_count, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(char_count, "0/150");
    lv_obj_align(char_count, LV_ALIGN_RIGHT_MID, 0, 0);

    ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, lv_pct(95), UI_COMPOSE_TA_H);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, UI_COMPOSE_TA_Y);
    lv_textarea_set_placeholder_text(ta, "Write a short message");
    lv_textarea_set_max_length(ta, 150);
    lv_textarea_set_one_line(ta, false);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, UI_BORDER_THIN, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ta, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, on_ta_blur, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(ta, on_ta_change, LV_EVENT_VALUE_CHANGED, NULL);
    ui::port::keyboard_focus_register(ta);

#if UI_COMPOSE_SHOW_KB
    if (!board::peri_status[E_PERI_KEYBOARD]) {
        kb = lv_keyboard_create(parent);
        lv_keyboard_set_textarea(kb, ta);
        lv_buttonmatrix_clear_button_ctrl_all(kb, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
        lv_buttonmatrix_set_button_ctrl_all(kb, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
        lv_obj_set_size(kb, lv_pct(100), 320);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_text_font(kb, UI_FONT_TITLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(kb, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
        lv_obj_set_style_text_color(kb, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_anim_duration(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(kb, lv_color_hex(EPD_COLOR_BG), LV_PART_ITEMS);
        lv_obj_set_style_text_color(kb, lv_color_hex(EPD_COLOR_TEXT), LV_PART_ITEMS);
        lv_obj_set_style_border_color(kb, lv_color_hex(EPD_COLOR_BORDER), LV_PART_ITEMS);
        lv_obj_set_style_border_width(kb, UI_BORDER_THIN, LV_PART_ITEMS);
        lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
        lv_obj_set_style_anim_duration(kb, 0, LV_PART_ITEMS);
        lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_ALL, NULL);
    }
#endif

    send_btn = ui::nav::text_button(parent, "Send", on_send, NULL);
    lv_obj_align(send_btn, LV_ALIGN_BOTTOM_MID, 0, UI_COMPOSE_SEND_BOTTOM);
#else
    create_epaper_picker_panel(parent);
    create_epaper_editor_panel(parent);
#endif

    model::refresh_contacts();
    load_entries();
    sync_filter_nav();
    render_recipient_list();
    update_recipient_card();
    update_char_count();
    sync_picker_ui();

    if (recipient_chosen) {
        show_editor();
    } else {
        show_picker();
    }
}

static void entry() {
    if (recipient_chosen) {
        enable_typing_refresh_mode();
        focus_editor_target();
    } else {
        focus_picker_target();
    }
}

static void exit_fn() {
    restore_refresh_mode();
}

static void destroy() {
    restore_refresh_mode();
    scr = NULL;
    recipient_list = NULL;
    recipient_list_area = NULL;
    recipient_card = NULL;
    lbl_to = NULL;
    lbl_hint = NULL;
    ta = NULL;
    kb = NULL;
    send_btn = NULL;
    editor_card = NULL;
    char_count = NULL;
    lbl_filter = NULL;
    picker_panel = NULL;
    editor_panel = NULL;
    picker_card = NULL;
    picker_lbl_to = NULL;
    picker_lbl_hint = NULL;
    editor_summary_card = NULL;
    editor_lbl_to = NULL;
    filter_row = NULL;
    first_picker_target = NULL;
    for (int i = 0; i < 3; i++) {
        filter_tabs[i] = NULL;
        filter_tab_labels[i] = NULL;
    }
    saved_refresh_mode = UI_REFRESH_MODE_NORMAL;
    refresh_mode_overridden = false;
    recipient_name[0] = 0;
    recipient_chosen = false;
    recipient_is_channel = false;
    recipient_channel_idx = 0;
    current_filter = PICK_FILTER_PEOPLE;
    pick_count = 0;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::compose
