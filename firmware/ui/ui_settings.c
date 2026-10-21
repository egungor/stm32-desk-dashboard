/**
 * @file    ui_settings.c
 * @brief   Settings page (#60; design A from #57).
 *
 * One screen: a bar with the back button, then a vertically scrolling list
 * of rows, each a label and a control. A change is applied at once:
 * g_ui.settings is updated, the whole record goes to the settings handler,
 * and a change of theme, accent, grid or language rebuilds the screens on
 * the next LVGL timer run (ui_request_rebuild), so the control that was
 * tapped is not deleted inside its own event.
 *
 * Two sections (ADR-005): GENEL (theme, colour, language, screen timeout,
 * flip, startup screen) and SES (layout, grid, startup tab, volume limit).
 * "Layout" chooses cards (A), list (B) or large tile (C); the grid row
 * applies to the cards layout and is greyed out with the others.
 * screen_timeout is used by the UI's screen timeout (ui.c); flipped and
 * the volume limit are for the display port and the host; startup_screen
 * chooses the tab shown at startup (ui_shell.c).
 */
#include <string.h>

#include "ui_internal.h"

#define ROW_H        48
#define SEG_H        36
#define SWATCH_D     26
#define SWITCH_W     48
#define SWITCH_H     26
#define LIMIT_STEP   5U   /**< volume limit in steps of 5 % */

/* Settings changed by a segmented control (user data of its events). */
typedef enum
{
    FIELD_LAYOUT,
    FIELD_THEME,
    FIELD_GRID,
    FIELD_STARTUP,
    FIELD_TIMEOUT,
    FIELD_LANGUAGE,
    FIELD_STARTUP_SCREEN,
} SegField;

/* Screen timeout choices in minutes (the record allows 0-60). */
static const uint8_t k_timeouts[] = { 0U, 1U, 5U, 15U };

/* lv_buttonmatrix keeps a pointer to its map: the maps live here and are
 * filled with the current language's text when the page is built. */
static const char *s_layout_map[4];
static const char *s_theme_map[3];
static const char *s_grid_map[5];
static const char *s_startup_map[4];
static const char *s_timeout_map[5];
static const char *s_startup_screen_map[UI_STARTUP_SCREEN_COUNT + 1U];
/* The startup screen segment in the rail's order: Dashboard, Music, Audio
 * (button index -> UI_STARTUP_SCREEN_*). */
static const uint8_t k_startup_screens[UI_STARTUP_SCREEN_COUNT] = { UI_STARTUP_SCREEN_DASHBOARD,
                                                                    UI_STARTUP_SCREEN_MUSIC,
                                                                    UI_STARTUP_SCREEN_AUDIO };
static const char *const k_language_map[] = { "T\xC3\xBC" "rk\xC3\xA7" "e", "English", "" };

/* Styles shared by the rows and controls (one copy instead of a local
 * style per object: the page has to fit in the LVGL heap next to the
 * other screens). Reset and filled again on every build (theme colours). */
static lv_style_t s_row;
static lv_style_t s_seg;
static lv_style_t s_seg_item;
static lv_style_t s_seg_checked;
static lv_style_t s_seg_pressed;
static lv_style_t s_seg_disabled;         /**< items */
static lv_style_t s_seg_disabled_checked; /**< the chosen item */
static lv_style_t s_switch;
static lv_style_t s_switch_on;
static lv_style_t s_knob;
static lv_style_t s_knob_on;
static bool       s_styles_init;

static void init_styles(void)
{
    lv_style_t *const all[] = { &s_row, &s_seg, &s_seg_item, &s_seg_checked, &s_seg_pressed, &s_seg_disabled, &s_seg_disabled_checked,
                                &s_switch, &s_switch_on, &s_knob, &s_knob_on };

    for (uint32_t i = 0U; i < sizeof(all) / sizeof(all[0]); i++)
    {
        if (s_styles_init)
        {
            lv_style_reset(all[i]);
        }
        lv_style_init(all[i]);
    }
    s_styles_init = true;

    lv_style_set_pad_hor(&s_row, 12);
    lv_style_set_pad_column(&s_row, 10);
    lv_style_set_border_side(&s_row, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&s_row, ui_color(g_ui.pal.line));
    lv_style_set_border_width(&s_row, 1);

    lv_style_set_bg_color(&s_seg, ui_color(g_ui.pal.track));
    lv_style_set_bg_opa(&s_seg, LV_OPA_COVER);
    lv_style_set_radius(&s_seg, 9);
    lv_style_set_pad_all(&s_seg, 3);
    lv_style_set_pad_gap(&s_seg, 3);
    lv_style_set_text_font(&s_seg, &ui_font_16);

    lv_style_set_radius(&s_seg_item, 7);
    lv_style_set_bg_opa(&s_seg_item, LV_OPA_TRANSP);
    lv_style_set_text_color(&s_seg_item, ui_color(g_ui.pal.muted));

    lv_style_set_bg_color(&s_seg_checked, ui_color(g_ui.pal.accent));
    lv_style_set_bg_opa(&s_seg_checked, LV_OPA_COVER);
    lv_style_set_text_color(&s_seg_checked, ui_color(g_ui.pal.on_acc));

    lv_style_set_bg_color(&s_seg_pressed, ui_color(g_ui.pal.btn));
    lv_style_set_bg_opa(&s_seg_pressed, LV_OPA_COVER);

    /* Greyed out with text and fill opacity, not the object's opa: that
     * would draw it through a layer buffer from the LVGL heap. */
    lv_style_set_text_opa(&s_seg_disabled, LV_OPA_40);
    lv_style_set_bg_opa(&s_seg_disabled_checked, LV_OPA_40);

    lv_style_set_bg_color(&s_switch, ui_color(g_ui.pal.track));
    lv_style_set_bg_opa(&s_switch, LV_OPA_COVER);
    lv_style_set_bg_color(&s_switch_on, ui_color(g_ui.pal.accent));
    lv_style_set_bg_color(&s_knob, ui_color(g_ui.pal.text));
    lv_style_set_shadow_width(&s_knob, 0);
    lv_style_set_bg_color(&s_knob_on, ui_color(g_ui.pal.on_acc));
}

/* ----------------------------------------------------------------- change */

/* Apply @p s (already validated by the caller) and report it. */
static void change(const UiSettings *s, bool appearance)
{
    g_ui.settings = *s;
    ui_notify_settings();
    if (appearance)
    {
        ui_request_rebuild();
    }
    else
    {
        ui_settings_render();
    }
}

static void on_segment(lv_event_t *e)
{
    lv_obj_t *const bm = lv_event_get_target_obj(e);
    const SegField field = (SegField)(uintptr_t)lv_event_get_user_data(e);
    const uint32_t idx = lv_buttonmatrix_get_selected_button(bm);
    UiSettings s = g_ui.settings;
    bool appearance = false;

    if (idx == LV_BUTTONMATRIX_BUTTON_NONE)
    {
        return;
    }
    switch (field)
    {
    case FIELD_LAYOUT:
        s.layout = (uint8_t)idx;
        appearance = true;
        break;
    case FIELD_THEME:
        s.theme = (uint8_t)idx;
        appearance = true;
        break;
    case FIELD_GRID:
        s.grid = (uint8_t)idx;
        appearance = true;
        break;
    case FIELD_STARTUP:
        s.startup_tab = (uint8_t)idx;
        break;
    case FIELD_TIMEOUT:
        s.screen_timeout = (idx < sizeof(k_timeouts)) ? k_timeouts[idx] : 0U;
        break;
    case FIELD_STARTUP_SCREEN:
        s.startup_screen = (idx < UI_STARTUP_SCREEN_COUNT) ? k_startup_screens[idx] : UI_STARTUP_SCREEN_DASHBOARD;
        break;
    default:
        s.language = (uint8_t)idx;
        appearance = true;
        break;
    }
    if (memcmp(&s, &g_ui.settings, sizeof(s)) != 0)
    {
        change(&s, appearance);
    }
}

static void on_swatch(lv_event_t *e)
{
    const uint8_t accent = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    UiSettings s = g_ui.settings;

    if (accent != s.accent)
    {
        s.accent = accent;
        change(&s, true);
    }
}

static void on_limit_switch(lv_event_t *e)
{
    UiSettings s = g_ui.settings;

    s.volume_limit_on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED) ? 1U : 0U;
    change(&s, false);
}

static void on_flip_switch(lv_event_t *e)
{
    UiSettings s = g_ui.settings;

    s.flipped = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED) ? 1U : 0U;
    change(&s, false);
}

/* The slider moves in steps of LIMIT_STEP; the record is sent once, when
 * the finger lifts. */
static void on_limit_slider(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    int32_t value;

    if ((code != LV_EVENT_VALUE_CHANGED) && (code != LV_EVENT_RELEASED))
    {
        return;
    }
    value = lv_slider_get_value(g_ui.set_limit_slider);
    value = ((value + (int32_t)(LIMIT_STEP / 2U)) / (int32_t)LIMIT_STEP) * (int32_t)LIMIT_STEP;
    lv_slider_set_value(g_ui.set_limit_slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_ui.set_limit_label, "%d%%", (int)value);
    if ((code == LV_EVENT_RELEASED) && ((uint8_t)value != g_ui.settings.volume_limit))
    {
        UiSettings s = g_ui.settings;

        s.volume_limit = (uint8_t)value;
        change(&s, false);
    }
}

static void on_back(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show_main();
}

/* ----------------------------------------------------------------- build */

/* One row: label on the left, the control (added by the caller) on the
 * right. Returns the row. */
static lv_obj_t *make_row(lv_obj_t *list, UiTextId label)
{
    lv_obj_t *row = ui_make_box(list);
    lv_obj_t *text;

    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_add_style(row, &s_row, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    text = ui_make_label(row, &ui_font_16, g_ui.pal.text, ui_text(label));
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    return row;
}

/* A section title between rows (GENEL, SES). */
static void make_section(lv_obj_t *list, UiTextId title)
{
    lv_obj_t *label = ui_make_label(list, &ui_font_14, g_ui.pal.muted, ui_text(title));

    lv_obj_set_style_pad_left(label, PAD + 4, 0);
    lv_obj_set_style_pad_top(label, 10, 0);
    lv_obj_set_style_pad_bottom(label, 2, 0);
}

/* Segmented control: the options of @p map (terminated by ""), one of them
 * marked in the accent colour. */
static lv_obj_t *make_segment(lv_obj_t *row, const char **map, int32_t width, SegField field)
{
    lv_obj_t *bm = lv_buttonmatrix_create(row);

    /* Not CHECKABLE: the buttons do not toggle themselves; the chosen one
     * is marked by ui_settings_render() from g_ui.settings only. */
    lv_obj_remove_style_all(bm);
    lv_buttonmatrix_set_map(bm, map);
    lv_obj_set_size(bm, width, SEG_H);
    lv_obj_add_style(bm, &s_seg, 0);
    lv_obj_add_style(bm, &s_seg_item, LV_PART_ITEMS);
    lv_obj_add_style(bm, &s_seg_checked, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_style(bm, &s_seg_pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_style(bm, &s_seg_disabled, LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_add_style(bm, &s_seg_disabled_checked, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_DISABLED);

    /* CLICKED, not VALUE_CHANGED: LVGL sends no click after a drag that
     * scrolled the list, so scrolling over a control changes nothing. */
    lv_obj_add_event_cb(bm, on_segment, LV_EVENT_CLICKED, (void *)(uintptr_t)field);
    return bm;
}

static lv_obj_t *make_switch(lv_obj_t *row, lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(row);

    lv_obj_set_size(sw, SWITCH_W, SWITCH_H);
    lv_obj_add_style(sw, &s_switch, LV_PART_MAIN);
    lv_obj_add_style(sw, &s_switch_on, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(sw, &s_knob, LV_PART_KNOB);
    lv_obj_add_style(sw, &s_knob_on, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(sw, 10);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static void build_swatches(lv_obj_t *row)
{
    lv_obj_t *box = ui_make_box(row);

    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_pad_column(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    for (uint32_t a = 0U; a < UI_ACCENT_COUNT; a++)
    {
        lv_obj_t *dot = ui_make_box(box);

        lv_obj_set_size(dot, SWATCH_D, SWATCH_D);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, ui_color(ui_theme_accent((uint8_t)a)), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_color(dot, ui_color(g_ui.pal.text), 0);
        lv_obj_set_style_outline_pad(dot, 2, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(dot, 5);
        lv_obj_add_event_cb(dot, on_swatch, LV_EVENT_CLICKED, (void *)(uintptr_t)a);
        g_ui.set_swatches[a] = dot;
    }
}

/* Hint and percentage slider under the volume limit row. */
static void build_limit_box(lv_obj_t *list)
{
    lv_obj_t *box = ui_make_box(list);
    lv_obj_t *hint;
    lv_obj_t *line;

    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(box, &s_row, 0);
    lv_obj_set_style_pad_ver(box, 8, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

    hint = ui_make_label(box, &ui_font_14, g_ui.pal.muted, ui_text(TXT_LIMIT_HINT));
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);

    line = ui_make_box(box);
    lv_obj_set_size(line, LV_PCT(100), 36);
    lv_obj_set_style_pad_column(line, 14, 0);
    lv_obj_set_style_pad_left(line, 8, 0);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_ui.set_limit_slider = lv_slider_create(line);
    lv_slider_set_range(g_ui.set_limit_slider, 0, 100);
    lv_obj_set_flex_grow(g_ui.set_limit_slider, 1);
    lv_obj_set_height(g_ui.set_limit_slider, 8);
    lv_obj_set_ext_click_area(g_ui.set_limit_slider, 14);
    lv_obj_set_style_bg_color(g_ui.set_limit_slider, ui_color(g_ui.pal.track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui.set_limit_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui.set_limit_slider, ui_color(g_ui.pal.accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_ui.set_limit_slider, ui_color(g_ui.pal.accent), LV_PART_KNOB);
    lv_obj_set_style_pad_all(g_ui.set_limit_slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(g_ui.set_limit_slider, on_limit_slider, LV_EVENT_ALL, NULL);

    g_ui.set_limit_label = ui_make_label(line, &ui_font_16, g_ui.pal.text, "");
    lv_obj_set_width(g_ui.set_limit_label, 48);
    lv_obj_set_style_text_align(g_ui.set_limit_label, LV_TEXT_ALIGN_RIGHT, 0);
    g_ui.set_limit_box = box;
}

static void build_bar(lv_obj_t *scr)
{
    lv_obj_t *bar = ui_make_box(scr);
    lv_obj_t *back;
    lv_obj_t *label;

    lv_obj_set_size(bar, LV_PCT(100), BAR_H);
    lv_obj_set_style_bg_color(bar, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, ui_color(g_ui.pal.line), 0);
    lv_obj_set_style_border_width(bar, g_ui.pal.dark ? 0 : 1, 0);

    /* The whole left half is the back button: easy to hit. */
    back = ui_make_box(bar);
    lv_obj_set_size(back, UI_WIDTH / 2, BAR_H);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    label = ui_make_label(back, &ui_font_16, g_ui.pal.text, "");
    lv_label_set_text_fmt(label, "%s   %s", SYM_PREV, ui_text(TXT_SETTINGS));
    lv_obj_align(label, LV_ALIGN_LEFT_MID, PAD + 4, 0);
}

void ui_settings_build(void)
{
    lv_obj_t *list;
    lv_obj_t *row;

    s_layout_map[0] = ui_text(TXT_LAYOUT_CARDS);
    s_layout_map[1] = ui_text(TXT_GRID_LIST);
    s_layout_map[2] = ui_text(TXT_LAYOUT_TILES);
    s_layout_map[3] = "";
    s_theme_map[0] = ui_text(TXT_THEME_DARK);
    s_theme_map[1] = ui_text(TXT_THEME_LIGHT);
    s_theme_map[2] = "";
    s_grid_map[0] = "2\xC3\x97" "2";
    s_grid_map[1] = "3\xC3\x97" "2";
    s_grid_map[2] = "3\xC3\x97" "3";
    s_grid_map[3] = ui_text(TXT_GRID_LIST);
    s_grid_map[4] = "";
    s_startup_map[0] = ui_text(TXT_STARTUP_LAST);
    s_startup_map[1] = ui_text(TXT_TAB_ALL);
    s_startup_map[2] = ui_text(TXT_STARTUP_FAV);
    s_startup_map[3] = "";
    s_timeout_map[0] = ui_text(TXT_TIMEOUT_NEVER);
    s_timeout_map[1] = ui_text(TXT_TIMEOUT_1);
    s_timeout_map[2] = ui_text(TXT_TIMEOUT_5);
    s_timeout_map[3] = ui_text(TXT_TIMEOUT_15);
    s_timeout_map[4] = "";
    s_startup_screen_map[0] = ui_text(TXT_DASH_TITLE);
    s_startup_screen_map[1] = ui_text(TXT_SCREEN_MUSIC);
    s_startup_screen_map[2] = ui_text(TXT_SCREEN_AUDIO);
    s_startup_screen_map[3] = "";

    init_styles();
    g_ui.settings_screen = ui_make_screen();
    build_bar(g_ui.settings_screen);

    list = ui_make_box(g_ui.settings_screen);
    lv_obj_set_pos(list, 0, BAR_H);
    lv_obj_set_size(list, UI_WIDTH, UI_HEIGHT - BAR_H);
    /* Clickable, so that a drag that starts on a label scrolls the list. */
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(list, ui_color(g_ui.pal.muted), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(list, PAD, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    g_ui.settings_list = list;

    make_section(list, TXT_SECTION_GENERAL);

    row = make_row(list, TXT_SET_THEME);
    g_ui.set_theme = make_segment(row, s_theme_map, 170, FIELD_THEME);

    row = make_row(list, TXT_SET_ACCENT);
    build_swatches(row);

    row = make_row(list, TXT_SET_LANGUAGE);
    g_ui.set_language = make_segment(row, (const char **)k_language_map, 190, FIELD_LANGUAGE);

    row = make_row(list, TXT_SET_TIMEOUT);
    g_ui.set_timeout = make_segment(row, s_timeout_map, 280, FIELD_TIMEOUT);

    row = make_row(list, TXT_SET_FLIP);
    g_ui.set_flip_switch = make_switch(row, on_flip_switch);

    row = make_row(list, TXT_SET_STARTUP_SCREEN);
    g_ui.set_startup_screen = make_segment(row, s_startup_screen_map, 260, FIELD_STARTUP_SCREEN);

    make_section(list, TXT_SECTION_AUDIO);

    row = make_row(list, TXT_SET_LAYOUT);
    g_ui.set_layout = make_segment(row, s_layout_map, 230, FIELD_LAYOUT);

    row = make_row(list, TXT_SET_GRID);
    g_ui.set_grid = make_segment(row, s_grid_map, 250, FIELD_GRID);

    row = make_row(list, TXT_SET_STARTUP);
    g_ui.set_startup = make_segment(row, s_startup_map, 270, FIELD_STARTUP);

    row = make_row(list, TXT_SET_LIMIT);
    g_ui.set_limit_switch = make_switch(row, on_limit_switch);
    build_limit_box(list);
}

/* ---------------------------------------------------------------- render */

static void check_segment(lv_obj_t *bm, uint32_t idx, uint32_t count)
{
    lv_buttonmatrix_clear_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (idx < count)
    {
        lv_buttonmatrix_set_button_ctrl(bm, idx, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
}

static void set_checked(lv_obj_t *obj, bool on)
{
    if (on)
    {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(obj, LV_STATE_CHECKED);
    }
}

void ui_settings_render(void)
{
    const UiSettings *s = &g_ui.settings;
    uint32_t timeout = sizeof(k_timeouts);

    if (g_ui.settings_screen == NULL)
    {
        return;
    }
    for (uint32_t i = 0U; i < sizeof(k_timeouts); i++)
    {
        if (k_timeouts[i] == s->screen_timeout)
        {
            timeout = i;
        }
    }
    check_segment(g_ui.set_layout, s->layout, UI_LAYOUT_COUNT);
    check_segment(g_ui.set_theme, s->theme, UI_THEME_COUNT);
    check_segment(g_ui.set_grid, s->grid, UI_GRID_COUNT);
    /* The grid applies to the cards layout only. lv_buttonmatrix draws each
     * button with its own ctrl flags, not the object's state. */
    if (s->layout != UI_LAYOUT_CARDS)
    {
        lv_buttonmatrix_set_button_ctrl_all(g_ui.set_grid, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
    else
    {
        lv_buttonmatrix_clear_button_ctrl_all(g_ui.set_grid, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
    check_segment(g_ui.set_startup, s->startup_tab, 3U);
    check_segment(g_ui.set_timeout, timeout, sizeof(k_timeouts)); /* another value: none marked */
    check_segment(g_ui.set_language, s->language, UI_LANGUAGE_COUNT);
    for (uint32_t i = 0U; i < UI_STARTUP_SCREEN_COUNT; i++)
    {
        if (k_startup_screens[i] == s->startup_screen)
        {
            check_segment(g_ui.set_startup_screen, i, UI_STARTUP_SCREEN_COUNT);
        }
    }

    for (uint32_t a = 0U; a < UI_ACCENT_COUNT; a++)
    {
        lv_obj_set_style_outline_width(g_ui.set_swatches[a], (a == s->accent) ? 2 : 0, 0);
    }

    set_checked(g_ui.set_limit_switch, s->volume_limit_on != 0U);
    if (s->volume_limit_on != 0U)
    {
        lv_obj_remove_flag(g_ui.set_limit_box, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(g_ui.set_limit_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (!lv_obj_has_state(g_ui.set_limit_slider, LV_STATE_PRESSED))
    {
        lv_slider_set_value(g_ui.set_limit_slider, s->volume_limit, LV_ANIM_OFF);
        lv_label_set_text_fmt(g_ui.set_limit_label, "%u%%", (unsigned)s->volume_limit);
    }

    set_checked(g_ui.set_flip_switch, s->flipped != 0U);
}
