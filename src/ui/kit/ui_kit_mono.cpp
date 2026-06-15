// ui::kit mono backend — a tiny retained-mode UI engine that draws 1-bit
// straight to GxEPD2 on the nRF52 Wio Tracker L1. No LVGL.
//
// Model: a fixed pool of nodes forming a tree. Containers lay their children out
// (vertical stack, horizontal space-between, or absolute), labels measure/draw
// text via the built-in Adafruit-GFX font. render() does layout + a paged draw.
// A single focus cursor moves over clickable rows (joystick up/down + enter).
#ifdef BOARD_WIO_L1

#include "ui_kit.h"
#include "ui_kit_mono.h"
#include "../../board_wio.h"
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ui::kit {

// ---- size tokens -----------------------------------------------------------
const int CONTENT = -100000;
int pct(int v) { return -(v) - 1; }                 // pct(0)=-1 .. pct(100)=-101
static bool is_pct(int v) { return v <= -1 && v > CONTENT; }
static int  pct_of(int v) { return -(v) - 1; }

// ---- node model ------------------------------------------------------------
enum Kind : uint8_t { K_CONT, K_LABEL, K_CANVAS };
enum Lay  : uint8_t { L_NONE, L_COL, L_ROW_SPACE, L_ROW };

struct Node {
    Kind kind;
    Lay  lay;
    Node* parent;
    Node* first_child;
    Node* next_sib;

    int   w_spec, h_spec;     // px | pct(n) | CONTENT
    int   x, y, w, h;         // resolved (px)
    int   pad, gap, growf;
    Align align; int dx, dy;  // for absolute (free_layout) children

    bool  hidden, clickable, focusable_, card, center;
    Font  font;
    char  text[40];
    Cb    cb; void* user;

    // canvas
    uint8_t* cbuf; int cw, ch;
};

static const int POOL = 96;
static Node  g_pool[POOL];
static int   g_used = 0;
static Node* g_root = nullptr;
static Node* g_focus = nullptr;
static bool  g_dirty = true;     // redraw only when something actually changed
static int   g_scroll = 0;       // vertical scroll offset (px) for tall screens
static bool  g_invert = false;   // dark mode: swap fg/bg so the panel is white-on-black

// Current fg/bg GxEPD colours. Every draw goes through these so flipping
// g_invert repaints the whole UI in the opposite scheme.
static inline uint16_t cfg() { return g_invert ? GxEPD_WHITE : GxEPD_BLACK; }
static inline uint16_t cbg() { return g_invert ? GxEPD_BLACK : GxEPD_WHITE; }

// Fixed top status bar (clock / GPS / battery), drawn by the app callback.
static int          g_sb_h = 0;
static mono::StatusbarFn g_sb_fn = nullptr;
static int header_h() { return g_sb_fn ? g_sb_h : 0; }

static Node* alloc(Kind k) {
    if (g_used >= POOL) return &g_pool[POOL - 1];   // clamp; never crash
    Node* n = &g_pool[g_used++];
    memset(n, 0, sizeof(Node));
    n->kind = k; n->lay = L_NONE;
    n->w_spec = CONTENT; n->h_spec = CONTENT;
    n->font = Font::Body;
    return n;
}
static Node* N(Handle h) { return (Node*)h; }

static void add_child(Node* parent, Node* child) {
    if (!parent) return;
    child->parent = parent;
    if (!parent->first_child) { parent->first_child = child; return; }
    Node* c = parent->first_child;
    while (c->next_sib) c = c->next_sib;
    c->next_sib = child;
}

// ---- font metrics (built-in 6x8 GFX font, integer-scaled) ------------------
static int fsize(Font f) {
    switch (f) { case Font::Title: return 2; case Font::ClockLg: return 3; default: return 1; }
}
static int char_w(Font f) { return 6 * fsize(f); }
static int line_h(Font f) { return 8 * fsize(f) + 2; }
static int text_w(const char* s, Font f) {
    int maxc = 0, c = 0;
    for (const char* p = s; *p; p++) { if (*p == '\n') { if (c > maxc) maxc = c; c = 0; } else c++; }
    if (c > maxc) maxc = c;
    return maxc * char_w(f);
}
static int text_lines(const char* s) { int n = 1; for (const char* p = s; *p; p++) if (*p == '\n') n++; return n; }
static int text_h(const char* s, Font f) { return text_lines(s) * line_h(f); }

// ---- facade: tree / layout -------------------------------------------------
Handle screen_root() { return (Handle)g_root; }

Handle panel(Handle parent)  { Node* n = alloc(K_CONT); n->lay = L_NONE;      add_child(N(parent), n); return (Handle)n; }
Handle column(Handle parent) { Node* n = alloc(K_CONT); n->lay = L_COL;       add_child(N(parent), n); return (Handle)n; }
Handle row(Handle parent)    { Node* n = alloc(K_CONT); n->lay = L_ROW_SPACE; add_child(N(parent), n); return (Handle)n; }
Handle list(Handle parent)   { Node* n = alloc(K_CONT); n->lay = L_COL; n->w_spec = pct(100); add_child(N(parent), n); return (Handle)n; }
Handle content_area(Handle parent) { Node* n = alloc(K_CONT); n->lay = L_COL; n->w_spec = pct(100); n->h_spec = pct(100); add_child(N(parent), n); return (Handle)n; }

void size(Handle h, int w, int hgt) { N(h)->w_spec = w; N(h)->h_spec = hgt; }
int  px_width(Handle h)  { return N(h)->w > 0 ? N(h)->w : display.width(); }
// Fallback (node not laid out yet) excludes the status bar so canvases sized off
// this fit the visible viewport rather than overflowing the bottom edge.
int  px_height(Handle h) { return N(h)->h > 0 ? N(h)->h : (display.height() - header_h()); }
void pos(Handle h, int x, int y) { N(h)->dx = x; N(h)->dy = y; N(h)->align = Align::TopLeft; }
void pad(Handle h, int all)      { N(h)->pad = all; }
void gap(Handle h, int between)  { N(h)->gap = between; }
void grow(Handle h, int factor)  { N(h)->growf = factor; }
void scrollable(Handle, bool)    {}   // mono: lists just clip to screen
void hidden(Handle h, bool on)   { if (N(h)->hidden != on) { N(h)->hidden = on; g_dirty = true; } }
void align(Handle h, Align a, int dx, int dy) { N(h)->align = a; N(h)->dx = dx; N(h)->dy = dy; }

// ---- facade: widgets -------------------------------------------------------
Handle label(Handle parent, const char* text) {
    Node* n = alloc(K_LABEL);
    if (text) { strncpy(n->text, text, sizeof(n->text) - 1); }
    add_child(N(parent), n);
    return (Handle)n;
}
void set_text(Handle h, const char* text) {
    if (!h) return;
    const char* t = text ? text : "";
    if (strncmp(N(h)->text, t, sizeof(N(h)->text) - 1) == 0) return;   // unchanged → no redraw
    strncpy(N(h)->text, t, sizeof(N(h)->text) - 1);
    N(h)->text[sizeof(N(h)->text) - 1] = 0;
    g_dirty = true;
}
void set_textf(Handle h, const char* fmt, ...) {
    char buf[40];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    set_text(h, buf);
}

Handle button(Handle parent, const char* text, Cb cb, void* user) {
    Node* n = alloc(K_CONT); n->lay = L_ROW; n->clickable = n->focusable_ = n->card = n->center = true;
    n->cb = cb; n->user = user; n->pad = 2;
    add_child(N(parent), n);
    label((Handle)n, text);
    return (Handle)n;
}
void button_text(Handle btn, const char* text) {
    if (btn && N(btn)->first_child) set_text((Handle)N(btn)->first_child, text);
}

Handle menu_row(Handle parent, const char* text, Cb cb, void* user) {
    Node* n = alloc(K_CONT); n->lay = L_ROW_SPACE; n->clickable = n->focusable_ = true;
    n->cb = cb; n->user = user; n->w_spec = pct(100); n->pad = 3;
    add_child(N(parent), n);
    // Bigger menu text (Title = 2x the body font) — easy to read on the small panel.
    Handle l = label((Handle)n, text);  N(l)->font = Font::Title;
    Handle a = label((Handle)n, ">");    N(a)->font = Font::Title;
    return (Handle)n;
}

Handle toggle_item(Handle parent, const char* label_, const char* value, Cb cb, void* user) {
    Node* n = alloc(K_CONT); n->lay = L_ROW_SPACE; n->w_spec = pct(100); n->pad = 2;
    if (cb) { n->clickable = n->focusable_ = true; n->cb = cb; n->user = user; }
    add_child(N(parent), n);
    // Match menu_row / info_row sizing (Title = 2x body) so settings rows are
    // readable on the small panel.
    Handle lbl = label((Handle)n, label_);            N(lbl)->font = Font::Title;
    Handle val = label((Handle)n, value ? value : ""); N(val)->font = Font::Title;
    return val;
}

Handle info_row(Handle parent, const char* label_) {
    Node* n = alloc(K_CONT); n->lay = L_ROW_SPACE; n->w_spec = pct(100); n->pad = 3;
    add_child(N(parent), n);
    // Bigger, readable data rows (Title = 2x the body font) — scrolling handles
    // the extra height on tall screens like Mesh.
    Handle lbl = label((Handle)n, label_);  N(lbl)->font = Font::Title;
    Handle val = label((Handle)n, "--");    N(val)->font = Font::Title;
    return val;
}
Handle section_header(Handle parent, const char* text) {
    Handle l = label(parent, text);
    N(l)->font = Font::Title;
    return l;
}

void long_mode(Handle, LongMode) {}    // mono labels clip; no marquee

// ---- facade: style ---------------------------------------------------------
void card(Handle h)        { N(h)->card = true; }
void font(Handle h, Font f){ N(h)->font = f; }
void text_color(Handle, Color) {}      // mono is black-on-white only
void text_center(Handle h) { N(h)->center = true; }

// ---- facade: input / misc --------------------------------------------------
void focusable(Handle h)   { N(h)->focusable_ = true; }
void on_click(Handle h, Cb cb, void* user) { N(h)->clickable = N(h)->focusable_ = true; N(h)->cb = cb; N(h)->user = user; }
void free_layout(Handle h) { N(h)->lay = L_NONE; }
void focus_refresh()       {}

// ---- canvas ----------------------------------------------------------------
static uint8_t g_canvas_mem[((250 + 7) / 8) * 130];   // 1bpp scratch, sized for the panel
Handle canvas(Handle parent, int w, int h) {
    Node* n = alloc(K_CANVAS);
    n->cw = w; n->ch = h; n->cbuf = g_canvas_mem;
    n->w_spec = w; n->h_spec = h;
    add_child(N(parent), n);
    return (Handle)n;
}
int canvas_w(Handle c) { return N(c)->cw; }
int canvas_h(Handle c) { return N(c)->ch; }
static int cv_stride(Node* n) { return (n->cw + 7) / 8; }
static void cv_set(Node* n, int x, int y, bool ink) {
    if (!n->cbuf || x < 0 || x >= n->cw || y < 0 || y >= n->ch) return;
    uint8_t* p = &n->cbuf[y * cv_stride(n) + (x >> 3)];
    uint8_t  m = 0x80 >> (x & 7);
    if (ink) *p |= m; else *p &= ~m;    // bit set = ink (black)
}
void canvas_clear(Handle c, Color col) {
    Node* n = N(c); if (!n->cbuf) return;
    memset(n->cbuf, (col == Color::Bg) ? 0x00 : 0xFF, (size_t)cv_stride(n) * n->ch);
}
void canvas_pixel(Handle c, int x, int y, Color col) { cv_set(N(c), x, y, col != Color::Bg); }
void canvas_line(Handle c, int x0, int y0, int x1, int y1, Color col) {
    Node* n = N(c); bool ink = col != Color::Bg;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        cv_set(n, x0, y0, ink);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void canvas_fill_circle(Handle c, int cx, int cy, int r, Color col) {
    Node* n = N(c); bool ink = col != Color::Bg;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) cv_set(n, cx + dx, cy + dy, ink);
}
void canvas_flush(Handle) {}   // drawn during render()

// ---- message list (minimal: a vertical stack of "sender: text" lines) ------
Handle msglist(Handle parent) { Node* n = alloc(K_CONT); n->lay = L_COL; n->w_spec = pct(100); n->pad = 2; add_child(N(parent), n); return (Handle)n; }
void   msg_append(Handle l, const char* sender, const char* text, bool is_self, int) {
    char line[40];
    snprintf(line, sizeof(line), "%s: %s", is_self ? "me" : (sender ? sender : "?"), text ? text : "");
    Handle row_ = label(l, line);
    N(row_)->font = Font::Title;   // larger, readable chat text (2x the 5x7 base)
}
void msg_clear(Handle l) { if (l) N(l)->first_child = nullptr; }
void msg_scroll_bottom(Handle) {}

// ---- timers ----------------------------------------------------------------
struct Tmr { bool used; uint32_t period, next; Cb cb; void* user; };
static const int NTMR = 8;
static Tmr g_tmr[NTMR];
Timer every(uint32_t ms, Cb cb, void* user) {
    for (int i = 0; i < NTMR; i++) if (!g_tmr[i].used) {
        g_tmr[i] = {true, ms, millis() + ms, cb, user};
        return (Timer)&g_tmr[i];
    }
    return nullptr;
}
void stop(Timer t) { if (t) ((Tmr*)t)->used = false; }
void defer(Cb cb, void* user) { if (cb) cb(user); }   // mono: run inline

// ===========================================================================
//  layout
// ===========================================================================
static int resolve(int spec, int avail, const char* /*text*/, Font /*f*/, int content) {
    if (spec == CONTENT) return content;
    if (is_pct(spec))    return (int64_t)avail * pct_of(spec) / 100;
    return spec;   // pixels
}

// Measure a node's natural (content) height, recursing into containers.
static int measure_h(Node* n, int avail_w);

static int container_content_h(Node* n, int inner_w) {
    int total = 0; bool first = true;
    if (n->lay == L_COL) {
        for (Node* c = n->first_child; c; c = c->next_sib) {
            if (c->hidden) continue;
            if (!first) total += n->gap;
            total += measure_h(c, inner_w);
            first = false;
        }
    } else { // ROW / ROW_SPACE: height = tallest child
        for (Node* c = n->first_child; c; c = c->next_sib) {
            if (c->hidden) continue;
            int ch = measure_h(c, inner_w);
            if (ch > total) total = ch;
        }
    }
    return total;
}

static int measure_h(Node* n, int avail_w) {
    if (n->kind == K_LABEL) return text_h(n->text, n->font);
    if (n->kind == K_CANVAS) return n->ch;
    int inner_w = (n->w_spec == CONTENT ? avail_w : resolve(n->w_spec, avail_w, "", n->font, avail_w)) - 2 * n->pad;
    int ch = container_content_h(n, inner_w) + 2 * n->pad;
    if (n->h_spec != CONTENT) return resolve(n->h_spec, ch, "", n->font, ch);
    return ch;
}

static void layout(Node* n, int ox, int oy, int avail_w, int avail_h);

static void place_child_abs(Node* parent, Node* c, int avail_w, int avail_h) {
    int cw = resolve(c->w_spec, avail_w, c->text, c->font, c->kind == K_LABEL ? text_w(c->text, c->font) : avail_w);
    int ch = measure_h(c, avail_w);
    int x = parent->x, y = parent->y;
    switch (c->align) {
        case Align::TopLeft:    x = parent->x;                       y = parent->y; break;
        case Align::TopMid:     x = parent->x + (avail_w - cw) / 2;  y = parent->y; break;
        case Align::TopRight:   x = parent->x + avail_w - cw;        y = parent->y; break;
        case Align::Center:     x = parent->x + (avail_w - cw) / 2;  y = parent->y + (avail_h - ch) / 2; break;
        case Align::BottomLeft: x = parent->x;                       y = parent->y + avail_h - ch; break;
        case Align::BottomMid:  x = parent->x + (avail_w - cw) / 2;  y = parent->y + avail_h - ch; break;
        case Align::BottomRight:x = parent->x + avail_w - cw;        y = parent->y + avail_h - ch; break;
        case Align::LeftMid:    x = parent->x;                       y = parent->y + (avail_h - ch) / 2; break;
        case Align::RightMid:   x = parent->x + avail_w - cw;        y = parent->y + (avail_h - ch) / 2; break;
    }
    layout(c, x + c->dx, y + c->dy, cw, ch);
}

static void layout(Node* n, int ox, int oy, int avail_w, int avail_h) {
    n->x = ox; n->y = oy;
    n->w = (n->w_spec == CONTENT)
             ? (n->kind == K_LABEL ? text_w(n->text, n->font) : avail_w)
             : resolve(n->w_spec, avail_w, n->text, n->font, n->kind == K_LABEL ? text_w(n->text, n->font) : avail_w);
    n->h = (n->h_spec == CONTENT) ? measure_h(n, avail_w)
                                  : resolve(n->h_spec, avail_h, "", n->font, measure_h(n, avail_w));

    if (n->kind != K_CONT) return;
    int inner_x = n->x + n->pad, inner_y = n->y + n->pad;
    int inner_w = n->w - 2 * n->pad, inner_h = n->h - 2 * n->pad;

    if (n->lay == L_COL) {
        // Pass 1: total fixed (non-grow) child height + sum of grow weights, so
        // grow children can share whatever vertical space is left over.
        int fixed = 0, grow_total = 0, visible = 0;
        for (Node* c = n->first_child; c; c = c->next_sib) {
            if (c->hidden) continue;
            visible++;
            if (c->growf > 0) grow_total += c->growf;
            else              fixed += measure_h(c, inner_w);
        }
        int gaps  = visible > 1 ? (visible - 1) * n->gap : 0;
        int extra = inner_h - fixed - gaps;
        if (extra < 0) extra = 0;

        int cy = inner_y;
        for (Node* c = n->first_child; c; c = c->next_sib) {
            if (c->hidden) continue;
            int ch = (c->growf > 0 && grow_total > 0)
                       ? (extra * c->growf) / grow_total
                       : measure_h(c, inner_w);
            layout(c, inner_x, cy, inner_w, ch);
            cy += ch + n->gap;
        }
    } else if (n->lay == L_ROW_SPACE) {
        // first child left, last child right; middles ignored on this tiny panel
        Node* firstc = nullptr; Node* lastc = nullptr;
        for (Node* c = n->first_child; c; c = c->next_sib) { if (c->hidden) continue; if (!firstc) firstc = c; lastc = c; }
        if (firstc) layout(firstc, inner_x, inner_y, inner_w, inner_h);
        if (lastc && lastc != firstc) {
            int lw = text_w(lastc->text, lastc->font);
            layout(lastc, n->x + n->w - n->pad - lw, inner_y, lw, inner_h);
        }
    } else if (n->lay == L_ROW) {
        int cx = inner_x;
        for (Node* c = n->first_child; c; c = c->next_sib) {
            if (c->hidden) continue;
            int cw = (c->kind == K_LABEL) ? text_w(c->text, c->font) : inner_w;
            // center single-label buttons
            if (n->center && c->kind == K_LABEL) cx = n->x + (n->w - cw) / 2;
            layout(c, cx, inner_y + (inner_h - measure_h(c, inner_w)) / 2, cw, inner_h);
            cx += cw + n->gap;
        }
    } else { // L_NONE: absolute children by align
        for (Node* c = n->first_child; c; c = c->next_sib) {
            if (c->hidden) continue;
            place_child_abs(n, c, n->w, n->h);
        }
    }
}

// ===========================================================================
//  draw (called inside the GxEPD2 paged loop)
// ===========================================================================
static void draw_text(int x, int y, const char* s, Font f, bool invert) {
    display.setTextSize(fsize(f));
    display.setTextColor(invert ? cbg() : cfg());
    int lh = line_h(f), cy = y;
    const char* p = s;
    char line[40]; int li = 0;
    auto flush_line = [&]() {
        line[li] = 0;
        display.setCursor(x, cy + 1);
        display.print(line);
        cy += lh; li = 0;
    };
    for (; *p; p++) { if (*p == '\n') flush_line(); else if (li < 39) line[li++] = *p; }
    flush_line();
}

static void draw_node(Node* n) {
    if (n->hidden) return;
    int sy = n->y + header_h() - g_scroll;    // below the status bar, scrolled

    if (n->kind == K_LABEL) {
        bool inv = false;
        // inverted text when the enclosing clickable row is focused
        for (Node* a = n->parent; a; a = a->parent) if (a == g_focus) { inv = true; break; }
        draw_text(n->x, sy, n->text, n->font, inv);
        return;
    }
    if (n->kind == K_CANVAS) {
        for (int yy = 0; yy < n->ch; yy++)
            for (int xx = 0; xx < n->cw; xx++) {
                uint8_t b = n->cbuf[yy * cv_stride(n) + (xx >> 3)];
                if (b & (0x80 >> (xx & 7))) display.drawPixel(n->x + xx, sy + yy, cfg());
            }
        return;
    }
    // container
    if (n == g_focus)   display.fillRect(n->x, sy, n->w, n->h, cfg());
    else if (n->card)   display.drawRect(n->x, sy, n->w, n->h, cfg());
    for (Node* c = n->first_child; c; c = c->next_sib) draw_node(c);
}

// ===========================================================================
//  mono entry points
// ===========================================================================
namespace mono {

static bool s_drawn = false;     // has the panel been drawn since boot?
static int  s_partials = 0;      // partial refreshes since the last full one
// Effectively never auto-full-refresh — this panel doesn't need it; ghosting is
// minimal and full flashes are the annoyance. (Was 20.)
static const int MAX_PARTIALS = 100000;

// ---- transient toast banner ------------------------------------------------
static char     g_toast[40]   = {0};
static uint32_t g_toast_until = 0;

void toast(const char* msg, uint32_t ms) {
    strncpy(g_toast, msg ? msg : "", sizeof(g_toast) - 1);
    g_toast[sizeof(g_toast) - 1] = 0;
    g_toast_until = millis() + ms;
    g_dirty = true;
}

static void draw_toast() {
    if (!g_toast[0] || (int32_t)(millis() - g_toast_until) >= 0) return;
    int tw = text_w(g_toast, Font::Body), th = text_h(g_toast, Font::Body);
    int bw = tw + 12, bh = th + 8;
    int bx = (display.width() - bw) / 2;
    if (bx < 0) { bx = 0; bw = display.width(); }
    int by = header_h() + 4;
    display.fillRect(bx, by, bw, bh, cbg());
    display.drawRect(bx, by, bw, bh, cfg());
    draw_text(bx + 6, by + 4, g_toast, Font::Body, false);
}

void reset() {
    g_used = 0; g_focus = nullptr;
    g_root = alloc(K_CONT);
    g_root->lay = L_COL;
    g_root->w_spec = display.width();
    g_root->h_spec = display.height();
    g_root->pad = 2;
    g_dirty = true;
    g_scroll = 0;                // new screen starts at the top
    // Note: do NOT force a full refresh on screen change — a partial refresh
    // already repaints the whole window (fillScreen + redraw), so switching
    // screens stays flash-free. Only the very first boot draw is full.
}

// First focusable node in tree order.
static Node* first_focusable(Node* n) {
    if (!n) return nullptr;
    if (n->focusable_ && !n->hidden) return n;
    for (Node* c = n->first_child; c; c = c->next_sib) { Node* f = first_focusable(c); if (f) return f; }
    return nullptr;
}
static void collect_focusables(Node* n, Node** out, int& cnt, int max) {
    if (!n || n->hidden || cnt >= max) return;
    if (n->focusable_) out[cnt++] = n;
    for (Node* c = n->first_child; c; c = c->next_sib) collect_focusables(c, out, cnt, max);
}

// Render only when something changed. Routine updates use a fast PARTIAL
// refresh (no full-screen flash); every MAX_PARTIALS we do one full refresh to
// clear e-ink ghosting, and a brand-new screen always gets a clean full pass.
void render() {
    if (!g_dirty) return;
    g_dirty = false;

    // Lay out into the viewport BELOW the status bar — draw_node() shifts every
    // node down by header_h(), so laying out the full panel height would push the
    // bottom `header_h()` pixels (e.g. bottom-aligned buttons) off-screen.
    int vh = display.height() - header_h();
    g_root->h_spec = vh;
    layout(g_root, 0, 0, display.width(), vh);
    if (!g_focus) g_focus = first_focusable(g_root);

    bool full = !s_drawn || s_partials >= MAX_PARTIALS;
    if (full) { display.setFullWindow(); s_partials = 0; }
    else      { display.setPartialWindow(0, 0, display.width(), display.height()); s_partials++; }

    display.firstPage();
    do {
        display.fillScreen(cbg());
        draw_node(g_root);
        if (g_sb_fn) {
            // Status bar on top — clear the band (in case content scrolled under
            // it), let the app paint it, then a separator line.
            display.fillRect(0, 0, display.width(), g_sb_h, cbg());
            g_sb_fn(display.width(), g_sb_h);
            display.drawFastHLine(0, g_sb_h - 1, display.width(), cfg());
        }
        draw_toast();
    } while (display.nextPage());
    s_drawn = true;
}

void redraw() { g_dirty = true; }

uint16_t fg() { return cfg(); }
uint16_t bg() { return cbg(); }
bool get_invert() { return g_invert; }
void set_invert(bool on) {
    if (g_invert == on) return;
    g_invert = on;
    s_drawn = false;   // force a full refresh so the e-ink clears the old scheme cleanly
    g_dirty = true;
}

void set_statusbar(int h, StatusbarFn fn) { g_sb_h = h; g_sb_fn = fn; g_dirty = true; }

void tick(uint32_t now_ms) {
    // Expire the toast banner: clear it and force one redraw to erase it.
    if (g_toast[0] && (int32_t)(now_ms - g_toast_until) >= 0) { g_toast[0] = 0; g_dirty = true; }
    for (int i = 0; i < NTMR; i++) {
        if (g_tmr[i].used && (int32_t)(now_ms - g_tmr[i].next) >= 0) {
            g_tmr[i].next = now_ms + g_tmr[i].period;
            if (g_tmr[i].cb) g_tmr[i].cb(g_tmr[i].user);
        }
    }
}

// ---- screen stack ----------------------------------------------------------
static const int NAV = 8;
static ScreenFn g_nav[NAV];
static int      g_nav_depth = 0;
static ScreenFn g_current = nullptr;

void go(ScreenFn fn) {
    if (!fn) return;
    if (g_current && g_nav_depth < NAV) g_nav[g_nav_depth++] = g_current;
    g_current = fn;
    reset();
    fn();
    render();
}

void back() {
    if (g_nav_depth == 0) return;
    g_current = g_nav[--g_nav_depth];
    reset();
    if (g_current) g_current();
    render();
}

// Lowest pixel any visible node reaches — the true content height for scrolling.
static int content_bottom(Node* n) {
    if (!n || n->hidden) return 0;
    int b = n->y + n->h;
    for (Node* c = n->first_child; c; c = c->next_sib) { int cb = content_bottom(c); if (cb > b) b = cb; }
    return b;
}

void feed_key(char key) {
    if (key == 'B') { back(); return; }

    Node* foc[POOL]; int cnt = 0;
    collect_focusables(g_root, foc, cnt, POOL);

    if (cnt == 0) {
        // No focusable rows (read-only screen) — page-scroll the content.
        int vp = display.height() - header_h();   // viewport below the status bar
        int total = content_bottom(g_root);
        int maxscroll = total - vp; if (maxscroll < 0) maxscroll = 0;
        if (maxscroll == 0) return;             // everything fits
        int step = vp / 2;
        if (key == 'U') g_scroll -= step;
        else if (key == 'D') g_scroll += step;
        else return;
        if (g_scroll < 0) g_scroll = 0;
        if (g_scroll > maxscroll) g_scroll = maxscroll;
        g_dirty = true; render();
        return;
    }
    int idx = 0;
    for (int i = 0; i < cnt; i++) if (foc[i] == g_focus) { idx = i; break; }

    auto ensure_visible = [&]() {
        if (!g_focus) return;
        int H = display.height(), hh = header_h();
        int top = g_focus->y + hh - g_scroll;
        int bot = g_focus->y + g_focus->h + hh - g_scroll;
        if (top < hh)     g_scroll = g_focus->y;                       // just below status bar
        else if (bot > H) g_scroll = g_focus->y + g_focus->h - (H - hh);
        if (g_scroll < 0) g_scroll = 0;
    };

    if (key == 'U')      { idx = (idx + cnt - 1) % cnt; g_focus = foc[idx]; ensure_visible(); g_dirty = true; render(); }
    else if (key == 'D') { idx = (idx + 1) % cnt;       g_focus = foc[idx]; ensure_visible(); g_dirty = true; render(); }
    else if (key == '\r' || key == 'E') {
        Node* f = g_focus;
        if (f && f->cb) f->cb(f->user);   // cb may call go()/back() or set_text
        render();
    }
}

} // namespace mono
} // namespace ui::kit

#endif // BOARD_WIO_L1
