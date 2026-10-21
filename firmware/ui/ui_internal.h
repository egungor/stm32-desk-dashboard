/**
 * @file    ui_internal.h
 * @brief   State and helpers shared by the files of the UI module. Not part
 *          of the API (ui.h).
 */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui.h"
#include "ui_text.h"

/* Layout for the 480 x 272 panel. */
#define UI_WIDTH     480
#define UI_HEIGHT    272
#define BAR_H        36
#define BOTTOM_H     40
#define PAD          8
/* Touch margin of the volume slider (lv_obj_set_ext_click_area) and the
 * gap between the slider and the mute button. The gap must be wider than
 * the margin: where the slider's touch area covers the button, LVGL gives
 * the touch to the slider (created later), and a tap on mute then sets the
 * volume to the touched point. */
#define VOLUME_TOUCH_PAD 14
#define VOLUME_GAP       (VOLUME_TOUCH_PAD + 6)
#define BTN_RADIUS   12
#define FLIP_W       44
#define TAB_W        116
/* Application screen (ADR-005): rail on the left, the shown tab beside it. */
#define SHELL_RAIL_W 56
#define UI_CONTENT_W (UI_WIDTH - SHELL_RAIL_W) /* 424 */

/* Font Awesome glyphs in ui_font_14/16/22 (tools/gen-ui-fonts.sh). */
#define SYM_CHECK      "\xEF\x80\x8C"
#define SYM_WARNING    "\xEF\x81\xB1"
#define SYM_PREV       "\xEF\x81\x93"
#define SYM_NEXT       "\xEF\x81\x94"
#define SYM_PLUG       "\xEF\x87\xA6"
#define SYM_HOURGLASS  "\xEF\x89\x94"
#define SYM_VOLUME     "\xEF\x80\xA8" /* volume-up */
#define SYM_MUTED      "\xEF\x9A\xA9" /* volume-mute */
#define SYM_FLIP       "\xEF\x8B\xB1" /* sync-alt */
#define SYM_STAR       "\xEF\x80\x85"
#define SYM_COG        "\xEF\x80\x93"
#define SYM_PENCIL     "\xEF\x8C\x83" /* pencil-alt */
#define SYM_LIST       "\xEF\x80\xBA"
#define SYM_GRID       "\xEF\x80\x89" /* th-large */
#define SYM_UP         "\xEF\x81\xA2" /* arrow-up */
#define SYM_DOWN       "\xEF\x81\xA3" /* arrow-down */
#define SYM_BACKSPACE  "\xEF\x95\x9A"
#define SYM_CLOSE      "\xEF\x80\x8D" /* times */

LV_FONT_DECLARE(ui_font_14)
LV_FONT_DECLARE(ui_font_16)
LV_FONT_DECLARE(ui_font_22)
LV_FONT_DECLARE(ui_icons_40)

/** Colours of the current theme and accent (ui_theme.c). */
typedef struct
{
    uint32_t bg;       /**< screen */
    uint32_t bar;      /**< bars and panels */
    uint32_t btn;      /**< cards and buttons */
    uint32_t line;     /**< card outline (light theme) */
    uint32_t track;    /**< slider track */
    uint32_t accent;   /**< active device, selection, knob */
    uint32_t on_acc;   /**< text on the accent */
    uint32_t text;
    uint32_t muted;    /**< secondary text, icons */
    uint32_t link;     /**< connection state */
    bool     dark;
} UiPalette;

/** How much a device button shows (layout and grid). */
typedef enum
{
    CARD_LARGE,  /**< A 2x2: 40 px icon, 22 px name on two lines, state line */
    CARD_MEDIUM, /**< A 3x2: 22 px icon, 16 px name on two lines, state line */
    CARD_SMALL,  /**< A 3x3: 16 px icon, one line */
    CARD_ROW,    /**< A list row */
    CARD_LIST,   /**< B: row with the Windows name under an alias, check mark */
    CARD_TILE,   /**< C: small tile, icon and star over a one-line name */
} CardKind;

/** One device card or list row. */
typedef struct
{
    lv_obj_t *btn;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *state;  /**< "Active output" / "Switching..."; NULL if none */
    lv_obj_t *star;   /**< favorite mark; NULL if none */
    lv_obj_t *sub;    /**< B: Windows name under an alias; NULL if none */
} UiDeviceButton;

#define UI_EDIT_PER_PAGE 4U

/** One card of the edit mode: three objects, so four cards fit the LVGL
 *  heap next to the main screen. */
typedef struct
{
    lv_obj_t *card;
    lv_obj_t *name;     /**< icon and name in one label */
    lv_obj_t *buttons;  /**< lv_buttonmatrix: left, right, star, pencil */
} UiEditCard;

typedef struct
{
    /* Handlers. */
    UiSelectCallback   on_select;
    void              *user;
    UiVolumeCallback   on_volume;
    UiMuteCallback     on_mute;
    void              *audio_user;
    UiFlipCallback     on_flip;
    void              *flip_user;
    UiSettingsCallback on_settings;
    void              *settings_user;

    /* State reported by the host. */
    UiDevice devices[UI_MAX_DEVICES];
    uint32_t count;
    uint8_t  active;
    uint8_t  pending;
    uint8_t  volume;  /**< 0-100 or UI_VOLUME_UNKNOWN */
    uint8_t  muted;   /**< 0, 1 or UI_VOLUME_UNKNOWN */
    bool     unmute_asked;  /**< slider moved while muted: unmute sent */

    /* Settings and view. */
    UiSettings settings;
    bool       settings_seen;      /**< Ui_ApplySettings called since Ui_Init */
    UiPalette  pal;
    uint32_t   tab;                /**< UI_TAB_* */
    uint32_t   page;
    uint8_t    view[UI_MAX_DEVICES]; /**< device indices shown in the tab */
    uint32_t   view_count;

    /* Flip button. */
    uint32_t flip_pressed_at;  /**< lv_tick_get() at press */
    bool     flip_armed;       /**< held long enough; flip on release */

    /* Objects (rebuilt when the appearance changes). */
    lv_obj_t      *waiting_screen;
    lv_obj_t      *main_screen;    /**< application screen: rail and tabs (ui_shell.c) */
    lv_obj_t      *dash_tab;       /**< Dashboard tab content, 424 x 272 */
    lv_obj_t      *audio_tab;      /**< Audio tab content, 424 x 272 */
    lv_obj_t      *rail_btns[UI_APP_TAB_COUNT];
    lv_obj_t      *rail_labels[UI_APP_TAB_COUNT];
    uint32_t       app_tab;        /**< UI_APP_TAB_*, kept over a rebuild */
    UiDeviceButton buttons[UI_MAX_PER_PAGE];
    uint32_t       per_page;
    lv_obj_t      *tab_btns[2];
    lv_obj_t      *tab_labels[2];
    lv_obj_t      *empty_label;
    lv_obj_t      *pager;
    lv_obj_t      *page_label;
    lv_obj_t      *mute_btn;
    lv_obj_t      *mute_icon;
    lv_obj_t      *slider;
    lv_obj_t      *volume_label;
    lv_obj_t      *prev_btn;
    lv_obj_t      *next_btn;
    lv_obj_t      *flip_icons[2];  /**< waiting screen, main screen (NULL in B and C) */
    CardKind       card_kind;      /**< of the buttons on the main screen */
    lv_obj_t      *list_title;     /**< B: "ALL OUTPUTS" / "FAVORITES" */
    lv_obj_t      *list_count;     /**< B: "9 outputs" */
    lv_obj_t      *hero_icon;      /**< C: active device panel */
    lv_obj_t      *hero_name;
    lv_obj_t      *hero_sub;
    lv_obj_t      *notice;
    lv_obj_t      *notice_label;
    lv_timer_t    *notice_timer;   /**< created once */

    /* Settings page (ui_settings.c), rebuilt with the other screens. */
    lv_obj_t *settings_screen;
    lv_obj_t *settings_list;       /**< the scrolling list of rows */
    int32_t   settings_scroll;     /**< list scroll position kept over a rebuild */
    lv_obj_t *set_layout;          /**< segmented controls (lv_buttonmatrix) */
    lv_obj_t *set_theme;
    lv_obj_t *set_grid;
    lv_obj_t *set_startup;
    lv_obj_t *set_timeout;
    lv_obj_t *set_language;
    lv_obj_t *set_swatches[UI_ACCENT_COUNT];
    lv_obj_t *set_limit_switch;
    lv_obj_t *set_limit_box;       /**< hint and slider, hidden while the limit is off */
    lv_obj_t *set_limit_slider;
    lv_obj_t *set_limit_label;
    lv_obj_t *set_flip_switch;
    lv_obj_t *set_startup_screen;
    bool      rebuild_queued;      /**< ui_request_rebuild() pending */

    /* Edit mode and alias keyboard (ui_edit.c): built while shown. */
    UiEditHandlers edit;
    void          *edit_user;
    bool           edit_set;            /**< Ui_SetEditHandlers called */
    lv_obj_t      *edit_btn;            /**< pencil in the main screen's bar */
    lv_obj_t      *edit_screen;
    UiEditCard     edit_cards[UI_EDIT_PER_PAGE];
    lv_obj_t      *edit_prev;
    lv_obj_t      *edit_next;
    lv_obj_t      *edit_page_label;
    lv_obj_t      *edit_empty;
    uint32_t       edit_page;
    lv_obj_t      *kb_screen;
    lv_obj_t      *kb_text;             /**< lv_textarea with the alias */
    uint8_t        kb_device;           /**< device being renamed */
    char           kb_draft[UI_MAX_ALIAS_BYTES + 1U]; /**< text kept over a rebuild */

    /* Screen timeout: created once in Ui_Init, never rebuilt. */
    UiBacklightCallback on_backlight;
    void               *backlight_user;
    lv_timer_t         *screen_timer;
    lv_obj_t           *wake_catcher;  /**< black, over everything, while dark */
    bool                screen_off;
} UiState;

extern UiState g_ui;

/* Main screen pieces shared by the layouts (ui.c). */
void      ui_on_device_clicked(lv_event_t *e);  /**< user data: slot */
void      ui_on_tab_clicked(lv_event_t *e);     /**< user data: UI_TAB_* */
void      ui_on_page_clicked(lv_event_t *e);    /**< user data: NULL = previous */
void      ui_on_slider_event(lv_event_t *e);
void      ui_on_mute_clicked(lv_event_t *e);
void      ui_on_settings_clicked(lv_event_t *e);
void      ui_on_edit_clicked(lv_event_t *e);
lv_obj_t *ui_make_small_button(lv_obj_t *parent, int32_t w, int32_t h, const char *symbol, lv_obj_t **label);
void      ui_style_volume_slider(lv_obj_t *slider);

/** The Audio tab's top bar (layouts A and B): All / Favorites and the edit
 *  button, @p width wide at the top of @p parent (ui.c). */
void ui_build_tab_bar(lv_obj_t *parent, int32_t width);

/* Audio tab content of layouts B (list) and C (large tile) (ui_layouts.c),
 * built into g_ui.audio_tab. They set g_ui.per_page, g_ui.card_kind and the
 * main screen objects. */
void ui_build_list_main(void);
void ui_build_tiles_main(void);

/* Application screen (ui_shell.c, ADR-005). */
void ui_shell_build(void);          /**< create g_ui.main_screen, rail, dash_tab, audio_tab */
void ui_shell_show_tab(uint32_t tab); /**< show UI_APP_TAB_* in the content area */

/** Fill g_ui.pal from g_ui.settings (theme, accent). */
void ui_theme_update(void);

/** Accent colour @p accent on the current theme (for the colour swatches). */
uint32_t ui_theme_accent(uint8_t accent);

/* Building blocks shared by the screens (ui.c). */
lv_obj_t *ui_make_screen(void);
lv_obj_t *ui_make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *ui_make_box(lv_obj_t *parent);
void      ui_style_card(lv_obj_t *obj, int32_t radius);

/** Report g_ui.settings to the settings handler. */
void ui_notify_settings(void);

/** Rebuild every screen with the current settings on the next LVGL timer
 *  run: a control must not delete itself from its own event handler. */
void ui_request_rebuild(void);

/** Show the main screen / the settings page. */
void ui_show_main(void);
void ui_show_settings(void);

/** Icon glyph of a device (UiIcon) in ui_font_* / ui_icons_40 (ui.c). */
const char *ui_icon_symbol(uint8_t icon);

/** Name shown for a device: alias if set, else the Windows name (ui.c). */
const char *ui_display_name(const UiDevice *d);

/* Edit mode and alias keyboard (ui_edit.c). */
void ui_show_edit(void);
void ui_edit_build(void);       /**< create g_ui.edit_screen */
void ui_edit_render(void);      /**< show g_ui.devices in the cards */
void ui_keyboard_build(void);   /**< create g_ui.kb_screen for kb_device with kb_draft */
void ui_edit_drop(void);        /**< delete the edit and keyboard screens (async) */

/* Settings page (ui_settings.c). */
void ui_settings_build(void);   /**< create g_ui.settings_screen */
void ui_settings_render(void);  /**< show g_ui.settings in the controls */

/** lv_color_t of a palette value. */
static inline lv_color_t ui_color(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

#endif /* UI_INTERNAL_H */
