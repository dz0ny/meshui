// Mono implementation of ui::screen_mgr — drives the ui_kit_mono engine via the
// same screen_lifecycle_t the LVGL backend uses. Lets the real ported screen
// .cpp files (status/settings/gps/...) run unchanged on the nRF52 tracker.
#ifdef BOARD_WIO_L1

#include "ui_screen_mgr.h"
#include "kit/ui_kit.h"
#include "kit/ui_kit_mono.h"

namespace ui::screen_mgr {

static const int MAX_ID = 37;
static screen_lifecycle_t* g_reg[MAX_ID] = {};

static const int MAX_STACK = 8;
static int g_stack[MAX_STACK];
static int g_depth = 0;
static screen_lifecycle_t* g_cur = nullptr;
static int g_cur_id = -1;

// Build (or rebuild) the current screen into a fresh mono tree and draw it.
static void build_current() {
    ui::kit::mono::reset();
    if (g_cur && g_cur->create) g_cur->create(ui::kit::screen_root());
    if (g_cur && g_cur->entry)  g_cur->entry();
    ui::kit::mono::render();
}

void init() {}

bool register_screen(int id, screen_lifecycle_t* life) {
    if (id < 0 || id >= MAX_ID) return false;
    g_reg[id] = life;
    return true;
}

bool switch_to(int id, bool) {
    if (id < 0 || id >= MAX_ID || !g_reg[id]) return false;
    if (g_cur && g_cur->exit)    g_cur->exit();
    if (g_cur && g_cur->destroy) g_cur->destroy();
    g_depth = 0;
    g_cur = g_reg[id]; g_cur_id = id;
    build_current();
    return true;
}

bool push(int id, bool) {
    if (id < 0 || id >= MAX_ID || !g_reg[id]) return false;   // unregistered → ignore
    // Stop the outgoing screen (halts its timers) but keep it on the stack to
    // return to; its widgets are rebuilt by create() when we pop back.
    if (g_cur && g_cur->exit) g_cur->exit();
    if (g_cur && g_depth < MAX_STACK) g_stack[g_depth++] = g_cur_id;
    g_cur = g_reg[id]; g_cur_id = id;
    build_current();
    return true;
}

bool pop(bool) {
    if (g_depth == 0) return false;
    if (g_cur && g_cur->exit)    g_cur->exit();
    if (g_cur && g_cur->destroy) g_cur->destroy();
    int id = g_stack[--g_depth];
    g_cur = g_reg[id]; g_cur_id = id;
    build_current();
    return true;
}

void reload_stack() { build_current(); }
void set_nav_title(const char*) {}                       // mono draws its own title
const char* previous_nav_title(const char* fb) { return fb; }
int  top_id() { return g_cur_id; }

} // namespace ui::screen_mgr
#endif // BOARD_WIO_L1
