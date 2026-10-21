/**
 * @file    ui_shell.c
 * @brief   Application screen: rail and tabs (ADR-005, #10).
 *
 *     ┌────┬──────────────────────────────────┐
 *     │ ▦  │                                  │
 *     │ ♪  │   the shown tab, 424 x 272        │
 *     │    │                                  │
 *     │ ⚙  │                                  │
 *     │ ⇄  │                                  │
 *     └────┴──────────────────────────────────┘
 *
 * The rail holds the tab buttons (Dashboard, Audio), the settings button
 * and the connection mark. Both tabs are built once (with the other
 * screens) and the one not shown is hidden, so its state (page, sub-tab)
 * survives a switch.
 */
#include "ui_internal.h"

#define RAIL_BTN 44

static void on_rail_tab(lv_event_t *e)
{
    ui_shell_show_tab((uint32_t)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *make_rail_button(lv_obj_t *rail, const char *symbol, lv_event_cb_t cb, void *user,
                                  lv_obj_t **label)
{
    lv_obj_t *btn = ui_make_box(rail);
    lv_obj_t *l;

    lv_obj_set_size(btn, RAIL_BTN, RAIL_BTN);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, ui_color(g_ui.pal.accent), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    l = ui_make_label(btn, &ui_font_16, g_ui.pal.muted, symbol);
    lv_obj_center(l);
    if (label != NULL)
    {
        *label = l;
    }
    return btn;
}

static lv_obj_t *make_tab(lv_obj_t *scr)
{
    lv_obj_t *tab = ui_make_box(scr);

    lv_obj_set_pos(tab, SHELL_RAIL_W, 0);
    lv_obj_set_size(tab, UI_CONTENT_W, UI_HEIGHT);
    return tab;
}

void ui_shell_build(void)
{
    static const char *const tab_symbols[UI_APP_TAB_COUNT] = { SYM_GRID, SYM_VOLUME };
    lv_obj_t *scr;
    lv_obj_t *rail;
    lv_obj_t *spacer;
    lv_obj_t *plug;

    g_ui.main_screen = ui_make_screen();
    scr = g_ui.main_screen;

    rail = ui_make_box(scr);
    lv_obj_set_size(rail, SHELL_RAIL_W, UI_HEIGHT);
    lv_obj_set_style_bg_color(rail, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(rail, ui_color(g_ui.pal.line), 0);
    lv_obj_set_style_border_width(rail, g_ui.pal.dark ? 0 : 1, 0);
    lv_obj_set_style_pad_ver(rail, PAD, 0);
    lv_obj_set_style_pad_row(rail, PAD, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint32_t t = 0U; t < UI_APP_TAB_COUNT; t++)
    {
        g_ui.rail_btns[t] = make_rail_button(rail, tab_symbols[t], on_rail_tab, (void *)(uintptr_t)t,
                                             &g_ui.rail_labels[t]);
    }
    spacer = ui_make_box(rail);
    lv_obj_set_width(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);
    (void)make_rail_button(rail, SYM_COG, ui_on_settings_clicked, NULL, NULL);
    plug = ui_make_label(rail, &ui_font_16, g_ui.pal.link, SYM_PLUG);
    lv_obj_set_style_pad_bottom(plug, 4, 0);

    g_ui.dash_tab = make_tab(scr);
    ui_dash_build();
    g_ui.audio_tab = make_tab(scr);
}

void ui_shell_show_tab(uint32_t tab)
{
    if (tab >= UI_APP_TAB_COUNT)
    {
        return;
    }
    g_ui.app_tab = tab;
    if (g_ui.main_screen == NULL)
    {
        return;
    }
    for (uint32_t t = 0U; t < UI_APP_TAB_COUNT; t++)
    {
        const bool on = (t == tab);

        lv_obj_set_style_bg_opa(g_ui.rail_btns[t], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(g_ui.rail_labels[t], ui_color(on ? g_ui.pal.on_acc : g_ui.pal.muted), 0);
    }
    if (tab == UI_APP_TAB_DASHBOARD)
    {
        lv_obj_remove_flag(g_ui.dash_tab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.audio_tab, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(g_ui.dash_tab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_ui.audio_tab, LV_OBJ_FLAG_HIDDEN);
    }
}

uint32_t Ui_CurrentAppTab(void)
{
    return g_ui.app_tab;
}
