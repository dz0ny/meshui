#pragma once

#ifndef BOARD_WIO_L1
#include "lvgl.h"
#endif
#include "kit/ui_kit.h"

// Screen lifecycle callbacks — each screen namespace provides one of these.
// create() receives a ui::kit::Handle (the screen's root container) rather than
// a raw lv_obj_t*, so screen code can be written against the backend-agnostic
// ui::kit facade. Under the LVGL backend the Handle is literally the lv_obj_t*.
struct screen_lifecycle_t {
    void (*create)(ui::kit::Handle parent);
    void (*entry)(void);
    void (*exit)(void);
    void (*destroy)(void);
};

namespace ui::screen_mgr {

void init();
bool register_screen(int id, screen_lifecycle_t* life);
bool switch_to(int id, bool anim);
bool push(int id, bool anim);
bool pop(bool anim);
void reload_stack();
void set_nav_title(const char* title);
const char* previous_nav_title(const char* fallback);
int  top_id();
#ifndef BOARD_WIO_L1
lv_obj_t* set_nav_action(const char* action_text, lv_event_cb_t action_cb, void* action_user_data);
lv_obj_t* set_nav_actions(const char* first_action_text, lv_event_cb_t first_action_cb, void* first_action_user_data,
                          const char* second_action_text, lv_event_cb_t second_action_cb, void* second_action_user_data);
lv_obj_t* top_obj();
#endif

} // namespace ui::screen_mgr
