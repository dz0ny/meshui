#include "nav_button.h"
#include "../ui_port.h"
#include "../ui_screen_mgr.h"
#include "../ui_theme.h"
#include "../../nvs_param.h"

namespace ui::nav {

static bool hit_area_debug_state = false;
static bool hit_area_debug_loaded = false;

static void ensure_hit_area_debug_loaded() {
    if (hit_area_debug_loaded) return;
    hit_area_debug_state = nvs_param_get_u8(NVS_ID_HIT_AREA_DEBUG) != 0;
    hit_area_debug_loaded = true;
}

static void style_hit_area(lv_obj_t* obj) {
    ensure_hit_area_debug_loaded();
    lv_obj_set_style_bg_opa(obj, LV_OPA_0, LV_PART_MAIN);
    if (hit_area_debug_state) {
        lv_obj_set_style_pad_all(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, lv_color_hex(EPD_COLOR_FOCUS), LV_PART_MAIN);
    } else {
        lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    }
}

#ifdef BOARD_TDECK
static void set_back_pressed_visual(lv_obj_t* obj, bool pressed) {
    if (!obj) return;
    lv_color_t color = lv_color_hex(pressed ? EPD_COLOR_BORDER : EPD_COLOR_TEXT);
    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        lv_obj_set_style_text_color(child, color, LV_PART_MAIN);
    }
}

static void on_back_press_feedback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(e);
    if (!target) return;
    if (code == LV_EVENT_PRESSED) {
        set_back_pressed_visual(target, true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        set_back_pressed_visual(target, false);
    }
}

static void set_back_focus_visual(lv_obj_t* obj, bool focused) {
    if (!obj) return;
    lv_color_t color = lv_color_hex(focused ? EPD_COLOR_FOCUS : EPD_COLOR_TEXT);
    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        lv_obj_set_style_text_color(child, color, LV_PART_MAIN);
    }
}

static void on_back_focus_feedback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(e);
    if (!target) return;
    if (code == LV_EVENT_FOCUSED) {
        set_back_focus_visual(target, true);
    } else if (code == LV_EVENT_DEFOCUSED) {
        set_back_focus_visual(target, false);
    }
}

static void set_menu_pressed_visual(lv_obj_t* obj, bool pressed) {
    if (!obj) return;
    lv_color_t color = lv_color_hex(pressed ? EPD_COLOR_BORDER : EPD_COLOR_TEXT);
    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        lv_obj_set_style_text_color(child, color, LV_PART_MAIN);
    }
}

static void on_menu_press_feedback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(e);
    if (!target) return;
    if (code == LV_EVENT_PRESSED) {
        set_menu_pressed_visual(target, true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        set_menu_pressed_visual(target, false);
    }
}

static void set_row_focus_visual(lv_obj_t* obj, bool focused) {
    if (!obj) return;
    lv_color_t bg = lv_color_hex(focused ? EPD_COLOR_FOCUS : EPD_COLOR_BG);
    lv_color_t text = lv_color_hex(focused ? EPD_COLOR_BG : EPD_COLOR_TEXT);
    lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        lv_obj_set_style_text_color(child, text, LV_PART_MAIN);
    }
}

static void on_row_focus_feedback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(e);
    if (!target) return;
    if (code == LV_EVENT_FOCUSED) {
        set_row_focus_visual(target, true);
    } else if (code == LV_EVENT_DEFOCUSED) {
        set_row_focus_visual(target, false);
    }
}

static void set_action_focus_visual(lv_obj_t* obj, bool focused) {
    if (!obj) return;
    lv_color_t bg = lv_color_hex(focused ? EPD_COLOR_FOCUS : EPD_COLOR_BG);
    lv_color_t text = lv_color_hex(focused ? EPD_COLOR_BG : EPD_COLOR_TEXT);
    lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        lv_obj_set_style_text_color(child, text, LV_PART_MAIN);
    }
}

static void on_action_focus_feedback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(e);
    if (!target) return;
    if (code == LV_EVENT_FOCUSED) {
        set_action_focus_visual(target, true);
    } else if (code == LV_EVENT_DEFOCUSED) {
        set_action_focus_visual(target, false);
    }
}
#endif

static lv_obj_t* create_hit_row(lv_obj_t* parent, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* hit = lv_obj_create(parent);
    lv_obj_set_size(hit, lv_pct(100), UI_MENU_ITEM_HEIGHT);
    style_hit_area(hit);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_ext_click_area(hit, UI_EXT_CLICK_LIST);
    ui::port::keyboard_focus_register(hit);
    return hit;
}

static lv_obj_t* create_back_hit_row(lv_obj_t* parent, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* hit = lv_obj_create(parent);
    lv_obj_set_size(hit, LV_SIZE_CONTENT, UI_BACK_BTN_HEIGHT);
    style_hit_area(hit);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) {
        lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, user_data);
        ui::port::keyboard_focus_register(hit);
    } else {
        lv_obj_clear_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_ext_click_area(hit, UI_EXT_CLICK_BACK);
    return hit;
}

static void create_back_content(lv_obj_t* parent, const char* title) {
    lv_obj_t* arrow = lv_label_create(parent);
    lv_obj_set_style_text_font(arrow, UI_FONT_NAV, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_add_flag(arrow, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, UI_FONT_NAV, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(label, title);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static lv_obj_t* create_nav_action_button(lv_obj_t* parent, const char* action_text,
                                          lv_event_cb_t action_cb, void* action_user_data,
                                          lv_obj_t** action_label_out) {
    lv_obj_t* action = lv_obj_create(parent);
    lv_obj_set_size(action, LV_SIZE_CONTENT, UI_ACTION_BTN_H);
    lv_obj_add_style(action, &ui::theme::style_nav_action, LV_PART_MAIN);
    lv_obj_clear_flag(action, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(action, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(action, action_cb, LV_EVENT_CLICKED, action_user_data);
    lv_obj_set_ext_click_area(action, UI_EXT_CLICK_ACTION);
    ui::port::keyboard_focus_register(action);
#ifdef BOARD_TDECK
    lv_obj_add_event_cb(action, on_action_focus_feedback, LV_EVENT_FOCUSED, action);
    lv_obj_add_event_cb(action, on_action_focus_feedback, LV_EVENT_DEFOCUSED, action);
#endif

    lv_obj_t* label = lv_label_create(action);
    lv_obj_set_style_text_font(label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(label, action_text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(label);

    if (action_label_out) {
        *action_label_out = label;
    }
    return action;
}

lv_obj_t* back_button(lv_obj_t* parent, const char* title, lv_event_cb_t cb) {
    ui::screen_mgr::set_nav_title(title);

    lv_obj_t* hit = create_back_hit_row(parent, cb, NULL);

    lv_obj_t* row = lv_obj_create(hit);
    lv_obj_set_size(row, LV_SIZE_CONTENT, UI_BACK_BTN_HEIGHT);
    lv_obj_align(row, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, UI_NAV_PAD_LEFT, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, UI_NAV_PAD_LEFT, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, UI_BACK_BTN_PAD_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, UI_BACK_BTN_PAD_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, UI_BACK_BTN_COL_PAD, LV_PART_MAIN);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#ifdef BOARD_TDECK
    lv_obj_add_event_cb(hit, on_back_press_feedback, LV_EVENT_PRESSED, row);
    lv_obj_add_event_cb(hit, on_back_press_feedback, LV_EVENT_RELEASED, row);
    lv_obj_add_event_cb(hit, on_back_press_feedback, LV_EVENT_PRESS_LOST, row);
    lv_obj_add_event_cb(hit, on_back_focus_feedback, LV_EVENT_FOCUSED, row);
    lv_obj_add_event_cb(hit, on_back_focus_feedback, LV_EVENT_DEFOCUSED, row);
#endif

    create_back_content(row, ui::screen_mgr::previous_nav_title(title));

    return hit;
}

lv_obj_t* back_button_action_ex(lv_obj_t* parent, const char* title, lv_event_cb_t back_cb,
                                const char* action_text, lv_event_cb_t action_cb, void* action_user_data,
                                lv_obj_t** action_label_out) {
    return back_button_actions_ex(parent, title, back_cb,
                                  NULL, NULL, NULL,
                                  action_text, action_cb, action_user_data,
                                  NULL, action_label_out);
}

lv_obj_t* back_button_actions_ex(lv_obj_t* parent, const char* title, lv_event_cb_t back_cb,
                                 const char* first_action_text, lv_event_cb_t first_action_cb, void* first_action_user_data,
                                 const char* second_action_text, lv_event_cb_t second_action_cb, void* second_action_user_data,
                                 lv_obj_t** first_action_label_out, lv_obj_t** second_action_label_out) {
    return back_button_three_actions_ex(parent, title, back_cb,
                                        first_action_text, first_action_cb, first_action_user_data,
                                        second_action_text, second_action_cb, second_action_user_data,
                                        NULL, NULL, NULL,
                                        first_action_label_out, second_action_label_out, NULL);
}

lv_obj_t* back_button_three_actions_ex(lv_obj_t* parent, const char* title, lv_event_cb_t back_cb,
                                       const char* first_action_text, lv_event_cb_t first_action_cb, void* first_action_user_data,
                                       const char* second_action_text, lv_event_cb_t second_action_cb, void* second_action_user_data,
                                       const char* third_action_text, lv_event_cb_t third_action_cb, void* third_action_user_data,
                                       lv_obj_t** first_action_label_out, lv_obj_t** second_action_label_out,
                                       lv_obj_t** third_action_label_out) {
    ui::screen_mgr::set_nav_title(title);

    lv_obj_t* row = create_back_hit_row(parent, NULL, NULL);
    lv_obj_set_size(row, lv_pct(UI_OUTER_WIDTH_PCT), UI_BACK_BTN_HEIGHT);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

    lv_obj_t* back = lv_obj_create(row);
    lv_obj_set_size(back, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(back, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(back, UI_NAV_PAD_LEFT, LV_PART_MAIN);
    lv_obj_set_style_pad_right(back, UI_NAV_PAD_LEFT, LV_PART_MAIN);
    lv_obj_set_style_pad_top(back, UI_BACK_BTN_PAD_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(back, UI_BACK_BTN_PAD_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_column(back, UI_BACK_BTN_COL_PAD, LV_PART_MAIN);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(back, UI_EXT_CLICK_BACK);
    ui::port::keyboard_focus_register(back);
    lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#ifdef BOARD_TDECK
    lv_obj_add_event_cb(back, on_back_press_feedback, LV_EVENT_PRESSED, back);
    lv_obj_add_event_cb(back, on_back_press_feedback, LV_EVENT_RELEASED, back);
    lv_obj_add_event_cb(back, on_back_press_feedback, LV_EVENT_PRESS_LOST, back);
    lv_obj_add_event_cb(back, on_back_focus_feedback, LV_EVENT_FOCUSED, back);
    lv_obj_add_event_cb(back, on_back_focus_feedback, LV_EVENT_DEFOCUSED, back);
#endif

    create_back_content(back, ui::screen_mgr::previous_nav_title(title));

    lv_obj_t* actions = lv_obj_create(row);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_align(actions, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(actions, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(actions, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(actions, UI_BACK_BTN_PAD_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(actions, UI_BACK_BTN_PAD_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_column(actions, 4, LV_PART_MAIN);
    lv_obj_clear_flag(actions, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (first_action_text && first_action_cb) {
        create_nav_action_button(actions, first_action_text, first_action_cb, first_action_user_data,
                                 first_action_label_out);
    } else if (first_action_label_out) {
        *first_action_label_out = NULL;
    }
    if (second_action_text && second_action_cb) {
        create_nav_action_button(actions, second_action_text, second_action_cb, second_action_user_data,
                                 second_action_label_out);
    } else if (second_action_label_out) {
        *second_action_label_out = NULL;
    }
    if (third_action_text && third_action_cb) {
        create_nav_action_button(actions, third_action_text, third_action_cb, third_action_user_data,
                                 third_action_label_out);
    } else if (third_action_label_out) {
        *third_action_label_out = NULL;
    }
    return row;
}

lv_obj_t* back_button_action(lv_obj_t* parent, const char* title, lv_event_cb_t back_cb,
                             const char* action_text, lv_event_cb_t action_cb, void* action_user_data) {
    lv_obj_t* label = NULL;
    back_button_action_ex(parent, title, back_cb, action_text, action_cb, action_user_data, &label);
    return label;
}

lv_obj_t* menu_item(lv_obj_t* parent, const void* icon_src, const char* label_text, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* hit = create_hit_row(parent, cb, user_data);

    lv_obj_t* cont = lv_obj_create(hit);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_add_style(cont, &ui::theme::style_menu_row, LV_PART_MAIN);
    lv_obj_clear_flag(cont, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(cont, LV_OBJ_FLAG_EVENT_BUBBLE);
#ifdef BOARD_TDECK
    lv_obj_add_event_cb(hit, on_menu_press_feedback, LV_EVENT_PRESSED, cont);
    lv_obj_add_event_cb(hit, on_menu_press_feedback, LV_EVENT_RELEASED, cont);
    lv_obj_add_event_cb(hit, on_menu_press_feedback, LV_EVENT_PRESS_LOST, cont);
    lv_obj_add_event_cb(hit, on_row_focus_feedback, LV_EVENT_FOCUSED, cont);
    lv_obj_add_event_cb(hit, on_row_focus_feedback, LV_EVENT_DEFOCUSED, cont);
#endif

    (void)icon_src;  // unused — all callers pass NULL

    lv_obj_t* lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(lbl, label_text);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, UI_MENU_ITEM_INSET, 0);

    lv_obj_t* arrow = lv_label_create(cont);
    lv_obj_set_style_text_font(arrow, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -UI_MENU_ITEM_INSET, 0);

    return hit;
}

lv_obj_t* toggle_item(lv_obj_t* parent, const char* label_text, const char* value, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* hit = create_hit_row(parent, cb, user_data);

    lv_obj_t* cont = lv_obj_create(hit);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_add_style(cont, &ui::theme::style_menu_row, LV_PART_MAIN);
    lv_obj_clear_flag(cont, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(cont, LV_OBJ_FLAG_EVENT_BUBBLE);
#ifdef BOARD_TDECK
    lv_obj_add_event_cb(hit, on_menu_press_feedback, LV_EVENT_PRESSED, cont);
    lv_obj_add_event_cb(hit, on_menu_press_feedback, LV_EVENT_RELEASED, cont);
    lv_obj_add_event_cb(hit, on_menu_press_feedback, LV_EVENT_PRESS_LOST, cont);
    lv_obj_add_event_cb(hit, on_row_focus_feedback, LV_EVENT_FOCUSED, cont);
    lv_obj_add_event_cb(hit, on_row_focus_feedback, LV_EVENT_DEFOCUSED, cont);
#endif

    lv_obj_t* lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(lbl, label_text);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, UI_MENU_ITEM_INSET, 0);

    lv_obj_t* val = lv_label_create(cont);
    lv_obj_set_style_text_font(val, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(val, value);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -UI_MENU_ITEM_INSET, 0);

    return val;  // return the value label so caller can update it
}

lv_obj_t* text_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_size(btn, lv_pct(85), UI_TEXT_BTN_HEIGHT);
    lv_obj_add_style(btn, &ui::theme::style_text_button, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_ext_click_area(btn, UI_EXT_CLICK_ACTION);
    ui::port::keyboard_focus_register(btn);
#ifdef BOARD_TDECK
    lv_obj_add_event_cb(btn, on_row_focus_feedback, LV_EVENT_FOCUSED, btn);
    lv_obj_add_event_cb(btn, on_row_focus_feedback, LV_EVENT_DEFOCUSED, btn);
#endif

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_PROMPT_TXT), LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t* content_area(lv_obj_t* parent) {
    lv_obj_t* area = lv_obj_create(parent);
    lv_obj_set_size(area, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(area, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    return area;
}

lv_obj_t* scroll_list(lv_obj_t* parent) {
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(list, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    ui::theme::style_scrollbar_hint(list);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    // Disable elastic bounce and scroll momentum — bad on e-ink
    lv_obj_clear_flag(list, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM));
    return list;
}

bool hit_area_debug_enabled() {
    ensure_hit_area_debug_loaded();
    return hit_area_debug_state;
}

void set_hit_area_debug(bool enabled) {
    hit_area_debug_state = enabled;
    hit_area_debug_loaded = true;
}

} // namespace ui::nav
