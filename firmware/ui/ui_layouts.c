/**
 * @file    ui_layouts.c
 * @brief   Audio tab content of layouts B (list) and C (large tile) (#60;
 *          designs from #57; in the 424 px tab since #10). Layout A
 *          (cards) is in ui.c.
 *
 * Both fill the same UiState objects as layout A (device buttons, tabs,
 * pager, volume slider, mute button), so ui.c renders all three. They
 * differ in what a device button shows (CardKind) and where things sit:
 *
 *   B: layout A's top bar (all, favorites, edit), four rows with the
 *      Windows name under an alias, pages below them, vertical volume on
 *      the right. (Before #10 B had its own rail; the application rail
 *      took its place, ADR-005.)
 *   C: the active device in a large panel on the left with its volume;
 *      the other devices as six small tiles on the right, tabs as pills
 *      and the edit button above them, pages below.
 *
 * Settings are opened from the application rail in both.
 *
 * The mockups scroll the list (B) and show page dots (C); here both use
 * the same page buttons as layout A, which need no extra objects per
 * device and work the same way with a finger.
 */
#include "ui_internal.h"

#define VOL_W      64
#define RAIL_BTN   44
#define LIST_ROWS  4U
#define ROW_H      40
#define ROW_GAP    4
#define PAGER_H    28
#define HERO_W     176
#define TILE_COLS  2U
#define TILE_ROWS  3U

/* A panel in the bar colour. */
static lv_obj_t *make_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *panel = ui_make_box(parent);

    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, radius, 0);
    return panel;
}

/* A square icon button (rail, pills row). */
static lv_obj_t *make_icon_button(lv_obj_t *parent, int32_t size, const char *symbol, lv_event_cb_t cb,
                                  void *user, lv_obj_t **label)
{
    lv_obj_t *btn = ui_make_box(parent);
    lv_obj_t *l;

    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, ui_color(g_ui.pal.btn), 0);
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

/* Page buttons and "n/m" centred in a strip of the given width. */
static void build_pager(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    g_ui.pager = ui_make_box(parent);
    lv_obj_set_pos(g_ui.pager, x, y);
    lv_obj_set_size(g_ui.pager, w, PAGER_H);
    lv_obj_set_style_pad_column(g_ui.pager, 10, 0);
    lv_obj_set_flex_flow(g_ui.pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_ui.pager, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_ui.prev_btn = ui_make_small_button(g_ui.pager, 44, PAGER_H, SYM_PREV, NULL);
    g_ui.page_label = ui_make_label(g_ui.pager, &ui_font_16, g_ui.pal.muted, "");
    g_ui.next_btn = ui_make_small_button(g_ui.pager, 44, PAGER_H, SYM_NEXT, NULL);
    lv_obj_add_event_cb(g_ui.prev_btn, ui_on_page_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_ui.next_btn, ui_on_page_clicked, LV_EVENT_CLICKED, (void *)g_ui.next_btn);
}

/* ------------------------------------------------------------- B: list */

static void build_list_row(lv_obj_t *scr, uint32_t slot, int32_t x, int32_t w)
{
    UiDeviceButton *b = &g_ui.buttons[slot];
    lv_obj_t *text;

    b->btn = lv_button_create(scr);
    ui_style_card(b->btn, 10);
    lv_obj_set_pos(b->btn, x, BAR_H + PAD + ((int32_t)slot * (ROW_H + ROW_GAP)));
    lv_obj_set_size(b->btn, w, ROW_H);
    lv_obj_set_style_pad_hor(b->btn, 10, 0);
    lv_obj_set_style_pad_ver(b->btn, 0, 0);
    lv_obj_set_style_pad_column(b->btn, 8, 0);
    lv_obj_set_flex_flow(b->btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b->btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(b->btn, ui_on_device_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);

    b->icon = ui_make_label(b->btn, &ui_font_16, g_ui.pal.muted, "");
    lv_obj_set_width(b->icon, 22);
    lv_obj_set_style_text_align(b->icon, LV_TEXT_ALIGN_CENTER, 0);

    text = ui_make_box(b->btn);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    b->name = ui_make_label(text, &ui_font_16, g_ui.pal.text, "");
    lv_obj_set_width(b->name, LV_PCT(100));
    lv_obj_set_height(b->name, lv_font_get_line_height(&ui_font_16));
    lv_label_set_long_mode(b->name, LV_LABEL_LONG_MODE_DOTS);
    b->sub = ui_make_label(text, &ui_font_14, g_ui.pal.muted, "");
    lv_obj_set_width(b->sub, LV_PCT(100));
    lv_obj_set_height(b->sub, lv_font_get_line_height(&ui_font_14));
    lv_label_set_long_mode(b->sub, LV_LABEL_LONG_MODE_DOTS);

    b->star = ui_make_label(b->btn, &ui_font_14, g_ui.pal.accent, SYM_STAR);
    b->state = ui_make_label(b->btn, &ui_font_16, g_ui.pal.on_acc, "");
}

void ui_build_list_main(void)
{
    const int32_t x = PAD;
    const int32_t w = UI_CONTENT_W - VOL_W - (2 * PAD);
    lv_obj_t *const tab = g_ui.audio_tab;
    lv_obj_t *vol;

    g_ui.card_kind = CARD_LIST;
    g_ui.per_page = LIST_ROWS;

    /* Sub-tabs and edit as in layout A, over the rows only. */
    ui_build_tab_bar(tab, UI_CONTENT_W - VOL_W);

    for (uint32_t slot = 0U; slot < LIST_ROWS; slot++)
    {
        build_list_row(tab, slot, x, w);
    }
    g_ui.empty_label = ui_make_label(tab, &ui_font_16, g_ui.pal.muted, "");
    lv_obj_align(g_ui.empty_label, LV_ALIGN_CENTER, -VOL_W / 2, 0);
    build_pager(tab, x, UI_HEIGHT - PAGER_H - 6, w);

    /* Vertical volume: percentage, slider, mute button. */
    vol = make_panel(tab, UI_CONTENT_W - VOL_W, 0, VOL_W, UI_HEIGHT, 0);
    lv_obj_set_style_pad_ver(vol, 10, 0);
    lv_obj_set_style_pad_row(vol, 12, 0);
    lv_obj_set_flex_flow(vol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_ui.volume_label = ui_make_label(vol, &ui_font_16, g_ui.pal.text, "--");
    g_ui.slider = lv_slider_create(vol);
    lv_obj_set_width(g_ui.slider, 8);
    lv_obj_set_flex_grow(g_ui.slider, 1);
    ui_style_volume_slider(g_ui.slider);
    g_ui.mute_btn = ui_make_small_button(vol, RAIL_BTN, 40, SYM_VOLUME, &g_ui.mute_icon);
    lv_obj_add_event_cb(g_ui.mute_btn, ui_on_mute_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_margin_top(g_ui.mute_btn, VOLUME_GAP - 12, 0);  /* 12: the row gap */
}

/* -------------------------------------------------------- C: large tile */

static void build_tile(lv_obj_t *scr, uint32_t slot, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    UiDeviceButton *b = &g_ui.buttons[slot];
    lv_obj_t *top;

    b->btn = lv_button_create(scr);
    ui_style_card(b->btn, BTN_RADIUS);
    lv_obj_set_pos(b->btn, x0 + ((int32_t)(slot % TILE_COLS) * (w + PAD)),
                   y0 + ((int32_t)(slot / TILE_COLS) * (h + PAD)));
    lv_obj_set_size(b->btn, w, h);
    lv_obj_set_style_pad_all(b->btn, 8, 0);
    lv_obj_set_style_pad_row(b->btn, 4, 0);
    lv_obj_set_flex_flow(b->btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b->btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(b->btn, ui_on_device_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);

    top = ui_make_box(b->btn);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    b->icon = ui_make_label(top, &ui_font_16, g_ui.pal.muted, "");
    b->star = ui_make_label(top, &ui_font_14, g_ui.pal.accent, SYM_STAR);

    b->name = ui_make_label(b->btn, &ui_font_16, g_ui.pal.text, "");
    lv_obj_set_width(b->name, LV_PCT(100));
    lv_obj_set_height(b->name, lv_font_get_line_height(&ui_font_16));
    lv_label_set_long_mode(b->name, LV_LABEL_LONG_MODE_DOTS);
}

static void build_hero(lv_obj_t *scr)
{
    lv_obj_t *hero = make_panel(scr, PAD, PAD, HERO_W, UI_HEIGHT - (2 * PAD), 14);
    lv_obj_t *spacer;
    lv_obj_t *row;
    lv_obj_t *label;

    lv_obj_set_style_pad_all(hero, 12, 0);
    lv_obj_set_style_pad_row(hero, 6, 0);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);

    (void)ui_make_label(hero, &ui_font_14, g_ui.pal.muted, ui_text(TXT_HERO_TITLE));
    g_ui.hero_icon = ui_make_label(hero, &ui_icons_40, g_ui.pal.accent, "");
    g_ui.hero_name = ui_make_label(hero, &ui_font_22, g_ui.pal.text, "");
    lv_obj_set_width(g_ui.hero_name, LV_PCT(100));
    lv_obj_set_height(g_ui.hero_name, 2 * lv_font_get_line_height(&ui_font_22));
    lv_label_set_long_mode(g_ui.hero_name, LV_LABEL_LONG_MODE_DOTS);
    g_ui.hero_sub = ui_make_label(hero, &ui_font_14, g_ui.pal.muted, "");
    lv_obj_set_width(g_ui.hero_sub, LV_PCT(100));
    /* One line: a fixed height makes LVGL cut a long Windows name with
     * dots instead of wrapping it over the volume row. */
    lv_obj_set_height(g_ui.hero_sub, lv_font_get_line_height(&ui_font_14));
    lv_label_set_long_mode(g_ui.hero_sub, LV_LABEL_LONG_MODE_DOTS);

    spacer = ui_make_box(hero);
    lv_obj_set_flex_grow(spacer, 1);

    row = ui_make_box(hero);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    g_ui.volume_label = ui_make_label(row, &ui_font_22, g_ui.pal.text, "--");
    label = ui_make_label(row, &ui_font_14, g_ui.pal.muted, ui_text(TXT_HERO_VOLUME));
    LV_UNUSED(label);

    row = ui_make_box(hero);
    lv_obj_set_size(row, LV_PCT(100), 36);
    lv_obj_set_style_pad_column(row, VOLUME_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_ui.mute_btn = ui_make_small_button(row, 40, 36, SYM_VOLUME, &g_ui.mute_icon);
    lv_obj_add_event_cb(g_ui.mute_btn, ui_on_mute_clicked, LV_EVENT_CLICKED, NULL);
    g_ui.slider = lv_slider_create(row);
    lv_obj_set_flex_grow(g_ui.slider, 1);
    lv_obj_set_height(g_ui.slider, 8);
    ui_style_volume_slider(g_ui.slider);
}

void ui_build_tiles_main(void)
{
    static const UiTextId tab_texts[2] = { TXT_TAB_ALL, TXT_STARTUP_FAV };
    const int32_t x0 = PAD + HERO_W + PAD;
    const int32_t w_all = UI_CONTENT_W - x0 - PAD;
    const int32_t tile_w = (w_all - PAD) / (int32_t)TILE_COLS;
    const int32_t tile_y = 52;
    const int32_t tile_h = (UI_HEIGHT - tile_y - PAGER_H - 8 - ((int32_t)(TILE_ROWS - 1U) * PAD)) / (int32_t)TILE_ROWS;
    lv_obj_t *scr;
    lv_obj_t *top;
    lv_obj_t *spacer;
    lv_obj_t *btn;

    g_ui.card_kind = CARD_TILE;
    g_ui.per_page = TILE_COLS * TILE_ROWS;
    scr = g_ui.audio_tab;

    build_hero(scr);

    /* Tabs as pills (favorites as a star only, to fit), then the edit button. */
    top = ui_make_box(scr);
    lv_obj_set_pos(top, x0, PAD);
    lv_obj_set_size(top, w_all, 36);
    lv_obj_set_style_pad_column(top, 6, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint32_t t = 0U; t < 2U; t++)
    {
        btn = ui_make_box(top);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, 34);
        lv_obj_set_style_pad_hor(btn, 12, 0);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, ui_on_tab_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)t);
        g_ui.tab_btns[t] = btn;
        g_ui.tab_labels[t] = ui_make_label(btn, &ui_font_16, g_ui.pal.muted, "");
        if (t == 1U)
        {
            lv_label_set_text_static(g_ui.tab_labels[t], SYM_STAR);
        }
        else
        {
            lv_label_set_text(g_ui.tab_labels[t], ui_text(tab_texts[t]));
        }
        lv_obj_center(g_ui.tab_labels[t]);
    }
    spacer = ui_make_box(top);
    lv_obj_set_flex_grow(spacer, 1);
    g_ui.edit_btn = make_icon_button(top, 34, SYM_PENCIL, ui_on_edit_clicked, NULL, NULL);
    if (!g_ui.edit_set)
    {
        lv_obj_add_flag(g_ui.edit_btn, LV_OBJ_FLAG_HIDDEN);
    }

    for (uint32_t slot = 0U; slot < (TILE_COLS * TILE_ROWS); slot++)
    {
        build_tile(scr, slot, x0, tile_y, tile_w, tile_h);
    }
    g_ui.empty_label = ui_make_label(scr, &ui_font_16, g_ui.pal.muted, "");
    lv_obj_align(g_ui.empty_label, LV_ALIGN_TOP_LEFT, x0 + 8, tile_y + 20);
    build_pager(scr, x0, UI_HEIGHT - PAGER_H - 4, w_all);
}
