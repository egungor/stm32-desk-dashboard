/**
 * @file    ui_dash.c
 * @brief   Dashboard tab (ADR-006, #14): clock strip, four cards, media
 *          strip, and the generic detail view.
 *
 *     ┌──────────────────────────────────────────┐
 *     │ 14:32  Çarşamba 21 Ekim                  │  clock strip (36 px)
 *     │ ┌ card 0 ─────────┐ ┌ card 1 ─────────┐  │
 *     │ │ TITLE     badge │ │                 │  │  2 x 2 cards
 *     │ │ line            │ │                 │  │
 *     │ └─────────────────┘ └─────────────────┘  │
 *     │ ┌ card 2 ─────────┐ ┌ card 3 ─────────┐  │
 *     │ └─────────────────┘ └─────────────────┘  │
 *     │        [ << ]    [ || ]    [ >> ]        │  media strip (44 px)
 *     └──────────────────────────────────────────┘
 *
 * The board knows nothing about weather or menus: a card and a detail are
 * text records from the host (protocol.md, "Record text"):
 *
 *   card:   icon ␟ title ␟ badge \n line \n line \n line
 *   detail: title ␟ subtitle ␟ flags \n rows ...
 *           row:     icon ␟ label ␟ value
 *           section: #title
 *           button:  !action ␟ label   (at most 3, at the bottom)
 *           flags:   "n" shows previous / next in the header
 *
 * The detail replaces the cards in the tab (the rail stays); it is built
 * when shown and deleted when closed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define STRIP_H     36
#define MEDIA_H     44
#define MEDIA_BTN_W 72
#define LABEL_W     112   /**< width of a detail row's label */
#define MAX_LINES   48U   /**< lines of a detail that are drawn */
#define MAX_BUTTONS 3U
#define SEP         '\x1F'

/* Working copy of a record, split in place. */
static char s_scratch[UI_DASH_DETAIL_BYTES + 1U];

/* Icon numbers of protocol.md ("Record text"); none yet beyond 0. */
static const char *icon_symbol(const char *field)
{
    LV_UNUSED(field);
    return NULL;
}

/* Split @p text (modified) at '\n' into at most @p max lines. */
static uint32_t split_lines(char *text, char **lines, uint32_t max)
{
    uint32_t n = 0U;
    char *p = text;

    while ((n < max) && (p != NULL))
    {
        char *nl = strchr(p, '\n');

        lines[n++] = p;
        if (nl != NULL)
        {
            *nl = '\0';
            p = nl + 1;
        }
        else
        {
            p = NULL;
        }
    }
    return n;
}

/* Split one line (modified) at the field separator; missing fields are "". */
static void split_fields(char *line, char **fields, uint32_t count)
{
    static char empty[1] = "";
    char *p = line;

    for (uint32_t i = 0U; i < count; i++)
    {
        if (p == NULL)
        {
            fields[i] = empty;
            continue;
        }
        fields[i] = p;
        p = strchr(p, SEP);
        if (p != NULL)
        {
            *p = '\0';
            p++;
        }
    }
}

/* -------------------------------------------------------------- clock */

/* Days since 1970-01-01 -> year, month, day (Howard Hinnant's algorithm). */
static void civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d)
{
    const int32_t zz = z + 719468;
    const int32_t era = ((zz >= 0) ? zz : (zz - 146096)) / 146097;
    const uint32_t doe = (uint32_t)(zz - (era * 146097));
    const uint32_t yoe = (doe - (doe / 1460U) + (doe / 36524U) - (doe / 146096U)) / 365U;
    const uint32_t doy = doe - ((365U * yoe) + (yoe / 4U) - (yoe / 100U));
    const uint32_t mp = ((5U * doy) + 2U) / 153U;

    *d = doy - (((153U * mp) + 2U) / 5U) + 1U;
    *m = (mp < 10U) ? (mp + 3U) : (mp - 9U);
    *y = (int32_t)yoe + (era * 400) + ((*m <= 2U) ? 1 : 0);
}

void ui_dash_clock_tick(void)
{
    uint32_t now;
    uint32_t minute;
    uint32_t days;
    int32_t year;
    uint32_t month;
    uint32_t day;

    if ((g_ui.clock_label == NULL) || (g_ui.date_label == NULL))
    {
        return;
    }
    if (!g_ui.time_known)
    {
        lv_label_set_text_static(g_ui.clock_label, "--:--");
        lv_label_set_text_static(g_ui.date_label, "");
        g_ui.clock_shown = UINT32_MAX;
        return;
    }
    now = g_ui.time_base + (lv_tick_elaps(g_ui.time_tick) / 1000U);
    minute = now / 60U;
    if (minute == g_ui.clock_shown)
    {
        return;
    }
    g_ui.clock_shown = minute;
    days = now / 86400U;
    civil_from_days((int32_t)days, &year, &month, &day);
    LV_UNUSED(year);
    lv_label_set_text_fmt(g_ui.clock_label, "%02u:%02u", (unsigned)((now / 3600U) % 24U),
                          (unsigned)((now / 60U) % 60U));
    /* 1970-01-01 was a Thursday (TXT_DAY_4). */
    lv_label_set_text_fmt(g_ui.date_label, "%s %u %s", ui_text((UiTextId)(TXT_DAY_0 + ((days + 4U) % 7U))),
                          (unsigned)day, ui_text((UiTextId)(TXT_MONTH_1 + (month - 1U))));
}

/* --------------------------------------------------------------- cards */

static void render_card(uint32_t slot)
{
    lv_obj_t *card = g_ui.dash_card_objs[slot];
    char *lines[4];
    char *head[3];
    uint32_t n;
    lv_obj_t *top;
    lv_obj_t *label;
    const char *icon;

    if (card == NULL)
    {
        return;
    }
    lv_obj_clean(card);
    if (g_ui.dash_cards[slot][0] == '\0')
    {
        lv_obj_set_style_bg_opa(card, LV_OPA_40, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        return;
    }
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    (void)memcpy(s_scratch, g_ui.dash_cards[slot], sizeof(g_ui.dash_cards[slot]));
    n = split_lines(s_scratch, lines, 4U);
    split_fields(lines[0], head, 3U);

    top = ui_make_box(card);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 6, 0);
    icon = icon_symbol(head[0]);
    if (icon != NULL)
    {
        (void)ui_make_label(top, &ui_font_14, g_ui.pal.muted, icon);
    }
    label = ui_make_label(top, &ui_font_14, g_ui.pal.muted, head[1]);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(label, lv_font_get_line_height(&ui_font_14));
    if (head[2][0] != '\0')
    {
        (void)ui_make_label(top, &ui_font_14, g_ui.pal.accent, head[2]);
    }

    for (uint32_t i = 1U; i < n; i++)
    {
        const lv_font_t *font = (i == 1U) ? &ui_font_16 : &ui_font_14;

        label = ui_make_label(card, font, (i == 1U) ? g_ui.pal.text : g_ui.pal.muted, lines[i]);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_height(label, lv_font_get_line_height(font));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    }
}

/* --------------------------------------------------------------- detail */

static void show_view(uint32_t view);

static void on_back(lv_event_t *e)
{
    LV_UNUSED(e);
    show_view(UI_DASH_VIEW_CARDS);
    g_ui.dash_slot = UI_DASH_NO_SLOT;
    if (g_ui.dash.close_detail != NULL)
    {
        g_ui.dash.close_detail(g_ui.dash_user);
    }
}

static void on_nav(lv_event_t *e)
{
    const int8_t arg = (lv_event_get_user_data(e) != NULL) ? 1 : -1;

    if (g_ui.dash_slot == UI_DASH_NO_SLOT)
    {
        return;
    }
    show_view(UI_DASH_VIEW_LOADING);
    if (g_ui.dash.open_detail != NULL)
    {
        g_ui.dash.open_detail(g_ui.dash_slot, arg, g_ui.dash_user);
    }
}

static void on_detail_button(lv_event_t *e)
{
    const uint8_t action = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    if ((g_ui.dash.action != NULL) && (g_ui.dash_slot != UI_DASH_NO_SLOT))
    {
        g_ui.dash.action(g_ui.dash_slot, action, g_ui.dash_user);
    }
}

static lv_obj_t *make_header_button(lv_obj_t *bar, const char *symbol, lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = ui_make_box(bar);

    lv_obj_set_size(btn, 40, STRIP_H);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    lv_obj_center(ui_make_label(btn, &ui_font_16, g_ui.pal.text, symbol));
    return btn;
}

/* Header: back, title and subtitle, previous / next when asked for. */
static void build_detail_header(lv_obj_t *view, const char *title, const char *subtitle, bool nav)
{
    lv_obj_t *bar = ui_make_box(view);
    lv_obj_t *texts;
    lv_obj_t *label;

    lv_obj_set_size(bar, LV_PCT(100), STRIP_H);
    lv_obj_set_style_bg_color(bar, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    (void)make_header_button(bar, SYM_PREV, on_back, NULL);
    texts = ui_make_box(bar);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(texts, 8, 0);
    label = ui_make_label(texts, &ui_font_16, g_ui.pal.text, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    label = ui_make_label(texts, &ui_font_14, g_ui.pal.muted, subtitle);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_height(label, lv_font_get_line_height(&ui_font_14));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    if (nav)
    {
        (void)make_header_button(bar, SYM_PREV, on_nav, NULL);
        (void)make_header_button(bar, SYM_NEXT, on_nav, (void *)1);
    }
}

static void add_row(lv_obj_t *list, char *line)
{
    char *f[3];
    lv_obj_t *row;
    lv_obj_t *label;
    const char *icon;

    split_fields(line, f, 3U);
    row = ui_make_box(list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 3, 0);
    icon = icon_symbol(f[0]);
    if (icon != NULL)
    {
        (void)ui_make_label(row, &ui_font_16, g_ui.pal.muted, icon);
    }
    if (f[1][0] != '\0')
    {
        label = ui_make_label(row, &ui_font_14, g_ui.pal.muted, f[1]);
        lv_obj_set_width(label, LABEL_W);
        lv_obj_set_style_pad_top(label, 2, 0);
    }
    label = ui_make_label(row, &ui_font_16, g_ui.pal.text, f[2]);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
}

static void add_section(lv_obj_t *list, const char *title)
{
    lv_obj_t *label = ui_make_label(list, &ui_font_14, g_ui.pal.accent, title);

    lv_obj_set_style_pad_top(label, 8, 0);
    lv_obj_set_style_pad_bottom(label, 2, 0);
}

static void build_detail_body(lv_obj_t *view, char **lines, uint32_t n)
{
    char *buttons[MAX_BUTTONS];
    uint32_t button_count = 0U;
    lv_obj_t *list;

    for (uint32_t i = 1U; i < n; i++)
    {
        if ((lines[i][0] == '!') && (button_count < MAX_BUTTONS))
        {
            buttons[button_count++] = &lines[i][1];
        }
    }

    list = ui_make_box(view);
    lv_obj_set_pos(list, 0, STRIP_H);
    lv_obj_set_size(list, LV_PCT(100), UI_HEIGHT - STRIP_H - ((button_count > 0U) ? MEDIA_H : 0));
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_hor(list, 12, 0);
    lv_obj_set_style_pad_ver(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    for (uint32_t i = 1U; i < n; i++)
    {
        if (lines[i][0] == '#')
        {
            add_section(list, &lines[i][1]);
        }
        else if (lines[i][0] != '!')
        {
            add_row(list, lines[i]);
        }
        else
        {
            /* a button, below */
        }
    }

    if (button_count > 0U)
    {
        lv_obj_t *strip = ui_make_box(view);

        lv_obj_set_size(strip, LV_PCT(100), MEDIA_H);
        lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        for (uint32_t b = 0U; b < button_count; b++)
        {
            char *f[2];
            long action;
            lv_obj_t *btn;
            lv_obj_t *label;

            split_fields(buttons[b], f, 2U);
            action = strtol(f[0], NULL, 10);
            if ((action < 1) || (action > 255))
            {
                continue;
            }
            btn = lv_button_create(strip);
            ui_style_card(btn, 8);
            lv_obj_set_height(btn, MEDIA_H - 8);
            lv_obj_set_style_pad_hor(btn, 14, 0);
            lv_obj_add_event_cb(btn, on_detail_button, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
            label = ui_make_label(btn, &ui_font_16, g_ui.pal.text, f[1]);
            lv_obj_center(label);
        }
    }
}

/* Build the detail view for the current state: "loading" with the card's
 * title, or the kept detail text. */
static void build_detail_view(void)
{
    char *lines[MAX_LINES];
    char *head[3];
    uint32_t n;
    lv_obj_t *view;

    if (g_ui.dash_detail_view != NULL)
    {
        lv_obj_delete(g_ui.dash_detail_view);
    }
    view = ui_make_box(g_ui.dash_tab);
    lv_obj_set_size(view, LV_PCT(100), LV_PCT(100));
    g_ui.dash_detail_view = view;

    if (g_ui.dash_view == UI_DASH_VIEW_LOADING)
    {
        /* The card's title until the detail arrives. */
        char *card_lines[1];
        lv_obj_t *label;

        (void)memcpy(s_scratch, g_ui.dash_cards[g_ui.dash_slot], sizeof(g_ui.dash_cards[0]));
        (void)split_lines(s_scratch, card_lines, 1U);
        split_fields(card_lines[0], head, 3U);
        build_detail_header(view, head[1], "", false);
        label = ui_make_label(view, &ui_font_16, g_ui.pal.muted, ui_text(TXT_LOADING));
        lv_obj_center(label);
        return;
    }

    (void)memcpy(s_scratch, g_ui.dash_detail, sizeof(g_ui.dash_detail));
    n = split_lines(s_scratch, lines, MAX_LINES);
    split_fields(lines[0], head, 3U);
    build_detail_header(view, head[0], head[1], strchr(head[2], 'n') != NULL);
    build_detail_body(view, lines, n);
}

static void show_view(uint32_t view)
{
    g_ui.dash_view = view;
    if (view == UI_DASH_VIEW_CARDS)
    {
        if (g_ui.dash_detail_view != NULL)
        {
            /* May run inside an event of the view's back button. */
            lv_obj_delete_async(g_ui.dash_detail_view);
            g_ui.dash_detail_view = NULL;
        }
        if (g_ui.dash_cards_view != NULL)
        {
            lv_obj_remove_flag(g_ui.dash_cards_view, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (g_ui.dash_cards_view != NULL)
    {
        lv_obj_add_flag(g_ui.dash_cards_view, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui.dash_detail_view != NULL)
    {
        /* Replaced from inside one of its events (previous / next). */
        lv_obj_delete_async(g_ui.dash_detail_view);
        g_ui.dash_detail_view = NULL;
    }
    build_detail_view();
}

static void on_card(lv_event_t *e)
{
    const uint32_t slot = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (g_ui.dash_cards[slot][0] == '\0')
    {
        return;
    }
    g_ui.dash_slot = (uint8_t)slot;
    show_view(UI_DASH_VIEW_LOADING);
    if (g_ui.dash.open_detail != NULL)
    {
        g_ui.dash.open_detail((uint8_t)slot, 0, g_ui.dash_user);
    }
}

static void on_media(lv_event_t *e)
{
    const uint8_t action = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    if (g_ui.dash.media != NULL)
    {
        g_ui.dash.media(action, g_ui.dash_user);
    }
}

/* ----------------------------------------------------------------- build */

static void build_clock_strip(lv_obj_t *parent)
{
    lv_obj_t *strip = ui_make_box(parent);

    lv_obj_set_size(strip, LV_PCT(100), STRIP_H);
    lv_obj_set_style_pad_hor(strip, 12, 0);
    lv_obj_set_style_pad_column(strip, 10, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_ui.clock_label = ui_make_label(strip, &ui_font_22, g_ui.pal.text, "--:--");
    g_ui.date_label = ui_make_label(strip, &ui_font_14, g_ui.pal.muted, "");
    g_ui.clock_shown = UINT32_MAX;
    ui_dash_clock_tick();
}

static void build_cards(lv_obj_t *parent)
{
    static const int32_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const int32_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_t *grid = ui_make_box(parent);

    lv_obj_set_pos(grid, 0, STRIP_H);
    lv_obj_set_size(grid, LV_PCT(100), UI_HEIGHT - STRIP_H - MEDIA_H);
    lv_obj_set_style_pad_hor(grid, PAD, 0);
    lv_obj_set_style_pad_gap(grid, PAD, 0);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    for (uint32_t slot = 0U; slot < UI_DASH_CARDS; slot++)
    {
        lv_obj_t *card = ui_make_box(grid);

        ui_style_card(card, BTN_RADIUS);
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, (int32_t)(slot % 2U), 1, LV_GRID_ALIGN_STRETCH,
                             (int32_t)(slot / 2U), 1);
        lv_obj_set_style_pad_hor(card, 10, 0);
        lv_obj_set_style_pad_ver(card, 8, 0);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(card, on_card, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);
        g_ui.dash_card_objs[slot] = card;
        render_card(slot);
    }
}

static void build_media_strip(lv_obj_t *parent)
{
    static const char *const symbols[3] = { SYM_PREV SYM_PREV, "| |", SYM_NEXT SYM_NEXT };
    lv_obj_t *strip = ui_make_box(parent);

    lv_obj_set_size(strip, LV_PCT(100), MEDIA_H);
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_column(strip, 24, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        lv_obj_t *btn = ui_make_small_button(strip, MEDIA_BTN_W, MEDIA_H - 10, symbols[i], NULL);

        lv_obj_add_event_cb(btn, on_media, LV_EVENT_CLICKED, (void *)(uintptr_t)(UI_MEDIA_PREVIOUS + i));
    }
}

void ui_dash_build(void)
{
    lv_obj_t *view = ui_make_box(g_ui.dash_tab);

    lv_obj_set_size(view, LV_PCT(100), LV_PCT(100));
    g_ui.dash_cards_view = view;
    g_ui.dash_detail_view = NULL;
    build_clock_strip(view);
    build_cards(view);
    build_media_strip(view);
    if (g_ui.dash_view != UI_DASH_VIEW_CARDS)
    {
        show_view(g_ui.dash_view);
    }
}

/* ------------------------------------------------------------------- API */

void Ui_SetDashHandlers(const UiDashHandlers *handlers, void *user)
{
    if (handlers != NULL)
    {
        g_ui.dash = *handlers;
    }
    else
    {
        (void)memset(&g_ui.dash, 0, sizeof(g_ui.dash));
    }
    g_ui.dash_user = user;
}

void Ui_SetCard(uint8_t slot, const char *text)
{
    if (slot >= UI_DASH_CARDS)
    {
        return;
    }
    (void)snprintf(g_ui.dash_cards[slot], sizeof(g_ui.dash_cards[slot]), "%s", (text != NULL) ? text : "");
    render_card(slot);
}

void Ui_ShowDetail(uint8_t slot, const char *text)
{
    if ((slot != g_ui.dash_slot) || (g_ui.dash_view == UI_DASH_VIEW_CARDS))
    {
        return;
    }
    (void)snprintf(g_ui.dash_detail, sizeof(g_ui.dash_detail), "%s", (text != NULL) ? text : "");
    show_view(UI_DASH_VIEW_DETAIL);
}

void Ui_DetailFailed(void)
{
    if (g_ui.dash_view != UI_DASH_VIEW_CARDS)
    {
        g_ui.dash_slot = UI_DASH_NO_SLOT;
        show_view(UI_DASH_VIEW_CARDS);
    }
    Ui_ShowNotice(UI_NOTICE_DETAIL_FAILED);
}

void Ui_SetTime(uint32_t local_time)
{
    g_ui.time_known = (local_time != 0U);
    g_ui.time_base = local_time;
    g_ui.time_tick = lv_tick_get();
    g_ui.clock_shown = UINT32_MAX;
    ui_dash_clock_tick();
}

uint32_t Ui_DashView(void)
{
    return g_ui.dash_view;
}
