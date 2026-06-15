#pragma once
// UI layout constants for LilyGo T5 ePaper S3 Pro (540x960)

#include "lvgl.h"

// ---------- Fonts ----------
#define UI_FONT_SMALL       &lv_font_noto_24
#define UI_FONT_BODY        &lv_font_noto_28
#define UI_FONT_TITLE       &lv_font_montserrat_bold_30
#define UI_FONT_NAV         &lv_font_noto_28
#define UI_FONT_CLOCK_SM    &lv_font_montserrat_bold_80
#define UI_FONT_CLOCK_LG    &lv_font_montserrat_bold_120

// ---------- Shared components ----------

#define UI_OUTER_WIDTH_PCT   95
#define UI_OUTER_MARGIN_X    5
#define UI_NAV_PAD_LEFT      5

// Statusbar
#define UI_STATUSBAR_HEIGHT  40
#define UI_STATUSBAR_Y       5
#define UI_STATUSBAR_PAD     5
#define UI_STATUSBAR_COL_PAD 8

// Screen top padding — push content below the statusbar overlay
#define UI_STATUSBAR_BOTTOM  (UI_STATUSBAR_Y + UI_STATUSBAR_HEIGHT - 5)

// Back button
#define UI_BACK_BTN_HEIGHT   70

// Gap between nav bar and content container (flex row gap)
#define UI_NAV_CONTENT_GAP   0
#define UI_BACK_BTN_PAD_TOP  10
#define UI_BACK_BTN_PAD_BOTTOM 10
#define UI_BACK_BTN_COL_PAD  8

// Menu / list items
#define UI_MENU_ITEM_HEIGHT  85
#define UI_MENU_ITEM_PAD     15
#define UI_MENU_ITEM_INSET   15

// Text button
#define UI_TEXT_BTN_HEIGHT   80
#define UI_TEXT_BTN_PAD      15
#define UI_TEXT_BTN_RADIUS   12
#define UI_TEXT_BTN_BORDER   3

// Nav band detection (screen-coordinate bottom of nav area)
#define UI_NAV_BAND_BOTTOM   (UI_STATUSBAR_BOTTOM + UI_BACK_BTN_HEIGHT)

// Message list
#define UI_MSG_LIST_PAD      2
#define UI_MSG_LIST_ROW_PAD  4
#define UI_MSG_WRAP_ROW_PAD  2
#define UI_MSG_BUBBLE_WIDTH  100
#define UI_MSG_BUBBLE_PAD    8
#define UI_MSG_BUBBLE_RADIUS 8
#define UI_MSG_BUBBLE_ROW_PAD 2
#define UI_MSG_SELF_INDENT   60

// Extended click areas
#define UI_EXT_CLICK_BACK    10
#define UI_EXT_CLICK_ACTION  8
#define UI_EXT_CLICK_LIST    0

// Action button (inline with back button)
#define UI_ACTION_BTN_H      56
#define UI_ACTION_BTN_PAD_H  18
#define UI_ACTION_BTN_PAD_V  8
#define UI_ACTION_BTN_RADIUS 12
#define UI_ACTION_BTN_BORDER 3

// Borders
#define UI_BORDER_CARD       3
#define UI_BORDER_THIN       2

// ---------- Splash screen ----------
#define UI_SPLASH_TITLE_Y   -60
#define UI_SPLASH_SUB_Y      60
#define UI_SPLASH_VER_Y     100
#define UI_SPLASH_STATUS_Y  160

// ---------- Home screen ----------
#define UI_HOME_NODE_Y       55
#define UI_HOME_SHOW_NODE    1
#define UI_HOME_CLOCK_INLINE 0
#define UI_HOME_CLOCK_Y      100
#define UI_HOME_DATE_Y       225
#define UI_HOME_MENU_Y       340
#define UI_HOME_MENU_SCROLL  0  // no scroll needed on 960px

// ---------- Lock screen ----------
#define UI_LOCK_NODE_Y       55
#define UI_LOCK_CLOCK_Y      100
#define UI_LOCK_DATE_Y       210

// ---------- Compose screen ----------
#define UI_COMPOSE_PICKER_CARD_Y     0
#define UI_COMPOSE_PICKER_CARD_H     110
#define UI_COMPOSE_FILTERS_Y         125
#define UI_COMPOSE_FILTERS_H         64
#define UI_COMPOSE_FILTERS_GAP       12
#define UI_COMPOSE_PICKER_LIST_Y     205
#define UI_COMPOSE_PICKER_BOTTOM_PAD 20
#define UI_COMPOSE_EDITOR_CARD_Y     0
#define UI_COMPOSE_EDITOR_CARD_H     86
#define UI_COMPOSE_MESSAGE_Y         105
#define UI_COMPOSE_MESSAGE_H         50
#define UI_COMPOSE_TA_Y              175
#define UI_COMPOSE_TEXT_GAP          20
#define UI_COMPOSE_EDITOR_BOTTOM_PAD 20
#define UI_COMPOSE_SEND_GAP          20
#define UI_COMPOSE_KB_H              320
#define UI_COMPOSE_SHOW_KB           1

// ---------- Contact detail screen ----------
#define UI_CONTACT_HERO_Y    130
#define UI_CONTACT_HERO_H    150
#define UI_CONTACT_BADGE_SZ  84
#define UI_CONTACT_NAME_X    108
#define UI_CONTACT_TYPE_X    108
#define UI_CONTACT_TYPE_Y    58
#define UI_CONTACT_DETAIL_Y  295
#define UI_CONTACT_DETAIL_H  230

// ---------- Ping screen ----------
#define UI_PING_BTN_ROW_Y    230
#define UI_PING_BTN_ROW_H    90

// ---------- Map screen ----------
#define UI_MAP_X             5
#define UI_MAP_Y             10
#define UI_MAP_W             530
#define UI_MAP_H             825
#define UI_MAP_BTN_W         80
#define UI_MAP_BTN_H         60
#define UI_MAP_BTN_SIDE      20
#define UI_MAP_BTN_BOTTOM    -20
#define UI_MAP_ZOOM_BOTTOM   -35
#define UI_MAP_INFO_BOTTOM   -85
