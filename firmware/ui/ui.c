/**
 * @file    ui.c
 * @brief   Touch UI of the desk dashboard (LVGL, hardware independent):
 *          waiting screen, the Audio tab's content (layout A; B and C in
 *          ui_layouts.c), notices, screen timeout and the API. The
 *          application screen with its rail is in ui_shell.c.
 *
 * Screens are built from g_ui.settings (theme, accent, language, grid) and
 * rebuilt when one of these changes; everything else updates the objects
 * in place.
 */
#include "ui.h"

#include <string.h>

#include "ui_internal.h"

UiState g_ui;

/* Grid shapes by UI_GRID_*: columns x rows, or a list of rows. */
typedef struct
{
    uint8_t cols;
    uint8_t rows;
    bool    list;
} GridShape;

static const GridShape k_grids[UI_GRID_COUNT] = {
    { 2U, 2U, false }, /* 2x2: 4 large cards */
    { 3U, 2U, false }, /* 3x2: 6 cards */
    { 3U, 3U, false }, /* 3x3: 9 small cards */
    { 1U, 5U, true },  /* list: 5 rows */
};

/* How much a card of layout A shows, by grid. */
static CardKind card_kind(void)
{
    switch (g_ui.settings.grid)
    {
    case UI_GRID_3X2:
        return CARD_MEDIUM;
    case UI_GRID_3X3:
        return CARD_SMALL;
    case UI_GRID_LIST:
        return CARD_ROW;
    default:
        return CARD_LARGE;
    }
}

const char *ui_icon_symbol(uint8_t icon)
{
    static const char *const symbols[] = {
        "\xEF\x80\x81", /* generic: music */
        "\xEF\x80\xA8", /* speakers: volume-up */
        "\xEF\x80\xA5", /* headphones */
        "\xEF\x96\x90", /* headset */
        "\xEF\x84\x88", /* display: desktop */
        "\xEF\x8A\x87", /* digital output: usb */
    };

    return (icon < sizeof(symbols) / sizeof(symbols[0])) ? symbols[icon] : symbols[0];
}

const char *ui_display_name(const UiDevice *d)
{
    return (d->alias[0] != '\0') ? d->alias : d->name;
}

static bool settings_valid(const UiSettings *s)
{
    return (s->layout < UI_LAYOUT_COUNT) && (s->theme < UI_THEME_COUNT) && (s->accent < UI_ACCENT_COUNT) &&
           (s->grid < UI_GRID_COUNT) && (s->startup_tab <= UI_STARTUP_FAVORITES) &&
           (s->last_tab <= UI_TAB_FAVORITES) && (s->screen_timeout <= 60U) && (s->flipped <= 1U) &&
           (s->language < UI_LANGUAGE_COUNT) && (s->volume_limit_on <= 1U) && (s->volume_limit <= 100U) &&
           (s->startup_screen < UI_STARTUP_SCREEN_COUNT);
}

/* ------------------------------------------------------------------ view */

/* Devices of the current tab, in the host's order. Layout C shows the
 * active device in its own panel, so not among the tiles. */
static void update_view(void)
{
    const bool skip_active = (g_ui.card_kind == CARD_TILE);

    g_ui.view_count = 0U;
    for (uint32_t i = 0U; i < g_ui.count; i++)
    {
        if (skip_active && (g_ui.devices[i].id == g_ui.active))
        {
            continue;
        }
        if ((g_ui.tab == UI_TAB_ALL) || (g_ui.devices[i].favorite != 0U))
        {
            g_ui.view[g_ui.view_count++] = (uint8_t)i;
        }
    }
}

static uint32_t page_count(void)
{
    return (g_ui.view_count + g_ui.per_page - 1U) / g_ui.per_page;
}

static void clamp_page(void)
{
    const uint32_t pages = page_count();

    if ((pages == 0U) || (g_ui.page >= pages))
    {
        g_ui.page = 0U;
    }
}

/* ---------------------------------------------------------------- helpers */

lv_obj_t *ui_make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr, ui_color(g_ui.pal.bg), 0);
    lv_obj_set_style_text_color(scr, ui_color(g_ui.pal.text), 0);
    lv_obj_set_style_text_font(scr, &ui_font_16, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, ui_color(color), 0);
    lv_label_set_text(label, text);
    return label;
}

/* A plain container without the theme's styles. */
lv_obj_t *ui_make_box(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);

    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

void ui_style_card(lv_obj_t *obj, int32_t radius)
{
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, ui_color(g_ui.pal.btn), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    /* On the light theme white cards need an outline. */
    lv_obj_set_style_border_color(obj, ui_color(g_ui.pal.line), 0);
    lv_obj_set_style_border_width(obj, g_ui.pal.dark ? 0 : 1, 0);
}

/* ---------------------------------------------------------------- render */

static void render_button(uint32_t slot)
{
    UiDeviceButton *b = &g_ui.buttons[slot];
    const uint32_t pos = (g_ui.page * g_ui.per_page) + slot;
    const UiDevice *d;
    bool active;
    bool pending;
    uint32_t fg;

    if (pos >= g_ui.view_count)
    {
        lv_obj_add_flag(b->btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    d = &g_ui.devices[g_ui.view[pos]];
    active = (d->id == g_ui.active);
    pending = !active && (d->id == g_ui.pending);
    fg = active ? g_ui.pal.on_acc : g_ui.pal.text;

    lv_obj_remove_flag(b->btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(b->btn, ui_color(active ? g_ui.pal.accent : g_ui.pal.btn), 0);
    if (pending)
    {
        lv_obj_set_style_border_color(b->btn, ui_color(g_ui.pal.accent), 0);
        lv_obj_set_style_border_width(b->btn, 2, 0);
    }
    else
    {
        lv_obj_set_style_border_color(b->btn, ui_color(g_ui.pal.line), 0);
        lv_obj_set_style_border_width(b->btn, (g_ui.pal.dark || active) ? 0 : 1, 0);
    }
    lv_label_set_text_static(b->icon, ui_icon_symbol(d->icon));
    lv_obj_set_style_text_color(b->icon, ui_color(active ? g_ui.pal.on_acc : g_ui.pal.muted), 0);
    lv_label_set_text(b->name, ui_display_name(d));
    lv_obj_set_style_text_color(b->name, ui_color(fg), 0);
    if (b->sub != NULL)
    {
        /* B: the Windows name under an alias. */
        if (d->alias[0] != '\0')
        {
            lv_label_set_text(b->sub, d->name);
            lv_obj_set_style_text_color(b->sub, ui_color(active ? g_ui.pal.on_acc : g_ui.pal.muted), 0);
            lv_obj_remove_flag(b->sub, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(b->sub, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (b->star != NULL)
    {
        lv_obj_set_style_text_color(b->star, ui_color(active ? g_ui.pal.on_acc : g_ui.pal.accent), 0);
        if (d->favorite != 0U)
        {
            lv_obj_remove_flag(b->star, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(b->star, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if ((b->state != NULL) && (g_ui.card_kind == CARD_LIST))
    {
        /* B: a check mark for the active device, "..." while pending. */
        lv_label_set_text_static(b->state, active ? SYM_CHECK : (pending ? "..." : ""));
        lv_obj_set_style_text_color(b->state, ui_color(active ? g_ui.pal.on_acc : g_ui.pal.accent), 0);
    }
    else if (b->state != NULL)
    {
        if (active || pending)
        {
            lv_label_set_text_fmt(b->state, "%s%s", active ? SYM_CHECK " " : "",
                                  ui_text(active ? TXT_ACTIVE : TXT_PENDING));
            lv_obj_set_style_text_color(b->state, ui_color(active ? g_ui.pal.on_acc : g_ui.pal.accent), 0);
            lv_obj_remove_flag(b->state, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(b->state, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void render_tabs(void)
{
    for (uint32_t t = 0U; t < 2U; t++)
    {
        const bool on = (g_ui.tab == t);

        if (g_ui.card_kind == CARD_TILE)
        {
            /* C pills: the chosen tab filled with the accent. */
            lv_obj_set_style_bg_color(g_ui.tab_btns[t], ui_color(on ? g_ui.pal.accent : g_ui.pal.btn), 0);
            lv_obj_set_style_bg_opa(g_ui.tab_btns[t], (on || (g_ui.card_kind == CARD_TILE)) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    0);
            lv_obj_set_style_text_color(g_ui.tab_labels[t], ui_color(on ? g_ui.pal.on_acc : g_ui.pal.muted), 0);
        }
        else
        {
            lv_obj_set_style_text_color(g_ui.tab_labels[t], ui_color(on ? g_ui.pal.text : g_ui.pal.muted), 0);
            lv_obj_set_style_border_width(g_ui.tab_btns[t], on ? 3 : 0, 0);
        }
    }
}

/* C: the active device's panel. */
static void render_hero(void)
{
    const UiDevice *d = NULL;

    for (uint32_t i = 0U; i < g_ui.count; i++)
    {
        if (g_ui.devices[i].id == g_ui.active)
        {
            d = &g_ui.devices[i];
        }
    }
    lv_label_set_text_static(g_ui.hero_icon, ui_icon_symbol((d != NULL) ? d->icon : UI_ICON_GENERIC));
    lv_obj_set_style_text_color(g_ui.hero_icon, ui_color((d != NULL) ? g_ui.pal.accent : g_ui.pal.muted), 0);
    lv_label_set_text(g_ui.hero_name, (d != NULL) ? ui_display_name(d) : ui_text(TXT_HERO_NONE));
    lv_label_set_text(g_ui.hero_sub, ((d != NULL) && (d->alias[0] != '\0')) ? d->name : "");
}

static void render_main(void)
{
    uint32_t pages;

    update_view();
    clamp_page();
    pages = page_count();
    if (g_ui.hero_name != NULL)
    {
        render_hero();
    }
    if (g_ui.list_title != NULL)
    {
        lv_label_set_text_static(g_ui.list_title, ui_text((g_ui.tab == UI_TAB_FAVORITES) ? TXT_LIST_FAV : TXT_LIST_ALL));
        lv_label_set_text_fmt(g_ui.list_count, ui_text(TXT_N_OUTPUTS), (unsigned)g_ui.view_count);
    }

    for (uint32_t slot = 0U; slot < g_ui.per_page; slot++)
    {
        render_button(slot);
    }
    render_tabs();

    /* C with only the active device in the tab: the panel shows it, so the
     * empty tiles need no text. */
    if ((g_ui.view_count == 0U) && !((g_ui.card_kind == CARD_TILE) && (g_ui.count > 0U) &&
                                     (g_ui.tab == UI_TAB_ALL)))
    {
        lv_label_set_text(g_ui.empty_label,
                          ui_text(((g_ui.tab == UI_TAB_FAVORITES) && (g_ui.count > 0U)) ? TXT_FAV_EMPTY : TXT_EMPTY));
        lv_obj_remove_flag(g_ui.empty_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(g_ui.empty_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (pages > 1U)
    {
        lv_label_set_text_fmt(g_ui.page_label, "%u/%u", (unsigned)(g_ui.page + 1U), (unsigned)pages);
        lv_obj_remove_flag(g_ui.pager, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_state(g_ui.prev_btn, LV_STATE_DISABLED, g_ui.page == 0U);
        lv_obj_set_state(g_ui.next_btn, LV_STATE_DISABLED, g_ui.page + 1U >= pages);
    }
    else
    {
        lv_obj_add_flag(g_ui.pager, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_audio(void)
{
    const bool known = (g_ui.volume != UI_VOLUME_UNKNOWN);
    const bool muted = (g_ui.muted == 1U);
    const uint32_t fill = muted ? g_ui.pal.muted : g_ui.pal.accent;

    lv_obj_set_state(g_ui.slider, LV_STATE_DISABLED, !known);
    if (known && !lv_obj_has_state(g_ui.slider, LV_STATE_PRESSED))
    {
        lv_slider_set_value(g_ui.slider, g_ui.volume, LV_ANIM_OFF);
    }
    if (known)
    {
        lv_label_set_text_fmt(g_ui.volume_label, "%u%%", (unsigned)lv_slider_get_value(g_ui.slider));
    }
    else
    {
        lv_label_set_text_static(g_ui.volume_label, "--");
    }
    /* Muted: grey fill. Unknown: empty track, no knob. */
    lv_obj_set_style_bg_color(g_ui.slider, ui_color(fill), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_ui.slider, ui_color(fill), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(g_ui.slider, known ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_ui.slider, known ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_set_state(g_ui.mute_btn, LV_STATE_DISABLED, g_ui.muted == UI_VOLUME_UNKNOWN);
    lv_label_set_text_static(g_ui.mute_icon, muted ? SYM_MUTED : SYM_VOLUME);
    lv_obj_set_style_bg_color(g_ui.mute_btn, ui_color(muted ? g_ui.pal.accent : g_ui.pal.btn), 0);
    lv_obj_set_style_text_color(g_ui.mute_icon, ui_color(muted ? g_ui.pal.on_acc : g_ui.pal.text), 0);
}

/* ---------------------------------------------------------------- events */

void ui_on_slider_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const uint8_t value = (uint8_t)lv_slider_get_value(g_ui.slider);

    if ((code != LV_EVENT_VALUE_CHANGED) && (code != LV_EVENT_RELEASED))
    {
        return;
    }
    lv_label_set_text_fmt(g_ui.volume_label, "%u%%", (unsigned)value);
    /* The slider's value is the volume now. The caller may not report it
     * back (the host confirms the value already shown), and render_audio,
     * e.g. on a mute change, must not move the knob back to the volume from
     * before the drag. */
    if (g_ui.volume != UI_VOLUME_UNKNOWN)
    {
        g_ui.volume = value;
    }
    /* Moving the slider while muted unmutes, once until the host answers. */
    if ((code == LV_EVENT_VALUE_CHANGED) && (g_ui.muted == 1U) && !g_ui.unmute_asked && (g_ui.on_mute != NULL))
    {
        g_ui.unmute_asked = true;
        g_ui.on_mute(false, g_ui.audio_user);
    }
    if (g_ui.on_volume != NULL)
    {
        g_ui.on_volume(value, code == LV_EVENT_RELEASED, g_ui.audio_user);
    }
}

void ui_on_mute_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if ((g_ui.on_mute != NULL) && (g_ui.muted != UI_VOLUME_UNKNOWN))
    {
        g_ui.on_mute(g_ui.muted == 0U, g_ui.audio_user);
    }
}

void ui_on_device_clicked(lv_event_t *e)
{
    const uint32_t slot = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    const uint32_t pos = (g_ui.page * g_ui.per_page) + slot;
    uint8_t id;

    if (pos >= g_ui.view_count)
    {
        return;
    }
    id = g_ui.devices[g_ui.view[pos]].id;
    if ((id != g_ui.active) && (g_ui.on_select != NULL))
    {
        g_ui.on_select(id, g_ui.user);
    }
}

void ui_on_page_clicked(lv_event_t *e)
{
    const bool next = lv_event_get_user_data(e) != NULL;
    const uint32_t pages = page_count();

    if (next && (g_ui.page + 1U < pages))
    {
        g_ui.page++;
    }
    else if (!next && (g_ui.page > 0U))
    {
        g_ui.page--;
    }
    else
    {
        return;
    }
    render_main();
}

void ui_notify_settings(void)
{
    if (g_ui.on_settings != NULL)
    {
        g_ui.on_settings(&g_ui.settings, g_ui.settings_user);
    }
}

void ui_on_tab_clicked(lv_event_t *e)
{
    const uint32_t tab = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (tab == g_ui.tab)
    {
        return;
    }
    g_ui.tab = tab;
    g_ui.page = 0U;
    update_view();
    render_main();
    g_ui.settings.last_tab = (uint8_t)tab;
    ui_notify_settings();
}

static void on_notice_timer(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    if (g_ui.notice != NULL)
    {
        lv_obj_add_flag(g_ui.notice, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_flip_armed(bool armed)
{
    g_ui.flip_armed = armed;
    for (uint32_t i = 0U; i < 2U; i++)
    {
        if (g_ui.flip_icons[i] != NULL)
        {
            lv_obj_set_style_text_color(g_ui.flip_icons[i], ui_color(armed ? g_ui.pal.accent : g_ui.pal.muted), 0);
        }
    }
}

/* A long press arms the flip (the icon turns to the accent colour); it
 * happens when the finger lifts, so the rest of the press is not read in
 * the new orientation. Sliding off the button cancels. */
static void on_flip_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        g_ui.flip_pressed_at = lv_tick_get();
    }
    else if (code == LV_EVENT_PRESSING)
    {
        if (!g_ui.flip_armed && (lv_tick_elaps(g_ui.flip_pressed_at) >= UI_FLIP_HOLD_MS))
        {
            set_flip_armed(true);
        }
    }
    else if (code == LV_EVENT_RELEASED)
    {
        const bool armed = g_ui.flip_armed;

        set_flip_armed(false);
        if (armed && (g_ui.on_flip != NULL))
        {
            g_ui.on_flip(g_ui.flip_user);
        }
    }
    else if (code == LV_EVENT_PRESS_LOST)
    {
        set_flip_armed(false);
    }
    else
    {
        /* other events: nothing to do */
    }
}

/* ----------------------------------------------------------------- build */

/* Flip button in the top right corner of a screen, over the bar area. */
static void build_flip_button(lv_obj_t *scr, uint32_t index)
{
    lv_obj_t *btn = ui_make_box(scr);

    lv_obj_set_size(btn, FLIP_W, BAR_H);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    g_ui.flip_icons[index] = ui_make_label(btn, &ui_font_16, g_ui.pal.muted, SYM_FLIP);
    lv_obj_center(g_ui.flip_icons[index]);
    lv_obj_add_event_cb(btn, on_flip_event, LV_EVENT_ALL, NULL);
}

static void build_waiting_screen(void)
{
    lv_obj_t *box;

    g_ui.waiting_screen = ui_make_screen();
    box = ui_make_box(g_ui.waiting_screen);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 10, 0);

    (void)ui_make_label(box, &ui_icons_40, g_ui.pal.link, SYM_HOURGLASS);
    (void)ui_make_label(box, &ui_font_22, g_ui.pal.text, ui_text(TXT_WAITING));
    (void)ui_make_label(box, &ui_font_16, g_ui.pal.muted, ui_text(TXT_WAITING_HINT));
    build_flip_button(g_ui.waiting_screen, 0U);
}

void ui_on_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show_settings();
}

void ui_on_edit_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show_edit();
}

/* The Audio tab's top bar: the two sub-tabs on the left, the edit mode
 * button on the right (hidden without edit handlers). Settings and the
 * connection mark are on the rail (ui_shell.c). */
void ui_build_tab_bar(lv_obj_t *parent, int32_t width)
{
    static const char *const tab_symbols[2] = { SYM_GRID, SYM_STAR };
    static const UiTextId tab_texts[2] = { TXT_TAB_ALL, TXT_TAB_FAV };
    lv_obj_t *bar = ui_make_box(parent);
    lv_obj_t *pen;

    lv_obj_set_size(bar, width, BAR_H);
    lv_obj_set_style_bg_color(bar, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, ui_color(g_ui.pal.line), 0);
    lv_obj_set_style_border_width(bar, g_ui.pal.dark ? 0 : 1, 0);

    for (uint32_t t = 0U; t < 2U; t++)
    {
        lv_obj_t *btn = ui_make_box(bar);

        lv_obj_set_size(btn, TAB_W, BAR_H);
        lv_obj_set_pos(btn, (int32_t)(PAD + (t * TAB_W)), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        /* Underline of the selected tab. */
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(btn, ui_color(g_ui.pal.accent), 0);
        lv_obj_add_event_cb(btn, ui_on_tab_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)t);
        g_ui.tab_btns[t] = btn;
        g_ui.tab_labels[t] = ui_make_label(btn, &ui_font_16, g_ui.pal.muted, "");
        lv_label_set_text_fmt(g_ui.tab_labels[t], "%s  %s", tab_symbols[t], ui_text(tab_texts[t]));
        lv_obj_center(g_ui.tab_labels[t]);
    }

    pen = ui_make_box(bar);
    lv_obj_set_size(pen, FLIP_W, BAR_H);
    lv_obj_align(pen, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_flag(pen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pen, ui_on_edit_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_make_label(pen, &ui_font_16, g_ui.pal.muted, SYM_PENCIL));
    if (!g_ui.edit_set)
    {
        lv_obj_add_flag(pen, LV_OBJ_FLAG_HIDDEN);
    }
    g_ui.edit_btn = pen;
}

static void build_device_button(lv_obj_t *parent, uint32_t slot, const GridShape *shape, CardKind kind)
{
    UiDeviceButton *b = &g_ui.buttons[slot];
    lv_obj_t *text;

    memset(b, 0, sizeof(*b));
    b->btn = lv_button_create(parent);
    ui_style_card(b->btn, (kind == CARD_ROW) ? 10 : BTN_RADIUS);
    if (kind == CARD_ROW)
    {
        lv_obj_set_size(b->btn, LV_PCT(100), LV_PCT(100));
        lv_obj_set_grid_cell(b->btn, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, (int32_t)slot, 1);
    }
    else
    {
        lv_obj_set_grid_cell(b->btn, LV_GRID_ALIGN_STRETCH, (int32_t)(slot % shape->cols), 1,
                             LV_GRID_ALIGN_STRETCH, (int32_t)(slot / shape->cols), 1);
    }
    /* Narrow paddings: the Audio tab is 424 px wide (ADR-005), so a 2x2
     * card has 200 px and a 3x2 card 130 px for icon and name. */
    lv_obj_set_style_pad_hor(b->btn, ((kind == CARD_SMALL) || (kind == CARD_MEDIUM)) ? 6 : 8, 0);
    lv_obj_set_style_pad_ver(b->btn, (kind == CARD_ROW) ? 0 : 6, 0);
    lv_obj_set_style_pad_column(b->btn, (kind == CARD_MEDIUM) ? 6 : 8, 0);
    lv_obj_set_flex_flow(b->btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b->btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(b->btn, ui_on_device_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);

    switch (kind)
    {
    case CARD_LARGE:
        b->icon = ui_make_label(b->btn, &ui_icons_40, g_ui.pal.muted, "");
        lv_obj_set_width(b->icon, 44);
        break;
    case CARD_MEDIUM:
        b->icon = ui_make_label(b->btn, &ui_font_22, g_ui.pal.muted, "");
        lv_obj_set_width(b->icon, 24);
        break;
    default:
        b->icon = ui_make_label(b->btn, &ui_font_16, g_ui.pal.muted, "");
        lv_obj_set_width(b->icon, 22);
        break;
    }
    lv_obj_set_style_text_align(b->icon, LV_TEXT_ALIGN_CENTER, 0);

    text = ui_make_box(b->btn);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);

    if ((kind == CARD_LARGE) || (kind == CARD_MEDIUM))
    {
        const lv_font_t *font = (kind == CARD_LARGE) ? &ui_font_22 : &ui_font_16;

        b->name = ui_make_label(text, font, g_ui.pal.text, "");
        lv_obj_set_width(b->name, LV_PCT(100));
        lv_obj_set_height(b->name, 2 * lv_font_get_line_height(font));
        lv_label_set_long_mode(b->name, LV_LABEL_LONG_MODE_DOTS);
        b->state = ui_make_label(text, (kind == CARD_LARGE) ? &ui_font_16 : &ui_font_14, g_ui.pal.on_acc, "");

        /* Favorite mark in the top right corner, outside the flex flow. */
        b->star = ui_make_label(b->btn, &ui_font_14, g_ui.pal.accent, SYM_STAR);
        lv_obj_add_flag(b->star, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(b->star, LV_ALIGN_TOP_RIGHT, 2, -2);
    }
    else
    {
        b->name = ui_make_label(text, &ui_font_16, g_ui.pal.text, "");
        lv_obj_set_width(b->name, LV_PCT(100));
        /* One line; a fixed height makes LVGL cut long names with dots. */
        lv_obj_set_height(b->name, lv_font_get_line_height(&ui_font_16));
        lv_label_set_long_mode(b->name, LV_LABEL_LONG_MODE_DOTS);
        if (kind == CARD_ROW)
        {
            b->star = ui_make_label(b->btn, &ui_font_14, g_ui.pal.accent, SYM_STAR);
        }
    }
}

lv_obj_t *ui_make_small_button(lv_obj_t *parent, int32_t w, int32_t h, const char *symbol, lv_obj_t **label)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *l;

    lv_obj_set_size(btn, w, h);
    ui_style_card(btn, 8);
    l = ui_make_label(btn, &ui_font_16, g_ui.pal.text, symbol);
    lv_obj_center(l);
    if (label != NULL)
    {
        *label = l;
    }
    return btn;
}

/* The volume slider of every layout: 0-100, wide touch area, events. Its
 * size (horizontal or vertical) is the caller's. */
void ui_style_volume_slider(lv_obj_t *slider)
{
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_ext_click_area(slider, VOLUME_TOUCH_PAD);
    lv_obj_set_style_bg_color(slider, ui_color(g_ui.pal.track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, ui_on_slider_event, LV_EVENT_ALL, NULL);
}

/* Bottom strip: mute button, volume slider and percentage; page arrows on
 * the right when there is more than one page. */
static void build_bottom_strip(lv_obj_t *scr)
{
    lv_obj_t *strip = ui_make_box(scr);

    lv_obj_set_size(strip, LV_PCT(100), BOTTOM_H);
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_hor(strip, PAD, 0);
    lv_obj_set_style_pad_column(strip, 12, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_ui.mute_btn = ui_make_small_button(strip, 52, BOTTOM_H - 8, SYM_VOLUME, &g_ui.mute_icon);
    lv_obj_add_event_cb(g_ui.mute_btn, ui_on_mute_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_margin_right(g_ui.mute_btn, VOLUME_GAP - 12, 0);  /* 12: the column gap */

    g_ui.slider = lv_slider_create(strip);
    lv_obj_set_flex_grow(g_ui.slider, 1);
    lv_obj_set_height(g_ui.slider, 8);
    ui_style_volume_slider(g_ui.slider);

    g_ui.volume_label = ui_make_label(strip, &ui_font_16, g_ui.pal.text, "--");
    lv_obj_set_width(g_ui.volume_label, 44);

    g_ui.pager = ui_make_box(strip);
    lv_obj_set_size(g_ui.pager, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(g_ui.pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_ui.pager, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_ui.pager, 8, 0);
    lv_obj_set_style_pad_left(g_ui.pager, 4, 0);

    g_ui.prev_btn = ui_make_small_button(g_ui.pager, 44, BOTTOM_H - 8, SYM_PREV, NULL);
    g_ui.page_label = ui_make_label(g_ui.pager, &ui_font_16, g_ui.pal.muted, "");
    g_ui.next_btn = ui_make_small_button(g_ui.pager, 44, BOTTOM_H - 8, SYM_NEXT, NULL);
    lv_obj_add_event_cb(g_ui.prev_btn, ui_on_page_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_ui.next_btn, ui_on_page_clicked, LV_EVENT_CLICKED, (void *)g_ui.next_btn);
}

/* Layout A in the Audio tab: cards in a grid (or rows), sub-tabs in the
 * top bar, volume and pages in the bottom strip. */
static void build_cards_main(void)
{
    static int32_t cols[4];
    static int32_t rows[6];
    const GridShape *shape = &k_grids[(g_ui.settings.grid < UI_GRID_COUNT) ? g_ui.settings.grid : 0U];
    const CardKind kind = card_kind();
    lv_obj_t *grid;

    g_ui.card_kind = kind;
    g_ui.per_page = (uint32_t)shape->cols * shape->rows;
    for (uint32_t c = 0U; c < shape->cols; c++)
    {
        cols[c] = LV_GRID_FR(1);
    }
    cols[shape->cols] = LV_GRID_TEMPLATE_LAST;
    for (uint32_t r = 0U; r < shape->rows; r++)
    {
        rows[r] = LV_GRID_FR(1);
    }
    rows[shape->rows] = LV_GRID_TEMPLATE_LAST;

    ui_build_tab_bar(g_ui.audio_tab, UI_CONTENT_W);

    grid = ui_make_box(g_ui.audio_tab);
    lv_obj_set_pos(grid, 0, BAR_H);
    lv_obj_set_size(grid, LV_PCT(100), UI_HEIGHT - BAR_H - BOTTOM_H);
    lv_obj_set_style_pad_all(grid, PAD, 0);
    lv_obj_set_style_pad_gap(grid, shape->list ? 4 : PAD, 0);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    for (uint32_t slot = 0U; slot < g_ui.per_page; slot++)
    {
        build_device_button(grid, slot, shape, kind);
    }

    g_ui.empty_label = ui_make_label(g_ui.audio_tab, &ui_font_22, g_ui.pal.muted, "");
    lv_obj_center(g_ui.empty_label);

    build_bottom_strip(g_ui.audio_tab);
}

static void build_main_screen(void)
{
    /* Objects that only some layouts have. */
    g_ui.flip_icons[1] = NULL;
    g_ui.list_title = NULL;
    g_ui.list_count = NULL;
    g_ui.hero_icon = NULL;
    g_ui.hero_name = NULL;
    g_ui.hero_sub = NULL;
    g_ui.edit_btn = NULL;
    (void)memset(g_ui.buttons, 0, sizeof(g_ui.buttons));

    ui_shell_build();
    switch (g_ui.settings.layout)
    {
    case UI_LAYOUT_LIST:
        ui_build_list_main();
        break;
    case UI_LAYOUT_TILES:
        ui_build_tiles_main();
        break;
    default:
        build_cards_main();
        break;
    }
}

static void build_notice(void)
{
    g_ui.notice = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_ui.notice, 440, LV_SIZE_CONTENT);
    lv_obj_align(g_ui.notice, LV_ALIGN_BOTTOM_MID, 0, -PAD);
    lv_obj_set_style_bg_color(g_ui.notice, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_border_color(g_ui.notice, ui_color(g_ui.pal.accent), 0);
    lv_obj_set_style_border_width(g_ui.notice, 2, 0);
    lv_obj_set_style_radius(g_ui.notice, BTN_RADIUS, 0);
    lv_obj_set_style_pad_all(g_ui.notice, 10, 0);
    lv_obj_set_style_pad_column(g_ui.notice, 10, 0);
    lv_obj_set_flex_flow(g_ui.notice, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_ui.notice, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(g_ui.notice, LV_OBJ_FLAG_SCROLLABLE);

    (void)ui_make_label(g_ui.notice, &ui_font_22, g_ui.pal.accent, SYM_WARNING);
    g_ui.notice_label = ui_make_label(g_ui.notice, &ui_font_16, g_ui.pal.text, "");
    lv_obj_set_flex_grow(g_ui.notice_label, 1);
    lv_label_set_long_mode(g_ui.notice_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_add_flag(g_ui.notice, LV_OBJ_FLAG_HIDDEN);
}

/* Build every screen for the current settings. The previous screens are
 * deleted first so that LVGL never holds two sets at once (the LVGL heap
 * does not fit both); an empty screen is shown meanwhile, because LVGL
 * must not delete the active screen. Nothing is drawn in between: the
 * whole rebuild runs in one call. The screen shown before (and the
 * settings list's scroll position) is shown again afterwards. */
static void build_all(void)
{
    const uint32_t shown = Ui_CurrentScreen();
    lv_obj_t *const placeholder = lv_obj_create(NULL);

    if (g_ui.settings_list != NULL)
    {
        g_ui.settings_scroll = lv_obj_get_scroll_y(g_ui.settings_list);
    }
    if (g_ui.kb_text != NULL)
    {
        /* Keep what was typed so far. */
        (void)strncpy(g_ui.kb_draft, lv_textarea_get_text(g_ui.kb_text), UI_MAX_ALIAS_BYTES);
        g_ui.kb_draft[UI_MAX_ALIAS_BYTES] = '\0';
    }
    lv_screen_load(placeholder);
    if (g_ui.notice != NULL)
    {
        lv_obj_delete(g_ui.notice);
        g_ui.notice = NULL;
        lv_timer_pause(g_ui.notice_timer);
    }
    if (g_ui.waiting_screen != NULL)
    {
        lv_obj_delete(g_ui.waiting_screen);
        g_ui.waiting_screen = NULL;
    }
    if (g_ui.main_screen != NULL)
    {
        lv_obj_delete(g_ui.main_screen);
        g_ui.main_screen = NULL;
    }
    if (g_ui.settings_screen != NULL)
    {
        lv_obj_delete(g_ui.settings_screen);
        g_ui.settings_screen = NULL;
        g_ui.settings_list = NULL;
    }
    if (g_ui.kb_screen != NULL)
    {
        lv_obj_delete(g_ui.kb_screen);
        g_ui.kb_screen = NULL;
        g_ui.kb_text = NULL;
    }
    if (g_ui.edit_screen != NULL)
    {
        lv_obj_delete(g_ui.edit_screen);
        g_ui.edit_screen = NULL;
    }
    ui_theme_update();
    (void)lv_theme_default_init(lv_display_get_default(), ui_color(g_ui.pal.accent), ui_color(g_ui.pal.link),
                                g_ui.pal.dark, &ui_font_16);
    build_waiting_screen();
    build_main_screen();
    if (shown == UI_SCREEN_SETTINGS)
    {
        ui_settings_build();
    }
    if ((shown == UI_SCREEN_EDIT) || (shown == UI_SCREEN_KEYBOARD))
    {
        ui_edit_build();
        ui_edit_render();
    }
    if (shown == UI_SCREEN_KEYBOARD)
    {
        ui_keyboard_build();
    }
    build_notice();
    ui_prompt_rebuild();
    ui_shell_show_tab(g_ui.app_tab);

    clamp_page();
    render_main();
    render_audio();
    ui_settings_render();
    if (shown == UI_SCREEN_SETTINGS)
    {
        lv_obj_update_layout(g_ui.settings_screen);
        lv_obj_scroll_to_y(g_ui.settings_list, g_ui.settings_scroll, LV_ANIM_OFF);
        lv_screen_load(g_ui.settings_screen);
    }
    else if (shown == UI_SCREEN_EDIT)
    {
        lv_screen_load(g_ui.edit_screen);
    }
    else if (shown == UI_SCREEN_KEYBOARD)
    {
        lv_screen_load(g_ui.kb_screen);
    }
    else
    {
        lv_screen_load((shown == UI_SCREEN_MAIN) ? g_ui.main_screen : g_ui.waiting_screen);
    }
    lv_obj_delete(placeholder);
}

static void on_rebuild_async(void *user)
{
    LV_UNUSED(user);
    g_ui.rebuild_queued = false;
    build_all();
}

void ui_request_rebuild(void)
{
    if (!g_ui.rebuild_queued && (lv_async_call(on_rebuild_async, NULL) == LV_RESULT_OK))
    {
        g_ui.rebuild_queued = true;
    }
}

/* The settings page exists only while it is shown, so that the main and
 * waiting screens keep the LVGL heap they had before it (#60). Deleted
 * asynchronously: this can run inside an event of one of its objects. */
static void drop_settings_screen(void)
{
    if (g_ui.settings_screen != NULL)
    {
        lv_obj_delete_async(g_ui.settings_screen);
        g_ui.settings_screen = NULL;
        g_ui.settings_list = NULL;
    }
}

void ui_show_main(void)
{
    render_main();
    lv_screen_load(g_ui.main_screen);
    drop_settings_screen();
    ui_edit_drop();
}

void ui_show_settings(void)
{
    if (g_ui.settings_screen == NULL)
    {
        ui_settings_build();
    }
    ui_settings_render();
    lv_obj_scroll_to_y(g_ui.settings_list, 0, LV_ANIM_OFF);
    lv_screen_load(g_ui.settings_screen);
}

/* ---------------------------------------------------------- screen timeout */

static void set_screen_on(bool on)
{
    if (on == !g_ui.screen_off)
    {
        return;
    }
    g_ui.screen_off = !on;
    if (on)
    {
        lv_obj_add_flag(g_ui.wake_catcher, LV_OBJ_FLAG_HIDDEN);
        lv_display_trigger_activity(NULL);
    }
    else
    {
        lv_obj_remove_flag(g_ui.wake_catcher, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui.on_backlight != NULL)
    {
        g_ui.on_backlight(on, g_ui.backlight_user);
    }
}

/* The clock strip of the Dashboard tab changes once a minute. */
static void on_clock_timer(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_dash_clock_tick();
}

/* LVGL counts the time since the last touch (lv_display_get_inactive_time). */
static void on_screen_timer(lv_timer_t *timer)
{
    const uint32_t minutes = g_ui.settings.screen_timeout;

    LV_UNUSED(timer);
    /* A prompt keeps the screen lit: it waits for an answer. */
    if (!g_ui.screen_off && (minutes > 0U) && (g_ui.prompt_id == UI_NO_PROMPT) &&
        (lv_display_get_inactive_time(NULL) >= (minutes * 60000U)))
    {
        set_screen_on(false);
    }
}

void ui_wake_screen(void)
{
    set_screen_on(true);
    lv_display_trigger_activity(NULL);
}

static void on_prompt_timer(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_prompt_tick();
}

/* A press on the dark screen lands on the wake catcher (system layer, over
 * every screen), so it only wakes the screen; the rest of that press and
 * its release stay with the catcher. */
static void on_wake_pressed(lv_event_t *e)
{
    LV_UNUSED(e);
    set_screen_on(true);
}

static void build_wake_catcher(void)
{
    lv_obj_t *c = lv_obj_create(lv_layer_sys());

    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(c, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(c, on_wake_pressed, LV_EVENT_PRESSED, NULL);
    g_ui.wake_catcher = c;
}

/* ------------------------------------------------------------------- API */

void Ui_Init(UiSelectCallback on_select, void *user)
{
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.on_select = on_select;
    g_ui.user = user;
    g_ui.active = UI_NO_DEVICE;
    g_ui.pending = UI_NO_DEVICE;
    g_ui.volume = UI_VOLUME_UNKNOWN;
    g_ui.muted = UI_VOLUME_UNKNOWN;
    g_ui.settings.volume_limit = 50U; /* defaults of the settings record */
    g_ui.notice_timer = lv_timer_create(on_notice_timer, UI_NOTICE_MS, NULL);
    lv_timer_pause(g_ui.notice_timer);
    g_ui.screen_timer = lv_timer_create(on_screen_timer, UI_SCREEN_OFF_CHECK_MS, NULL);
    g_ui.clock_timer = lv_timer_create(on_clock_timer, 1000U, NULL);
    g_ui.dash_slot = UI_DASH_NO_SLOT;
    g_ui.prompt_id = UI_NO_PROMPT;
    g_ui.prompt_timer = lv_timer_create(on_prompt_timer, 1000U, NULL);
    build_wake_catcher();

    build_all();
    lv_screen_load(g_ui.waiting_screen);
}

void Ui_ShowWaiting(void)
{
    lv_screen_load(g_ui.waiting_screen);
    drop_settings_screen();
    ui_edit_drop();
}

void Ui_SetDevices(const UiDevice *devices, size_t count)
{
    if ((devices == NULL) || (count > UI_MAX_DEVICES))
    {
        count = (devices == NULL) ? 0U : UI_MAX_DEVICES;
    }
    for (size_t i = 0U; i < count; i++)
    {
        g_ui.devices[i] = devices[i];
        g_ui.devices[i].name[UI_MAX_NAME_BYTES] = '\0';
        g_ui.devices[i].alias[UI_MAX_ALIAS_BYTES] = '\0';
    }
    g_ui.count = (uint32_t)count;
    g_ui.pending = UI_NO_DEVICE;
    update_view();
    clamp_page();
    render_main();
    /* A new list (e.g. after an edit) leaves the settings page, the edit
     * mode and the keyboard open; from the waiting screen it shows the
     * devices. */
    ui_edit_render();
    if ((Ui_CurrentScreen() == UI_SCREEN_MAIN) || (Ui_CurrentScreen() == UI_SCREEN_WAITING))
    {
        lv_screen_load(g_ui.main_screen);
    }
}

void Ui_SetActive(uint8_t device_id)
{
    g_ui.active = device_id;
    g_ui.pending = UI_NO_DEVICE;
    render_main();
}

void Ui_SetPending(uint8_t device_id)
{
    g_ui.pending = device_id;
    render_main();
}

void Ui_ShowError(const char *text)
{
    lv_label_set_text(g_ui.notice_label, (text != NULL) ? text : "");
    lv_obj_remove_flag(g_ui.notice, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(g_ui.notice_timer);
    lv_timer_resume(g_ui.notice_timer);
}

void Ui_ShowNotice(UiNotice notice)
{
    static const UiTextId texts[] = { TXT_SELECT_FAILED, TXT_HOST_TIMEOUT, TXT_EDIT_FAILED, TXT_DETAIL_FAILED,
                                      TXT_ACTION_FAILED };

    Ui_ShowError(ui_text(((uint32_t)notice < (sizeof(texts) / sizeof(texts[0]))) ? texts[notice] : TXT_HOST_TIMEOUT));
}

void Ui_SetAudioHandlers(UiVolumeCallback on_volume, UiMuteCallback on_mute, void *user)
{
    g_ui.on_volume = on_volume;
    g_ui.on_mute = on_mute;
    g_ui.audio_user = user;
}

void Ui_SetVolume(uint8_t volume)
{
    g_ui.volume = (volume > 100U) ? UI_VOLUME_UNKNOWN : volume;
    render_audio();
}

void Ui_SetMuted(uint8_t muted)
{
    g_ui.muted = (muted > 1U) ? UI_VOLUME_UNKNOWN : muted;
    g_ui.unmute_asked = false;
    render_audio();
}

void Ui_SetFlipHandler(UiFlipCallback on_flip, void *user)
{
    g_ui.on_flip = on_flip;
    g_ui.flip_user = user;
}

void Ui_ApplySettings(const UiSettings *settings)
{
    const UiSettings old = g_ui.settings;
    bool rebuild;

    if ((settings == NULL) || !settings_valid(settings))
    {
        return;
    }
    g_ui.settings = *settings;
    if (old.screen_timeout != settings->screen_timeout)
    {
        /* A new timeout counts from now; "never" lights a dark screen. */
        lv_display_trigger_activity(NULL);
        if (settings->screen_timeout == 0U)
        {
            set_screen_on(true);
        }
    }
    rebuild = (old.theme != settings->theme) || (old.accent != settings->accent) ||
              (old.language != settings->language) || (old.layout != settings->layout) ||
              (old.grid != settings->grid);

    if (!g_ui.settings_seen)
    {
        /* The first settings after start choose the screen and the tab. */
        g_ui.settings_seen = true;
        ui_shell_show_tab(settings->startup_screen);
        if (settings->startup_tab == UI_STARTUP_LAST)
        {
            g_ui.tab = settings->last_tab;
        }
        else
        {
            g_ui.tab = (settings->startup_tab == UI_STARTUP_FAVORITES) ? UI_TAB_FAVORITES : UI_TAB_ALL;
        }
        g_ui.page = 0U;
    }
    g_ui.settings.last_tab = (uint8_t)g_ui.tab;
    update_view();
    if (rebuild)
    {
        build_all();
    }
    else
    {
        clamp_page();
        render_main();
        ui_settings_render();
    }
}

const UiSettings *Ui_Settings(void)
{
    return &g_ui.settings;
}

void Ui_SetSettingsHandler(UiSettingsCallback on_settings, void *user)
{
    g_ui.on_settings = on_settings;
    g_ui.settings_user = user;
}

uint32_t Ui_CurrentPage(void)
{
    return g_ui.page;
}

uint8_t Ui_ShownVolume(void)
{
    return (g_ui.volume == UI_VOLUME_UNKNOWN) ? UI_VOLUME_UNKNOWN : (uint8_t)lv_slider_get_value(g_ui.slider);
}

uint32_t Ui_CurrentTab(void)
{
    return g_ui.tab;
}

void Ui_SetBacklightHandler(UiBacklightCallback on_backlight, void *user)
{
    g_ui.on_backlight = on_backlight;
    g_ui.backlight_user = user;
}

bool Ui_ScreenOn(void)
{
    return !g_ui.screen_off;
}

void Ui_SetEditHandlers(const UiEditHandlers *handlers, void *user)
{
    g_ui.edit_set = (handlers != NULL);
    if (handlers != NULL)
    {
        g_ui.edit = *handlers;
    }
    else
    {
        (void)memset(&g_ui.edit, 0, sizeof(g_ui.edit));
    }
    g_ui.edit_user = user;
    if (g_ui.edit_btn != NULL)
    {
        if (g_ui.edit_set)
        {
            lv_obj_remove_flag(g_ui.edit_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_ui.edit_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

uint32_t Ui_CurrentScreen(void)
{
    const lv_obj_t *const scr = lv_screen_active();

    if (scr == NULL)
    {
        return UI_SCREEN_WAITING;
    }
    if (scr == g_ui.settings_screen)
    {
        return UI_SCREEN_SETTINGS;
    }
    if (scr == g_ui.edit_screen)
    {
        return UI_SCREEN_EDIT;
    }
    if (scr == g_ui.kb_screen)
    {
        return UI_SCREEN_KEYBOARD;
    }
    return ((scr != NULL) && (scr == g_ui.main_screen)) ? UI_SCREEN_MAIN : UI_SCREEN_WAITING;
}

void Ui_MemoryUse(uint32_t *used, uint32_t *peak)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    if (used != NULL)
    {
        *used = (uint32_t)(mon.total_size - mon.free_size);
    }
    if (peak != NULL)
    {
        *peak = (uint32_t)mon.max_used;
    }
}
