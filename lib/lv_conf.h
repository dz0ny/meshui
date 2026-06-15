/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.5 — t-paper (e-ink + TFT support)
 */

#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/

#if defined(BOARD_TDECK)
/** Color depth: 16 — RGB565 for TFT */
#define LV_COLOR_DEPTH 16
#else
/** Color depth: 8 — matches LV_COLOR_FORMAT_L8 for e-ink grayscale */
#define LV_COLOR_DEPTH 8
#endif

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

/* Custom malloc routes to PSRAM via heap_caps */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CUSTOM
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   HAL SETTINGS
 *====================*/

#if defined(BOARD_TDECK)
#define LV_DEF_REFR_PERIOD 16     /**< [ms] — ~60 FPS for TFT */
#else
#define LV_DEF_REFR_PERIOD 33     /**< [ms] — ~30 FPS for e-ink */
#endif

/* Tick provided via lv_tick_set_cb() in ui_port*.cpp */

/*====================
   OPERATING SYSTEM
 *====================*/

#define LV_USE_OS   LV_OS_NONE

/*====================
   FEATURE CONFIG
 *====================*/

#if defined(BOARD_TDECK)
/** TFT can handle animations */
#define LV_ANIM_DEF_TIME 200
#define LV_USE_ANIM 1
#else
/** Disable animations entirely for e-paper.
 *  Note: LV_USE_ANIM is NOT a real LVGL knob (no global anim kill-switch in v9),
 *  so animations are still compiled in. The one that actually shows on e-ink is
 *  the internal scroll-to-view (focus/tap brings a row into view): it animates
 *  the scroll over SCROLL_ANIM_TIME_MIN..MAX (200..400 ms) and repaints every
 *  intermediate step. Those macros are #ifndef-guarded in lv_obj_scroll.c, so
 *  forcing them to 0 here makes all internal scrolling a single instant jump —
 *  one refresh instead of an animation. */
#define LV_ANIM_DEF_TIME 0
#define LV_USE_ANIM 0
#define SCROLL_ANIM_TIME_MIN 0
#define SCROLL_ANIM_TIME_MAX 0
#endif

#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0
#define LV_USE_ASSERT_OBJ           0

#define LV_DRAW_BUF_ALIGN 4

/** Reduce widget style cache — simple UI */
#define LV_STYLE_PROP_MATRIX_FLAT_SIZE 0

/** Disable label text selection (saves RAM per label) */
#define LV_LABEL_TEXT_SELECTION 0

/** Disable long label scrolling — e-ink can't animate, TFT doesn't need it */
#define LV_LABEL_LONG_TXT_HINT 0

#if defined(BOARD_TDECK)
#define LV_OPA_MIX_MAX_SPEED 256
#else
/** No opacity/blending needed on e-ink */
#define LV_OPA_MIX_MAX_SPEED 0
#endif

/** Reduce draw unit count */
#define LV_DRAW_SW_DRAW_UNIT_CNT 1

/*====================
   FONT USAGE
 *====================*/

#define LV_FONT_MONTSERRAT_14 1   /* Default font (required by LVGL internals) */

#if defined(BOARD_TDECK)
#define LV_FONT_MONTSERRAT_16 0   /* Using custom noto_16 with FA5 glyphs instead */
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_28 0
#else
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_28 0
#endif

/** Default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   THEME USAGE
 *====================*/

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0

/*====================
   LAYOUTS
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
   WIDGETS
 *====================*/

#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_BAR        0
#define LV_USE_BUTTON     1
#if defined(BOARD_TDECK)
#define LV_USE_BUTTONMATRIX 0   /* not needed — physical keyboard */
#define LV_USE_KEYBOARD   0
#else
#define LV_USE_BUTTONMATRIX 1   /* required by on-screen keyboard */
#define LV_USE_KEYBOARD   1
#endif
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     1
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      1  /* required by canvas */
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_LABEL      1
#define LV_USE_LED        0
#define LV_USE_LINE       1
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/*====================
   DRAWING / RENDERING
 *====================*/

#if defined(BOARD_TDECK)
/** TFT can use complex drawing features */
#define LV_USE_DRAW_SW_COMPLEX 1
#else
/** Disable complex drawing features not needed on e-ink (shadows, gradients, etc.) */
#define LV_USE_DRAW_SW_COMPLEX 0
#endif

#if !defined(BOARD_TDECK)
/* E-ink renders 1-bit (I1): crisp black/white, 1/8th the draw buffer, and
 * lets the panel use the fast mono DU waveform. L8 stays enabled so the Map /
 * Trail off-screen canvases still composite onto the I1 screen. */
#define LV_DRAW_SW_SUPPORT_I1   1
#define LV_DRAW_SW_SUPPORT_L8   1
#endif

/** Disable image caching — redraws infrequently */
#define LV_IMAGE_CACHE_DEF_SIZE 0

/** Disable layer caching */
#define LV_LAYER_SIMPLE_BUF_SIZE 0

/** Disable vector graphics engines */
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_THORVG 0
#define LV_USE_DRAW_VG_LITE 0

/** Disable font engines we don't use (we use pre-compiled .c fonts) */
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0

/** Disable BiDi and Arabic shaping — saves RAM and CPU */
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/** Disable logging in release builds */
#define LV_USE_LOG 0

/** Image decoders — only need built-in */
#define LV_USE_LIBPNG 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_BMP 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_RLE 0
#define LV_USE_LZ4 0

/*====================
   OTHERS
 *====================*/

#define LV_USE_SNAPSHOT   0
#define LV_USE_SYSMON     0
#define LV_USE_PROFILER   0
#define LV_USE_MONKEY     0
#define LV_USE_GRIDNAV    0
#define LV_USE_FRAGMENT   0
#define LV_USE_OBSERVER   0
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

#define LV_BUILD_EXAMPLES 0

#endif /*LV_CONF_H*/

#endif /*Enable content*/
