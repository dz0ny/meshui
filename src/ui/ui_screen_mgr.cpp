#include "ui_screen_mgr.h"
#include "ui_port.h"
#include "ui_theme.h"
#include "components/nav_button.h"
#include <cstring>

// Ported from factory scr_mrg.cpp — kept as C-style linked list, works well.

namespace ui::screen_mgr {

enum card_state {
    STATE_IDLE = 0,
    STATE_DESTROYED,
    STATE_CREATED,
    STATE_INACTIVE,
    STATE_ACTIVE_BG,
    STATE_ACTIVE,
};

struct card_t {
    int id;
    lv_obj_t* obj;
    lv_obj_t* content_obj;
    lv_obj_t* nav_obj;
    lv_obj_t* nav_action_label;
    lv_obj_t* nav_action_label_2;
    card_state st;
    screen_lifecycle_t* life;
    char nav_title[32];
    char nav_action_text[24];
    char nav_action_text_2[24];
    lv_event_cb_t nav_action_cb;
    lv_event_cb_t nav_action_cb_2;
    void* nav_action_user_data;
    void* nav_action_user_data_2;
    bool nav_action_enabled;
    bool nav_action_enabled_2;
    card_t* next;
    card_t* prev;
};

static card_t* head = NULL;
static card_t* top_card = NULL;
static card_t* stack_root = NULL;
static card_t* stack_top = NULL;
static card_t* creating_card = NULL;

// No animations on e-paper — partial redraws cause visual artifacts
static lv_screen_load_anim_t anim_sw   = LV_SCR_LOAD_ANIM_NONE;
static lv_screen_load_anim_t anim_push = LV_SCR_LOAD_ANIM_NONE;
static lv_screen_load_anim_t anim_pop  = LV_SCR_LOAD_ANIM_NONE;
static uint32_t anim_time = 0;

// ---------- Internal ----------

static const char* default_nav_title(int id) {
    switch (id) {
        case SCREEN_HOME: return "Home";
        case SCREEN_CONTACTS: return "Contacts";
        case SCREEN_CHAT: return "Messages";
        case SCREEN_SETTINGS: return "Settings";
        case SCREEN_GPS: return "GPS";
        case SCREEN_BATTERY: return "Battery";
        case SCREEN_MESH: return "Mesh";
        case SCREEN_STATUS: return "Status";
        case SCREEN_SET_DISPLAY: return "Display";
        case SCREEN_SET_GPS: return "GPS Settings";
        case SCREEN_SET_MESH: return "Mesh Config";
        case SCREEN_DISCOVERY: return "Discovery";
        case SCREEN_LOCK: return "Lock";
        case SCREEN_CONTACT_DETAIL: return "Contact";
        case SCREEN_MSG_DETAIL: return "Message";
        case SCREEN_SET_BLE: return "Bluetooth";
        case SCREEN_SET_STORAGE: return "Storage";
        case SCREEN_COMPOSE: return "Compose";
        case SCREEN_MAP: return "Map";
        case SCREEN_SENSORS: return "Sensors";
        case SCREEN_PING: return "Ping";
        case SCREEN_SETTINGS_DEBUG: return "Debug";
        case SCREEN_SETTINGS_DEVICE: return "Device";
        case SCREEN_TRAIL: return "Trail";
        case SCREEN_TEAM: return "Team";
        case SCREEN_COMPASS: return "Compass";
        case SCREEN_WAYPOINTS: return "Waypoints";
        case SCREEN_WAYPOINT_DETAIL: return "Waypoint";
        default: return "";
    }
}

static bool screen_has_nav(int id) {
    return id != SCREEN_HOME && id != SCREEN_LOCK && id != SCREEN_TOUCH_DEBUG;
}

static void on_default_back(lv_event_t* e) {
    (void)e;
    pop(true);
}

static void rebuild_nav(card_t* card) {
    if (!card || !card->obj) return;

    if (card->nav_obj) {
        lv_obj_delete(card->nav_obj);
        card->nav_obj = NULL;
        card->nav_action_label = NULL;
        card->nav_action_label_2 = NULL;
    }

    if (!screen_has_nav(card->id)) return;

    const char* title = card->nav_title[0] ? card->nav_title : default_nav_title(card->id);
    if (card->nav_action_enabled || card->nav_action_enabled_2) {
        card->nav_obj = ui::nav::back_button_actions_ex(
            card->obj, title, on_default_back,
            card->nav_action_enabled ? card->nav_action_text : NULL,
            card->nav_action_enabled ? card->nav_action_cb : NULL,
            card->nav_action_enabled ? card->nav_action_user_data : NULL,
            card->nav_action_enabled_2 ? card->nav_action_text_2 : NULL,
            card->nav_action_enabled_2 ? card->nav_action_cb_2 : NULL,
            card->nav_action_enabled_2 ? card->nav_action_user_data_2 : NULL,
            &card->nav_action_label, &card->nav_action_label_2);
    } else {
        card->nav_obj = ui::nav::back_button(card->obj, title, on_default_back);
        card->nav_action_label = NULL;
        card->nav_action_label_2 = NULL;
    }
    if (card->nav_obj) {
        lv_obj_move_to_index(card->nav_obj, 0);
    }
}

static lv_obj_t* create_default_screen(card_t* card) {
    lv_obj_t* obj = lv_obj_create(NULL);
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(obj, UI_OUTER_MARGIN_X, LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj, UI_OUTER_MARGIN_X, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj, UI_OUTER_MARGIN_X, LV_PART_MAIN);
    if (screen_has_nav(card->id)) {
        lv_obj_set_style_pad_top(obj, UI_STATUSBAR_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_pad_row(obj, UI_NAV_CONTENT_GAP, LV_PART_MAIN);
    } else {
        lv_obj_set_style_pad_top(obj, UI_OUTER_MARGIN_X, LV_PART_MAIN);
    }
    lv_obj_clear_flag(obj, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM));
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    card->obj = obj;
    card->content_obj = NULL;
    card->nav_obj = NULL;
    card->nav_action_label = NULL;
    card->nav_action_label_2 = NULL;
    card->nav_title[0] = 0;
    card->nav_action_text[0] = 0;
    card->nav_action_text_2[0] = 0;
    card->nav_action_cb = NULL;
    card->nav_action_cb_2 = NULL;
    card->nav_action_user_data = NULL;
    card->nav_action_user_data_2 = NULL;
    card->nav_action_enabled = false;
    card->nav_action_enabled_2 = false;

    // Content container — fills remaining space below nav via flex_grow
    lv_obj_t* content = lv_obj_create(obj);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM));
    card->content_obj = content;

    // Defer nav build until after screen create() — if create() calls set_nav_actions,
    // rebuild_nav runs from there; otherwise we build the default back-only nav below.
    // This avoids the double-rebuild when screens add action buttons.
    creating_card = card;
    card->life->create((ui::kit::Handle)content);
    creating_card = NULL;

    if (!card->nav_obj) {
        rebuild_nav(card);
    }
    return obj;
}

static card_t* find_by_id(int id) {
    card_t* p = head->next;
    while (p) {
        if (p->id == id) return p;
        p = p->next;
    }
    return NULL;
}

static void activate(card_t* card) {
    if (card->st == STATE_DESTROYED) {
        card->obj = create_default_screen(card);
        card->life->entry();
        card->st = STATE_ACTIVE;
    } else if (card->st == STATE_CREATED || card->st == STATE_INACTIVE) {
        card->life->entry();
        card->st = STATE_ACTIVE;
    }
}

static void deactivate(card_t* card) {
    if (card->st > STATE_INACTIVE) {
        card->life->exit();
        card->st = STATE_INACTIVE;
    }
}

static void remove_card(card_t* card) {
    if (card->st > STATE_INACTIVE) {
        card->life->exit();
        card->life->destroy();
        card->st = STATE_IDLE;
    } else if (card->st > STATE_DESTROYED) {
        card->life->destroy();
        card->st = STATE_IDLE;
    }
}

// ---------- Public API ----------

void init() {
    head = (card_t*)lv_malloc(sizeof(card_t));
    head->id = -1;
    head->obj = NULL;
    head->content_obj = NULL;
    head->nav_obj = NULL;
    head->nav_action_label = NULL;
    head->nav_action_label_2 = NULL;
    head->st = (card_state)-1;
    head->life = NULL;
    head->nav_title[0] = 0;
    head->nav_action_text[0] = 0;
    head->nav_action_text_2[0] = 0;
    head->nav_action_cb = NULL;
    head->nav_action_cb_2 = NULL;
    head->nav_action_user_data = NULL;
    head->nav_action_user_data_2 = NULL;
    head->nav_action_enabled = false;
    head->nav_action_enabled_2 = false;
    head->next = NULL;
    head->prev = NULL;
    top_card = head;
    stack_root = NULL;
    stack_top = NULL;
}

bool register_screen(int id, screen_lifecycle_t* life) {
    if (find_by_id(id)) return false;

    card_t* c = (card_t*)lv_malloc(sizeof(card_t));
    c->id = id;
    c->obj = NULL;
    c->content_obj = NULL;
    c->nav_obj = NULL;
    c->nav_action_label = NULL;
    c->nav_action_label_2 = NULL;
    c->st = STATE_IDLE;
    c->life = life;
    c->nav_title[0] = 0;
    c->nav_action_text[0] = 0;
    c->nav_action_text_2[0] = 0;
    c->nav_action_cb = NULL;
    c->nav_action_cb_2 = NULL;
    c->nav_action_user_data = NULL;
    c->nav_action_user_data_2 = NULL;
    c->nav_action_enabled = false;
    c->nav_action_enabled_2 = false;
    c->next = NULL;
    c->prev = top_card;
    top_card->next = c;
    top_card = c;
    return true;
}

bool switch_to(int id, bool anim) {
    card_t* tgt = find_by_id(id);
    if (!tgt) return false;

    lv_obj_t* curr_obj = NULL;
    card_t* s = NULL;

    // Clear stack
    if (stack_top) {
        curr_obj = stack_top->obj;
        s = stack_top->prev;
        remove_card(stack_top);
        lv_free(stack_top);
        stack_top = s;
    }
    while (stack_top) {
        s = stack_top->prev;
        remove_card(stack_top);
        lv_free(stack_top);
        stack_top = s;
    }

    // Create new stack entry
    s = (card_t*)lv_malloc(sizeof(card_t));
    s->id = tgt->id;
    s->obj = NULL;
    s->content_obj = NULL;
    s->nav_obj = NULL;
    s->nav_action_label = NULL;
    s->nav_action_label_2 = NULL;
    s->st = STATE_IDLE;
    s->life = tgt->life;
    s->nav_title[0] = 0;
    s->nav_action_text[0] = 0;
    s->nav_action_text_2[0] = 0;
    s->nav_action_cb = NULL;
    s->nav_action_cb_2 = NULL;
    s->nav_action_user_data = NULL;
    s->nav_action_user_data_2 = NULL;
    s->nav_action_enabled = false;
    s->nav_action_enabled_2 = false;
    s->prev = NULL;
    s->next = NULL;
    s->obj = create_default_screen(s);
    s->st = STATE_CREATED;
    stack_root = s;
    stack_top = s;

    activate(s);

    if (anim_sw != LV_SCR_LOAD_ANIM_NONE && anim) {
        lv_screen_load_anim(s->obj, anim_sw, anim_time, 0, true);
    } else {
        lv_screen_load(s->obj);
        if (curr_obj) lv_obj_delete(curr_obj);
    }
    ui::port::keyboard_focus_invalidate();
    return true;
}

bool push(int id, bool anim) {
    card_t* tgt = find_by_id(id);
    if (!tgt) return false;
    if (stack_top && tgt->id == stack_top->id) return false;

    card_t* s = (card_t*)lv_malloc(sizeof(card_t));
    s->id = tgt->id;
    s->obj = NULL;
    s->content_obj = NULL;
    s->nav_obj = NULL;
    s->nav_action_label = NULL;
    s->nav_action_label_2 = NULL;
    s->st = STATE_IDLE;
    s->life = tgt->life;
    s->nav_title[0] = 0;
    s->nav_action_text[0] = 0;
    s->nav_action_text_2[0] = 0;
    s->nav_action_cb = NULL;
    s->nav_action_cb_2 = NULL;
    s->nav_action_user_data = NULL;
    s->nav_action_user_data_2 = NULL;
    s->nav_action_enabled = false;
    s->nav_action_enabled_2 = false;
    s->obj = create_default_screen(s);
    s->st = STATE_CREATED;

    if (!stack_top) {
        s->prev = NULL;
        s->next = NULL;
        stack_root = s;
        stack_top = s;
    } else {
        deactivate(stack_top);
        stack_top->next = s;
        s->prev = stack_top;
        s->next = NULL;
        stack_top = s;
    }

    activate(s);

    if (anim_push != LV_SCR_LOAD_ANIM_NONE && anim) {
        lv_screen_load_anim(s->obj, anim_push, anim_time, 0, false);
    } else {
        lv_screen_load(s->obj);
    }
    ui::port::keyboard_focus_invalidate();
    return true;
}

bool pop(bool anim) {
    if (!stack_top || stack_top == stack_root) return false;

    lv_obj_t* cur_obj = stack_top->obj;
    card_t* dst = stack_top->prev;
    remove_card(stack_top);
    lv_free(stack_top);
    stack_top = dst;

    activate(dst);

    if (anim_pop != LV_SCR_LOAD_ANIM_NONE && anim) {
        lv_screen_load_anim(dst->obj, anim_pop, anim_time, 0, true);
    } else {
        lv_screen_load(dst->obj);
        if (cur_obj) lv_obj_delete(cur_obj);
    }
    ui::port::keyboard_focus_invalidate();
    return true;
}

void reload_stack() {
    if (!stack_top) return;

    for (card_t* card = stack_root; card; card = card->next) {
        lv_obj_t* old_obj = card->obj;

        if (card->st > STATE_INACTIVE) {
            card->life->exit();
            card->life->destroy();
        } else if (card->st > STATE_DESTROYED) {
            card->life->destroy();
        }

        if (card == stack_top) {
            card->obj = create_default_screen(card);
            card->st = STATE_CREATED;
        } else {
            card->obj = NULL;
            card->st = STATE_DESTROYED;
        }

        if (old_obj) {
            lv_obj_delete(old_obj);
        }
    }

    activate(stack_top);
    lv_screen_load(stack_top->obj);
    ui::port::keyboard_focus_invalidate();
}

lv_obj_t* set_nav_action(const char* action_text, lv_event_cb_t action_cb, void* action_user_data) {
    return set_nav_actions(NULL, NULL, NULL, action_text, action_cb, action_user_data);
}

lv_obj_t* set_nav_actions(const char* first_action_text, lv_event_cb_t first_action_cb, void* first_action_user_data,
                          const char* second_action_text, lv_event_cb_t second_action_cb, void* second_action_user_data) {
    card_t* card = creating_card ? creating_card : stack_top;
    if (!card) return NULL;

    if (!first_action_text || !first_action_cb) {
        card->nav_action_enabled = false;
        card->nav_action_text[0] = 0;
        card->nav_action_cb = NULL;
        card->nav_action_user_data = NULL;
    } else {
        card->nav_action_enabled = true;
        strncpy(card->nav_action_text, first_action_text, sizeof(card->nav_action_text) - 1);
        card->nav_action_text[sizeof(card->nav_action_text) - 1] = 0;
        card->nav_action_cb = first_action_cb;
        card->nav_action_user_data = first_action_user_data;
    }

    if (!second_action_text || !second_action_cb) {
        card->nav_action_enabled_2 = false;
        card->nav_action_text_2[0] = 0;
        card->nav_action_cb_2 = NULL;
        card->nav_action_user_data_2 = NULL;
    } else {
        card->nav_action_enabled_2 = true;
        strncpy(card->nav_action_text_2, second_action_text, sizeof(card->nav_action_text_2) - 1);
        card->nav_action_text_2[sizeof(card->nav_action_text_2) - 1] = 0;
        card->nav_action_cb_2 = second_action_cb;
        card->nav_action_user_data_2 = second_action_user_data;
    }

    if (card->obj) {
        rebuild_nav(card);
    }
    return card->nav_action_label;
}

void set_nav_title(const char* title) {
    if (!stack_top || !title) return;

    strncpy(stack_top->nav_title, title, sizeof(stack_top->nav_title) - 1);
    stack_top->nav_title[sizeof(stack_top->nav_title) - 1] = 0;
}

const char* previous_nav_title(const char* fallback) {
    if (!stack_top || !stack_top->prev) return fallback ? fallback : "";

    if (stack_top->prev->nav_title[0] != 0) return stack_top->prev->nav_title;

    return default_nav_title(stack_top->prev->id);
}

int top_id() {
    return stack_top ? stack_top->id : -1;
}

lv_obj_t* top_obj() {
    return stack_top ? stack_top->obj : NULL;
}

} // namespace ui::screen_mgr
