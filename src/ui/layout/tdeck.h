#pragma once
// UI layout constants for LilyGo T-Deck (320x240)

#include "lvgl.h"

// ---------- Fonts ----------
// Custom noto_14/16 include FA5 glyphs (bluetooth, GPS, battery icons)
// that the built-in montserrat_14/16 lack.
#define UI_FONT_SMALL       &lv_font_noto_14
#define UI_FONT_BODY        &lv_font_noto_14
#define UI_FONT_TITLE       &lv_font_noto_16
#define UI_FONT_NAV         &lv_font_noto_15
#define UI_FONT_CLOCK_SM    &lv_font_montserrat_bold_30
#define UI_FONT_CLOCK_LG    &lv_font_montserrat_bold_30

// ---------- Shared components ----------

#define UI_OUTER_WIDTH_PCT   99
#define UI_OUTER_MARGIN_X    1
#define UI_NAV_PAD_LEFT      1

// Statusbar
#define UI_STATUSBAR_HEIGHT  18
#define UI_STATUSBAR_Y       0
#define UI_STATUSBAR_PAD     1
#define UI_STATUSBAR_COL_PAD 2

// Screen top padding — push content below the statusbar overlay
#define UI_STATUSBAR_BOTTOM  (UI_STATUSBAR_Y + UI_STATUSBAR_HEIGHT)

// Back button
#define UI_BACK_BTN_HEIGHT   28

// Gap between nav bar and content container (flex row gap)
#define UI_NAV_CONTENT_GAP   0
#define UI_BACK_BTN_PAD_TOP  0
#define UI_BACK_BTN_PAD_BOTTOM 2
#define UI_BACK_BTN_COL_PAD  3

// Menu / list items
#define UI_MENU_ITEM_HEIGHT  36
#define UI_MENU_ITEM_PAD     2
#define UI_MENU_ITEM_INSET   6

// Text button
#define UI_TEXT_BTN_HEIGHT   44
#define UI_TEXT_BTN_PAD      8
#define UI_TEXT_BTN_RADIUS   6
#define UI_TEXT_BTN_BORDER   2

// Nav band detection (screen-coordinate bottom of nav area)
#define UI_NAV_BAND_BOTTOM   (UI_STATUSBAR_BOTTOM + UI_BACK_BTN_HEIGHT)

// Message list
#define UI_MSG_LIST_PAD      1
#define UI_MSG_LIST_ROW_PAD  2
#define UI_MSG_WRAP_ROW_PAD  1
#define UI_MSG_BUBBLE_WIDTH  96
#define UI_MSG_BUBBLE_PAD    4
#define UI_MSG_BUBBLE_RADIUS 6
#define UI_MSG_BUBBLE_ROW_PAD 1
#define UI_MSG_SELF_INDENT   32

// Extended click areas
#define UI_EXT_CLICK_BACK    10
#define UI_EXT_CLICK_ACTION  8
#define UI_EXT_CLICK_LIST    0

// Action button (inline with back button)
#define UI_ACTION_BTN_H      18
#define UI_ACTION_BTN_PAD_H  6
#define UI_ACTION_BTN_PAD_V  0
#define UI_ACTION_BTN_RADIUS 4
#define UI_ACTION_BTN_BORDER 1

// Borders
#define UI_BORDER_CARD       1
#define UI_BORDER_THIN       1

// ---------- Splash screen ----------
#define UI_SPLASH_TITLE_Y   -40
#define UI_SPLASH_SUB_Y      20
#define UI_SPLASH_VER_Y      50
#define UI_SPLASH_STATUS_Y   80

// ---------- Home screen ----------
#define UI_HOME_NODE_Y       13
#define UI_HOME_SHOW_NODE    1
#define UI_HOME_CLOCK_INLINE 1       // time + date on one small line
#define UI_HOME_CLOCK_Y      34
#define UI_HOME_DATE_Y       52      // unused when inline
#define UI_HOME_MENU_Y       54
#define UI_HOME_MENU_SCROLL  1       // menu must scroll on 240px screen

// ---------- Lock screen ----------
#define UI_LOCK_NODE_Y       26
#define UI_LOCK_CLOCK_Y      40
#define UI_LOCK_DATE_Y       72

// ---------- Compose screen ----------
#define UI_COMPOSE_RECIPIENT_Y   0
#define UI_COMPOSE_RECIPIENT_H   28
#define UI_COMPOSE_LIST_Y        28
#define UI_COMPOSE_LIST_H        166
#define UI_COMPOSE_EDITOR_Y      28
#define UI_COMPOSE_EDITOR_H      16
#define UI_COMPOSE_TA_Y          46
#define UI_COMPOSE_TA_H          92
#define UI_COMPOSE_SEND_BOTTOM   -4
#define UI_COMPOSE_SHOW_KB       0

// ---------- Contact detail screen ----------
#define UI_CONTACT_HERO_Y    60
#define UI_CONTACT_HERO_H    65
#define UI_CONTACT_BADGE_SZ  40
#define UI_CONTACT_NAME_X    52
#define UI_CONTACT_TYPE_X    52
#define UI_CONTACT_TYPE_Y    28
#define UI_CONTACT_DETAIL_Y  130
#define UI_CONTACT_DETAIL_H  100

// ---------- Ping screen ----------
#define UI_PING_BTN_ROW_Y    111
#define UI_PING_BTN_ROW_H    45

// ---------- Map screen ----------
#define UI_MAP_X             UI_OUTER_MARGIN_X
#define UI_MAP_Y             0
#define UI_MAP_W             316
#define UI_MAP_H             206
#define UI_MAP_BTN_W         32
#define UI_MAP_BTN_H         24
#define UI_MAP_BTN_SIDE      10
#define UI_MAP_BTN_BOTTOM    -8
#define UI_MAP_ZOOM_BOTTOM   -28
#define UI_MAP_INFO_BOTTOM   -70
