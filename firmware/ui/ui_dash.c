/**
 * @file    ui_dash.c
 * @brief   Dashboard tab (ADR-006, #14): clock strip, four cards, bottom
 *          strip (media controls and volume, #32), and the generic detail
 *          view.
 *
 *     ┌──────────────────────────────────────────┐
 *     │ 14:32  Çarşamba 21 Ekim       (bell) 3 (●) │  clock strip (36 px), status icons
 *     │ ┌ card 0 ─────────┐ ┌ card 1 ─────────┐  │
 *     │ │ [ic] title badge│ │ [ic] line       │  │  host cards (slots 0, 1)
 *     │ │      line       │ │      line       │  │
 *     │ └─────────────────┘ └─────────────────┘  │
 *     │ ┌ now playing ────┐ ┌ picture ────────┐  │  the board's own cards
 *     │ └─────────────────┘ └─────────────────┘  │  (ADR-010)
 *     │ [|<] [ > ] [>|]      (vol) ━━━━●──── 45% │  bottom strip (44 px)
 *     └──────────────────────────────────────────┘
 *
 * The board knows nothing about weather or menus: a card and a detail are
 * text records from the host (protocol.md, "Record text"):
 *
 *   card:   icon ␟ title ␟ badge ␟ bg ␟ flags ␟ frame ␟ icon colour
 *           \n line \n line \n line [\n line]
 *           (ADR-010: bg RRGGBB, flags "L" = first line large; a 40 px icon
 *           on the left; only slots 0 and 1 have a place on the screen;
 *           frame RRGGBB: the gold-leaf frame, icon colour RRGGBB; up to
 *           four lines; without a title the badge sits below the icon)
 *   detail: title ␟ subtitle ␟ flags \n rows ...
 *           row:     icon ␟ label ␟ value ␟ colour (RRGGBB: icon, label and
 *                    value in that colour)
 *           section: #title
 *           button:  !action ␟ label   (at most 3, at the bottom)
 *           flags:   "n" shows previous / next in the header, "c" a clear
 *                    button at its right (ACTION 255)
 *
 * The detail replaces the cards in the tab (the rail stays); it is built
 * when shown and deleted when closed.
 *
 * Over every screen: the prompt (ADR-007, modal) and the banner (ADR-009,
 * a strip at the top that closes itself):
 *
 *   banner: icon ␟ title ␟ seconds ␟ slot \n line \n line
 *   prompt: title ␟ subtitle ␟ seconds ␟ bg ␟ frame ␟ flags ␟ icon \n as a
 *           detail (bg RRGGBB: the panel, or with flag "s" only its title
 *           strip; frame RRGGBB: the gold-leaf frame; icon: the title's)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define STRIP_H     36
#define MEDIA_H     44
#define MEDIA_BTN_W 44    /**< previous, next */
#define PLAY_BTN_W  56    /**< play / pause, in the accent colour */
#define LABEL_W     112   /**< width of a detail row's label */
#define MAX_LINES   48U   /**< lines of a detail that are drawn */
#define MAX_BUTTONS 3U
#define SEP         '\x1F'

/* Working copy of a record, split in place. */
static char s_scratch[UI_DASH_DETAIL_BYTES + 1U];

#define ICON_W      22    /**< icon column of a detail row */
#define ICON_CLAUDE 13U   /**< protocol.md "Icon": Claude Code */
#define CLAUDE_ORANGE 0xD97757U /**< the Claude symbol's own colour (#59) */

/* Icon numbers of protocol.md ("Icon"); NULL for 0, empty or unknown. */
static const char *icon_symbol(const char *field)
{
    static const char *const symbols[] = {
        NULL,          SYM_SUN,  SYM_MOON,       SYM_CLOUD_SUN, SYM_CLOUD_MOON, SYM_CLOUD,
        SYM_FOG,       SYM_RAIN, SYM_HEAVY_RAIN, SYM_SNOW,      SYM_STORM,      SYM_UTENSILS,
        SYM_BELL,      SYM_CLAUDE,    SYM_STAR, SYM_CLOSE,      SYM_WARNING,
    };
    char *end = NULL;
    unsigned long n;

    if ((field == NULL) || (field[0] < '0') || (field[0] > '9'))
    {
        return NULL;
    }
    n = strtoul(field, &end, 10);
    if ((end == NULL) || (*end != '\0') || (n >= (sizeof(symbols) / sizeof(symbols[0]))))
    {
        return NULL;
    }
    return symbols[n];
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

/* Six hex digits RRGGBB -> colour; false for anything else. */
static bool parse_rgb(const char *text, uint32_t *rgb)
{
    uint32_t v = 0U;

    if ((text == NULL) || (strlen(text) != 6U))
    {
        return false;
    }
    for (uint32_t i = 0U; i < 6U; i++)
    {
        const char c = text[i];
        uint32_t d;

        if ((c >= '0') && (c <= '9'))
        {
            d = (uint32_t)(c - '0');
        }
        else if ((c >= 'A') && (c <= 'F'))
        {
            d = (uint32_t)(c - 'A') + 10U;
        }
        else if ((c >= 'a') && (c <= 'f'))
        {
            d = (uint32_t)(c - 'a') + 10U;
        }
        else
        {
            return false;
        }
        v = (v << 4) | d;
    }
    *rgb = v;
    return true;
}

/* Text on a coloured card: dark on a bright background, white otherwise
 * (perceived brightness, ITU-R BT.601 weights). */
static uint32_t text_on(uint32_t bg)
{
    const uint32_t y = (((bg >> 16) & 0xFFU) * 299U) + (((bg >> 8) & 0xFFU) * 587U) + ((bg & 0xFFU) * 114U);

    return (y > 160000U) ? 0x1B1B1FU : 0xFFFFFFU;
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

#define CARD_ICON_W  44        /**< icon column of a card (40 px icons) */
#define CARD_RECORDS 2U        /**< host cards on the screen (slots 0, 1) */
#define MUSIC_BG     0x5B4B8AU /**< now-playing card (ADR-010) */
#define PICTURE_BG   0x2F6F73U /**< picture card without a picture */
#define THUMB        36        /**< cover on the now-playing card */

#define FRAME_W 4 /**< width of the gold-leaf frame */

/* The gold-leaf frame of a card or prompt (ADR-010 amendment, a liked
 * dish): @p obj becomes a vertical gradient of @p frame, light at the top
 * and dark at the bottom, FRAME_W around a new inner box in @p inner_bg,
 * which is returned for the content. The inner box takes no touches. */
static lv_obj_t *add_leaf_frame(lv_obj_t *obj, uint32_t frame, uint32_t inner_bg)
{
    const lv_color_t c = ui_color(frame);
    lv_obj_t *inner = ui_make_box(obj);

    lv_obj_set_style_bg_color(obj, lv_color_lighten(c, LV_OPA_50), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_darken(c, LV_OPA_30), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, FRAME_W, 0);
    lv_obj_set_size(inner, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(inner, ui_color(inner_bg), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(inner, BTN_RADIUS - FRAME_W + 1, 0);
    return inner;
}

/* A card's colours: the background and its text, or the theme's. */
static void card_colours(lv_obj_t *card, const char *bg_field, uint32_t *bg, uint32_t *fg, bool *coloured)
{
    *coloured = parse_rgb(bg_field, bg);
    ui_style_card(card, BTN_RADIUS);
    if (*coloured)
    {
        lv_obj_set_style_bg_color(card, ui_color(*bg), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        *fg = text_on(*bg);
    }
    else
    {
        *bg = g_ui.pal.btn;
        *fg = g_ui.pal.text;
    }
}

/* One line of a card: never wrapped, cut with "...". */
static lv_obj_t *card_line(lv_obj_t *parent, const lv_font_t *font, uint32_t color, bool secondary, const char *text)
{
    lv_obj_t *label = ui_make_label(parent, font, color, text);

    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    if (secondary)
    {
        lv_obj_set_style_text_opa(label, LV_OPA_80, 0);
    }
    return label;
}

/* The icon column on the left (in @p icon_fg; @p badge below the icon if
 * not NULL, in @p badge_fg) and the text column on its right. */
static lv_obj_t *card_columns(lv_obj_t *card, const char *icon, uint32_t icon_fg, const char *badge,
                              uint32_t badge_fg)
{
    lv_obj_t *col;

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 8, 0);
    if ((icon != NULL) || (badge != NULL))
    {
        lv_obj_t *icons = card;
        lv_obj_t *label;

        if (badge != NULL)
        {
            icons = ui_make_box(card);
            lv_obj_set_size(icons, CARD_ICON_W, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        if (icon != NULL)
        {
            label = ui_make_label(icons, &ui_icons_40, icon_fg, icon);
            lv_obj_set_width(label, CARD_ICON_W);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        }
        if (badge != NULL)
        {
            label = ui_make_label(icons, &ui_font_14, badge_fg, badge);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    col = ui_make_box(card);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 1, 0);
    return col;
}

static void render_card(uint32_t slot)
{
    lv_obj_t *card = g_ui.dash_card_objs[slot];
    char *lines[5];
    char *head[7];
    uint32_t n;
    uint32_t bg;
    uint32_t fg;
    uint32_t frame;
    uint32_t icon_fg;
    bool coloured;
    bool large;
    bool inline_badge;
    lv_obj_t *host;
    lv_obj_t *col;

    if ((card == NULL) || (slot >= CARD_RECORDS))
    {
        return;
    }
    lv_obj_clean(card);
    if (g_ui.dash_cards[slot][0] == '\0')
    {
        ui_style_card(card, BTN_RADIUS);
        lv_obj_set_style_bg_opa(card, LV_OPA_40, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        return;
    }
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    (void)memcpy(s_scratch, g_ui.dash_cards[slot], sizeof(g_ui.dash_cards[slot]));
    n = split_lines(s_scratch, lines, 5U);
    split_fields(lines[0], head, 7U);
    card_colours(card, head[3], &bg, &fg, &coloured);
    large = (strchr(head[4], 'L') != NULL);
    lv_obj_set_style_pad_hor(card, 10, 0);
    lv_obj_set_style_pad_ver(card, 8, 0);
    host = card;
    if (parse_rgb(head[5], &frame))
    {
        host = add_leaf_frame(card, frame, bg);
        lv_obj_set_style_pad_hor(host, 10 - FRAME_W, 0);
        lv_obj_set_style_pad_ver(host, 8 - FRAME_W, 0);
    }
    if (!parse_rgb(head[6], &icon_fg))
    {
        icon_fg = fg;
    }
    /* Without a title the badge goes below the icon, so that four lines
     * fit. */
    inline_badge = (head[1][0] == '\0') && (head[2][0] != '\0') && (n > 1U);
    col = card_columns(host, icon_symbol(head[0]), icon_fg, inline_badge ? head[2] : NULL,
                       coloured ? fg : g_ui.pal.accent);

    if (!inline_badge && ((head[1][0] != '\0') || (head[2][0] != '\0')))
    {
        lv_obj_t *top = ui_make_box(col);
        lv_obj_t *label;

        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(top, 6, 0);
        label = ui_make_label(top, &ui_font_14, coloured ? fg : g_ui.pal.muted, head[1]);
        lv_obj_set_flex_grow(label, 1);
        lv_obj_set_height(label, lv_font_get_line_height(&ui_font_14));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        if (coloured)
        {
            lv_obj_set_style_text_opa(label, LV_OPA_80, 0);
        }
        if (head[2][0] != '\0')
        {
            (void)ui_make_label(top, &ui_font_14, coloured ? fg : g_ui.pal.accent, head[2]);
        }
    }
    for (uint32_t i = 1U; i < n; i++)
    {
        const lv_font_t *font = (i == 1U) ? (large ? &ui_font_22 : &ui_font_16) : &ui_font_14;
        const uint32_t color = (coloured || (i == 1U)) ? fg : g_ui.pal.muted;

        (void)card_line(col, font, color, coloured && (i > 1U), lines[i]);
    }
}

/* The now-playing card (bottom left, ADR-010): the music icon, a small
 * cover and the title, from the media record the Music tab shows too. */
void ui_dash_music_render(void)
{
    lv_obj_t *card = g_ui.dash_card_objs[2];
    const uint32_t fg = text_on(MUSIC_BG);
    const bool session = (g_ui.media_status != UI_MEDIA_NONE);
    char *fields[4];
    lv_obj_t *col;

    if (card == NULL)
    {
        return;
    }
    lv_obj_clean(card);
    ui_style_card(card, BTN_RADIUS);
    lv_obj_set_style_bg_color(card, ui_color(MUSIC_BG), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    col = card_columns(card, SYM_MUSIC, fg, NULL, 0U);
    if (!session)
    {
        lv_obj_t *label = ui_make_label(col, &ui_font_16, fg, ui_text(TXT_NOTHING_PLAYING));

        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        return;
    }
    (void)snprintf(s_scratch, sizeof(s_scratch), "%s", g_ui.media_text);
    split_fields(s_scratch, fields, 4U);
    {
        lv_obj_t *top = ui_make_box(col);

        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(top, 6, 0);
        if (g_ui.art_valid && (g_ui.art_buf != NULL))
        {
            lv_obj_t *img = lv_image_create(top);

            lv_image_set_src(img, &g_ui.art_dsc);
            lv_obj_set_size(img, THUMB, THUMB);
            lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
            lv_obj_set_style_radius(img, 4, 0);
            lv_obj_set_style_clip_corner(img, true, 0);
        }
        (void)card_line(top, &ui_font_14, fg, true,
                        (g_ui.media_status == UI_MEDIA_PAUSED) ? ui_text(TXT_MEDIA_PAUSED) : fields[3]);
        lv_obj_set_flex_grow(lv_obj_get_child(top, -1), 1);
    }
    (void)card_line(col, &ui_font_16, fg, false, (fields[0][0] != '\0') ? fields[0] : "-");
    (void)card_line(col, &ui_font_14, fg, true, fields[1]);
}

/* The picture card (bottom right, ADR-010): the picture chosen on the PC,
 * 480 x 272 (protocol v7), scaled down to cover the card (the edges are
 * cut), or a placeholder. A tap shows the picture full screen. */
static void render_picture_card(void)
{
    lv_obj_t *card = g_ui.dash_card_objs[3];

    if (card == NULL)
    {
        return;
    }
    lv_obj_clean(card);
    ui_style_card(card, BTN_RADIUS);
    lv_obj_set_style_border_width(card, 0, 0);
    if (g_ui.pic_valid && (g_ui.pic_buf != NULL))
    {
        lv_obj_t *img = lv_image_create(card);

        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(img, &g_ui.pic_dsc);
        lv_obj_set_size(img, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_COVER);
        return;
    }
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_hor(card, 10, 0);
    lv_obj_set_style_pad_ver(card, 8, 0);
    lv_obj_set_style_bg_color(card, ui_color(PICTURE_BG), 0);
    {
        const uint32_t fg = text_on(PICTURE_BG);
        lv_obj_t *col = card_columns(card, SYM_IMAGE, fg, NULL, 0U);

        lv_obj_t *hint = ui_make_label(col, &ui_font_14, fg, ui_text(TXT_NO_PICTURE));

        lv_obj_set_width(hint, LV_PCT(100));
        lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_opa(hint, LV_OPA_80, 0);
    }
}

/* The picture full screen, 1:1, on lv_layer_top() behind a prompt or a
 * banner (they stay usable). A tap anywhere closes it. */
static void on_photo_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_photo_close();
}

static void open_photo(void)
{
    lv_obj_t *page;
    lv_obj_t *img;

    if ((g_ui.photo_obj != NULL) || !g_ui.pic_valid || (g_ui.pic_buf == NULL))
    {
        return;
    }
    page = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(page, on_photo_clicked, LV_EVENT_CLICKED, NULL);
    img = lv_image_create(page);
    lv_image_set_src(img, &g_ui.pic_dsc);
    lv_obj_center(img);
    lv_obj_move_to_index(page, 0);
    g_ui.photo_obj = page;
}

void ui_photo_close(void)
{
    if (g_ui.photo_obj != NULL)
    {
        /* May run inside its own click event. */
        lv_obj_delete_async(g_ui.photo_obj);
        g_ui.photo_obj = NULL;
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

static void on_clear(lv_event_t *e)
{
    LV_UNUSED(e);
    if ((g_ui.dash.action != NULL) && (g_ui.dash_slot != UI_DASH_NO_SLOT))
    {
        g_ui.dash.action(g_ui.dash_slot, (uint8_t)UI_DASH_ACTION_CLEAR, g_ui.dash_user);
    }
}

/* A header button: a visible key (#49: the back arrow was hard to hit) with
 * a touch area larger than it, more below, as the panel reports touches a
 * few pixels lower than the finger (docs/hardware.md, touch check). */
#define HEADER_BTN_H   30
#define HEADER_TOUCH   12     /**< extra touch area around a header button (the panel reads 5-13 px low) */

static lv_obj_t *make_header_button(lv_obj_t *bar, const char *symbol, int32_t width, lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = ui_make_box(bar);

    lv_obj_set_size(btn, width, HEADER_BTN_H);
    ui_style_card(btn, 8);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(btn, HEADER_TOUCH);
    lv_obj_set_style_bg_color(btn, ui_color(g_ui.pal.accent), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    lv_obj_center(ui_make_label(btn, &ui_font_22, g_ui.pal.text, symbol));
    return btn;
}

/* Header: back, title and subtitle, previous / next and clear when asked for. */
/* An invisible touch zone over the back button and HEADER_TOUCH pixels
 * below it, on top of everything in @p view (#49): LVGL does not look for a
 * button's extended touch area outside its parent (the header bar). */
static void add_back_zone(lv_obj_t *view)
{
    lv_obj_t *zone = ui_make_box(view);

    lv_obj_set_pos(zone, 0, 0);
    lv_obj_set_size(zone, 4 + 64 + HEADER_TOUCH, STRIP_H + HEADER_TOUCH);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(zone, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(zone);
}

static lv_obj_t *build_detail_header(lv_obj_t *view, const char *title, const char *subtitle, bool nav, bool clear)
{
    lv_obj_t *bar = ui_make_box(view);
    lv_obj_t *texts;
    lv_obj_t *label;

    lv_obj_set_size(bar, LV_PCT(100), STRIP_H);
    lv_obj_set_style_bg_color(bar, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    (void)make_header_button(bar, SYM_PREV, 64, on_back, NULL);
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
        (void)make_header_button(bar, SYM_PREV, 48, on_nav, NULL);
        (void)make_header_button(bar, SYM_NEXT, 48, on_nav, (void *)1);
    }
    if (clear)
    {
        (void)make_header_button(bar, SYM_TRASH, 48, on_clear, NULL);
    }
    return bar;
}

/* A row; @p fg its text colour and @p muted its label's on a coloured
 * panel (0: the theme's). A colour field paints the icon, the label and the
 * value. */
static void add_row(lv_obj_t *list, char *line, uint32_t fg, uint32_t muted)
{
    char *f[4];
    lv_obj_t *row;
    lv_obj_t *label;
    const char *icon;
    uint32_t colour = 0U;

    split_fields(line, f, 4U);
    if (fg == 0U)
    {
        fg = g_ui.pal.text;
        muted = g_ui.pal.muted;
    }
    if (parse_rgb(f[3], &colour))
    {
        fg = colour;
        muted = colour;
    }
    row = ui_make_box(list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 3, 0);
    /* A row with an icon field (also 0) keeps the icon column, so that its
     * label lines up with the rows that have an icon. */
    if (f[0][0] != '\0')
    {
        icon = icon_symbol(f[0]);
        label = ui_make_label(row, &ui_font_16, muted, (icon != NULL) ? icon : "");
        lv_obj_set_width(label, ICON_W);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (f[1][0] != '\0')
    {
        label = ui_make_label(row, &ui_font_14, muted, f[1]);
        lv_obj_set_width(label, LABEL_W);
        lv_obj_set_style_pad_top(label, 2, 0);
    }
    label = ui_make_label(row, &ui_font_16, fg, f[2]);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
}

static void add_section(lv_obj_t *list, const char *title, uint32_t colour)
{
    lv_obj_t *label = ui_make_label(list, &ui_font_14, (colour != 0U) ? colour : g_ui.pal.accent, title);

    lv_obj_set_style_pad_top(label, 8, 0);
    lv_obj_set_style_pad_bottom(label, 2, 0);
}

/* A row of up to MAX_BUTTONS buttons ("action ␟ label", modified), each
 * calling @p cb with its action number as user data. */
static lv_obj_t *add_button_strip(lv_obj_t *parent, char **buttons, uint32_t count, lv_event_cb_t cb)
{
    lv_obj_t *strip = ui_make_box(parent);

    lv_obj_set_size(strip, LV_PCT(100), MEDIA_H);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint32_t b = 0U; b < count; b++)
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
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
        label = ui_make_label(btn, &ui_font_16, g_ui.pal.text, f[1]);
        lv_obj_center(label);
    }
    return strip;
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
            add_section(list, &lines[i][1], 0U);
        }
        else if (lines[i][0] != '!')
        {
            add_row(list, lines[i], 0U, 0U);
        }
        else
        {
            /* a button, below */
        }
    }

    if (button_count > 0U)
    {
        lv_obj_t *strip = add_button_strip(view, buttons, button_count, on_detail_button);

        lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
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

        if (g_ui.dash_slot < UI_DASH_CARDS)
        {
            (void)memcpy(s_scratch, g_ui.dash_cards[g_ui.dash_slot], sizeof(g_ui.dash_cards[0]));
        }
        else
        {
            s_scratch[0] = '\0';  /* a status icon's slot above the kept cards */
        }
        (void)split_lines(s_scratch, card_lines, 1U);
        split_fields(card_lines[0], head, 3U);
        label = ui_make_label(view, &ui_font_16, g_ui.pal.muted, ui_text(TXT_LOADING));
        lv_obj_center(label);
        (void)build_detail_header(view, head[1], "", false, false);
        add_back_zone(view);
        return;
    }

    (void)memcpy(s_scratch, g_ui.dash_detail, sizeof(g_ui.dash_detail));
    n = split_lines(s_scratch, lines, MAX_LINES);
    split_fields(lines[0], head, 3U);
    {
        lv_obj_t *bar = build_detail_header(view, head[0], head[1], strchr(head[2], 'n') != NULL,
                                            strchr(head[2], 'c') != NULL);

        build_detail_body(view, lines, n);
        lv_obj_move_foreground(bar);
        add_back_zone(view);
    }
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

/* Open the detail of @p slot (0-7): a card, a status icon or the banner. */
static void open_detail_of(uint32_t slot)
{
    if (slot >= 8U)
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

static void on_card(lv_event_t *e)
{
    const uint32_t slot = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (slot == 2U)
    {
        ui_shell_show_tab(UI_APP_TAB_MUSIC);  /* the now-playing card */
        return;
    }
    if (slot == 3U)
    {
        open_photo();  /* the picture card */
        return;
    }
    if ((slot < CARD_RECORDS) && (g_ui.dash_cards[slot][0] != '\0'))
    {
        open_detail_of(slot);
    }
}

/* ---------------------------------------------------------- status icons */

static uint32_t indicator_colour(uint8_t colour)
{
    switch (colour)
    {
    case UI_INDICATOR_RED:
        return 0xE5484DU;
    case UI_INDICATOR_YELLOW:
        return 0xE5B000U;
    case UI_INDICATOR_GREEN:
        return 0x3FB950U;
    default:
        return g_ui.pal.muted;
    }
}

static void on_indicator(lv_event_t *e)
{
    const uint32_t i = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (i < g_ui.ind_count)
    {
        open_detail_of(g_ui.indicators[i].slot);
    }
}

static void render_indicators(void)
{
    if (g_ui.ind_box == NULL)
    {
        return;
    }
    lv_obj_clean(g_ui.ind_box);
    for (uint32_t i = 0U; i < g_ui.ind_count; i++)
    {
        const UiIndicator *it = &g_ui.indicators[i];
        /* The Claude symbol keeps its own orange unless a state colours it. */
        const uint32_t color = ((it->icon == ICON_CLAUDE) && (it->colour == UI_INDICATOR_NORMAL))
                                   ? CLAUDE_ORANGE
                                   : indicator_colour(it->colour);
        const char *icon = NULL;
        char number[8];
        lv_obj_t *btn = ui_make_box(g_ui.ind_box);

        {
            char field[4];

            (void)snprintf(field, sizeof(field), "%u", (unsigned)it->icon);
            icon = icon_symbol(field);
        }
        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_style_pad_hor(btn, 6, 0);
        lv_obj_set_style_pad_column(btn, 4, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        (void)ui_make_label(btn, &ui_font_22, color, (icon != NULL) ? icon : "?");
        if (it->number > 0U)
        {
            if (it->number > 99U)
            {
                (void)snprintf(number, sizeof(number), "99+");
            }
            else
            {
                (void)snprintf(number, sizeof(number), "%u", (unsigned)it->number);
            }
            (void)ui_make_label(btn, &ui_font_16, color, number);
        }
        if (it->slot < 8U)
        {
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(btn, 6);
            lv_obj_add_event_cb(btn, on_indicator, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        }
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
    lv_obj_set_flex_grow(g_ui.date_label, 1);
    g_ui.ind_box = ui_make_box(strip);
    lv_obj_set_size(g_ui.ind_box, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(g_ui.ind_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_ui.ind_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_ui.ind_box, 4, 0);
    render_indicators();
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
        lv_obj_add_event_cb(card, on_card, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        g_ui.dash_card_objs[slot] = card;
        render_card(slot);
    }
    lv_obj_add_flag(g_ui.dash_card_objs[2], LV_OBJ_FLAG_CLICKABLE);  /* opens the Music tab */
    ui_dash_music_render();
    render_picture_card();
}

/* Bottom strip: media controls on the left (the middle one shows pause
 * while something plays, play otherwise; ui_media_render), the volume of
 * the Windows default output on the right (the Audio tab's, ui.c). */
static void build_media_strip(lv_obj_t *parent)
{
    static const char *const symbols[3] = { SYM_BACKWARD, SYM_PLAY, SYM_FORWARD };
    lv_obj_t *strip = ui_make_box(parent);

    lv_obj_set_size(strip, LV_PCT(100), MEDIA_H);
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_hor(strip, PAD, 0);
    lv_obj_set_style_pad_column(strip, 6, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        lv_obj_t *label = NULL;
        lv_obj_t *btn = ui_make_small_button(strip, (i == 1U) ? PLAY_BTN_W : MEDIA_BTN_W, MEDIA_H - 10, symbols[i],
                                             &label);

        lv_obj_add_event_cb(btn, ui_on_media_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)(UI_MEDIA_PREVIOUS + i));
        if (i == 1U)
        {
            lv_obj_set_style_bg_color(btn, ui_color(g_ui.pal.accent), 0);
            lv_obj_set_style_text_color(label, ui_color(g_ui.pal.on_acc), 0);
            g_ui.dash_play_label = label;
        }
    }

    /* Volume: mute button, slider, percentage. */
    g_ui.dash_mute_btn = ui_make_small_button(strip, 44, MEDIA_H - 10, SYM_VOLUME, &g_ui.dash_mute_icon);
    lv_obj_set_style_margin_left(g_ui.dash_mute_btn, 16, 0);
    lv_obj_set_style_margin_right(g_ui.dash_mute_btn, VOLUME_GAP - 6, 0);  /* 6: the column gap */
    lv_obj_add_event_cb(g_ui.dash_mute_btn, ui_on_mute_clicked, LV_EVENT_CLICKED, NULL);

    g_ui.dash_slider = lv_slider_create(strip);
    lv_obj_set_flex_grow(g_ui.dash_slider, 1);
    lv_obj_set_height(g_ui.dash_slider, 8);
    ui_style_volume_slider(g_ui.dash_slider);

    g_ui.dash_volume_label = ui_make_label(strip, &ui_font_14, g_ui.pal.text, "--");
    lv_obj_set_width(g_ui.dash_volume_label, 40);
    lv_obj_set_style_margin_left(g_ui.dash_volume_label, 6, 0);
    lv_obj_set_style_text_align(g_ui.dash_volume_label, LV_TEXT_ALIGN_RIGHT, 0);
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

/* --------------------------------------------------------------- prompt */

#define PROMPT_W        464
#define PROMPT_H        260
#define PROMPT_GRACE_S  5U   /**< kept after the countdown before closing itself */

static char s_prompt_scratch[UI_PROMPT_BYTES + 1U];

static void close_prompt_view(void)
{
    if (g_ui.prompt_obj != NULL)
    {
        /* May run inside an event of one of its buttons. */
        lv_obj_delete_async(g_ui.prompt_obj);
        g_ui.prompt_obj = NULL;
        g_ui.prompt_count = NULL;
    }
}

static void update_prompt_count(void)
{
    uint32_t elapsed;

    if ((g_ui.prompt_count == NULL) || (g_ui.prompt_seconds == 0U))
    {
        return;
    }
    elapsed = lv_tick_elaps(g_ui.prompt_tick) / 1000U;
    lv_label_set_text_fmt(g_ui.prompt_count, "%us",
                          (unsigned)((elapsed < g_ui.prompt_seconds) ? (g_ui.prompt_seconds - elapsed) : 0U));
}

static void on_prompt_button(lv_event_t *e)
{
    const uint8_t action = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *strip = lv_obj_get_parent(lv_event_get_target_obj(e));

    if (g_ui.prompt_answered || (g_ui.prompt_id == UI_NO_PROMPT))
    {
        return;
    }
    g_ui.prompt_answered = true;
    for (uint32_t i = 0U; i < lv_obj_get_child_count(strip); i++)
    {
        lv_obj_add_state(lv_obj_get_child(strip, (int32_t)i), LV_STATE_DISABLED);
    }
    if (g_ui.dash.prompt_action != NULL)
    {
        g_ui.dash.prompt_action((uint8_t)g_ui.prompt_id, action, g_ui.dash_user);
    }
}

/* Backdrop over everything (it takes the touches), a panel with the header
 * (title, subtitle, countdown), the rows and the buttons. */
static void build_prompt_view(void)
{
    char *lines[MAX_LINES];
    char *head[7];
    char *buttons[MAX_BUTTONS];
    uint32_t button_count = 0U;
    uint32_t n;
    uint32_t bg = 0U;
    uint32_t frame = 0U;
    uint32_t fg = 0U;    /* text on a coloured panel; 0: the theme's */
    uint32_t muted = 0U;
    uint32_t bar_fg;
    uint32_t icon_fg;
    bool coloured;
    bool strip;
    bool framed;
    const char *icon;
    lv_obj_t *panel;
    lv_obj_t *bar;
    lv_obj_t *label;
    lv_obj_t *list;
    lv_obj_t *back = lv_obj_create(lv_layer_top());

    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(back, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_60, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    g_ui.prompt_obj = back;

    panel = ui_make_box(back);
    ui_style_card(panel, BTN_RADIUS);
    lv_obj_set_size(panel, PROMPT_W, PROMPT_H);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, ui_color(g_ui.pal.bar), 0); /* buttons in the card colour stand out */
    lv_obj_set_style_border_color(panel, ui_color(g_ui.pal.accent), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);

    (void)memcpy(s_prompt_scratch, g_ui.prompt_text, sizeof(s_prompt_scratch));
    n = split_lines(s_prompt_scratch, lines, MAX_LINES);
    split_fields(lines[0], head, 7U);
    g_ui.prompt_seconds = (uint32_t)strtoul(head[2], NULL, 10);
    coloured = parse_rgb(head[3], &bg);
    strip = coloured && (strchr(head[5], 's') != NULL);
    framed = parse_rgb(head[4], &frame);
    icon = icon_symbol(head[6]);

    /* The panel: in bg (unless only its strip is), in a gold-leaf frame. */
    if (coloured && !strip)
    {
        lv_obj_set_style_bg_color(panel, ui_color(bg), 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        fg = text_on(bg);
        muted = fg;
    }
    if (framed)
    {
        panel = add_leaf_frame(panel, frame, (coloured && !strip) ? bg : g_ui.pal.bar);
        lv_obj_set_style_pad_all(panel, 8 - FRAME_W + 2, 0);
    }
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    bar = ui_make_box(panel);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    bar_fg = (fg != 0U) ? fg : g_ui.pal.text;
    if (strip)
    {
        lv_obj_set_style_border_color(panel, ui_color(bg), 0); /* the frame in the strip's colour */
        bar_fg = text_on(bg);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(bar, ui_color(bg), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, BTN_RADIUS - 4, 0);
        lv_obj_set_style_pad_hor(bar, 10, 0);
        lv_obj_set_style_pad_ver(bar, 7, 0);
    }
    icon_fg = framed ? frame : (coloured ? bar_fg : g_ui.pal.accent);
    (void)ui_make_label(bar, &ui_font_16, icon_fg, (icon != NULL) ? icon : SYM_WARNING);
    label = ui_make_label(bar, &ui_font_16, bar_fg, head[0]);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    label = ui_make_label(bar, &ui_font_14, coloured ? bar_fg : g_ui.pal.muted, head[1]);
    if (coloured)
    {
        lv_obj_set_style_text_opa(label, LV_OPA_80, 0);
    }
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_height(label, lv_font_get_line_height(&ui_font_14));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    g_ui.prompt_count = ui_make_label(bar, &ui_font_16, coloured ? bar_fg : g_ui.pal.accent, "");
    update_prompt_count();

    for (uint32_t i = 1U; i < n; i++)
    {
        if ((lines[i][0] == '!') && (button_count < MAX_BUTTONS))
        {
            buttons[button_count++] = &lines[i][1];
        }
    }
    list = ui_make_box(panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_ver(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    for (uint32_t i = 1U; i < n; i++)
    {
        if (lines[i][0] == '#')
        {
            add_section(list, &lines[i][1], fg);
        }
        else if (lines[i][0] != '!')
        {
            add_row(list, lines[i], fg, muted);
        }
        else
        {
            /* a button, below */
        }
    }
    if (button_count > 0U)
    {
        lv_obj_t *strip = add_button_strip(panel, buttons, button_count, on_prompt_button);

        if (g_ui.prompt_answered)
        {
            for (uint32_t i = 0U; i < lv_obj_get_child_count(strip); i++)
            {
                lv_obj_add_state(lv_obj_get_child(strip, (int32_t)i), LV_STATE_DISABLED);
            }
        }
    }
}

/* ---------------------------------------------------------------- banner */

#define BANNER_MARGIN     6
#define BANNER_DEFAULT_S  5U
#define BANNER_MAX_S      30U

static char s_banner_scratch[UI_BANNER_BYTES + 1U];

void ui_banner_close(void)
{
    g_ui.banner_on = false;
    if (g_ui.banner_timer != NULL)
    {
        lv_timer_pause(g_ui.banner_timer);
    }
    if (g_ui.banner_obj != NULL)
    {
        /* May run inside the banner's own click event. */
        lv_obj_delete_async(g_ui.banner_obj);
        g_ui.banner_obj = NULL;
        g_ui.banner_count = NULL;
    }
}

static void update_banner_count(void)
{
    const uint32_t elapsed = lv_tick_elaps(g_ui.banner_tick);
    const uint32_t left = (elapsed < g_ui.banner_ms) ? (g_ui.banner_ms - elapsed + 999U) / 1000U : 0U;

    if (g_ui.banner_count != NULL)
    {
        lv_label_set_text_fmt(g_ui.banner_count, "%us", (unsigned)left);
    }
}

static void on_banner_clicked(lv_event_t *e)
{
    const uint8_t slot = g_ui.banner_slot;

    LV_UNUSED(e);
    ui_banner_close();
    if (Ui_CurrentScreen() != UI_SCREEN_MAIN)
    {
        return; /* settings, editing: a tap only closes it */
    }
    if (slot < 8U)
    {
        ui_shell_show_tab(UI_APP_TAB_DASHBOARD);
        open_detail_of(slot);
    }
}

/* One text line of the banner, cut with "..." (never wrapped). */
static void add_banner_line(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = ui_make_label(parent, font, color, text);

    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

/* A panel at the top of lv_layer_top(): icon, title and countdown, then up
 * to two lines. Only the panel takes touches; the screen below stays usable. */
static void build_banner_view(void)
{
    char *lines[3];
    char *head[4];
    uint32_t n;
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_t *bar;
    lv_obj_t *label;
    const char *icon;

    lv_obj_remove_style_all(panel);
    ui_style_card(panel, BTN_RADIUS);
    lv_obj_set_size(panel, UI_WIDTH - (2 * BANNER_MARGIN), LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, BANNER_MARGIN);
    lv_obj_set_style_bg_color(panel, ui_color(g_ui.pal.bar), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, ui_color(g_ui.pal.accent), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_hor(panel, 10, 0);
    lv_obj_set_style_pad_ver(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, on_banner_clicked, LV_EVENT_CLICKED, NULL);
    g_ui.banner_obj = panel;

    (void)memcpy(s_banner_scratch, g_ui.banner_text, sizeof(s_banner_scratch));
    n = split_lines(s_banner_scratch, lines, 3U);
    split_fields(lines[0], head, 4U);

    bar = ui_make_box(panel);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    icon = icon_symbol(head[0]);
    if (icon != NULL)
    {
        (void)ui_make_label(bar, &ui_font_16, g_ui.pal.accent, icon);
    }
    label = ui_make_label(bar, &ui_font_16, g_ui.pal.accent, head[1]);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_height(label, lv_font_get_line_height(&ui_font_16));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    g_ui.banner_count = ui_make_label(bar, &ui_font_14, g_ui.pal.muted, "");
    update_banner_count();

    if (n > 1U)
    {
        add_banner_line(panel, &ui_font_16, g_ui.pal.text, lines[1]);
    }
    if (n > 2U)
    {
        add_banner_line(panel, &ui_font_14, g_ui.pal.muted, lines[2]);
    }
}

void ui_banner_rebuild(void)
{
    if (g_ui.banner_obj != NULL)
    {
        lv_obj_delete(g_ui.banner_obj);
        g_ui.banner_obj = NULL;
        g_ui.banner_count = NULL;
    }
    if (g_ui.banner_on)
    {
        build_banner_view();
    }
}

void ui_banner_tick(void)
{
    if (!g_ui.banner_on)
    {
        return;
    }
    if (g_ui.screen_off || (g_ui.prompt_id != UI_NO_PROMPT) || (lv_tick_elaps(g_ui.banner_tick) >= g_ui.banner_ms))
    {
        ui_banner_close();
        return;
    }
    update_banner_count();
}

void ui_prompt_rebuild(void)
{
    if (g_ui.prompt_obj != NULL)
    {
        lv_obj_delete(g_ui.prompt_obj);
        g_ui.prompt_obj = NULL;
        g_ui.prompt_count = NULL;
    }
    if (g_ui.prompt_id != UI_NO_PROMPT)
    {
        build_prompt_view();
    }
}

void ui_prompt_tick(void)
{
    if (g_ui.prompt_id == UI_NO_PROMPT)
    {
        return;
    }
    update_prompt_count();
    if ((g_ui.prompt_seconds > 0U) &&
        ((lv_tick_elaps(g_ui.prompt_tick) / 1000U) >= (g_ui.prompt_seconds + PROMPT_GRACE_S)))
    {
        /* The host did not close it (a lost frame): it has timed out there. */
        g_ui.prompt_id = UI_NO_PROMPT;
        close_prompt_view();
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

void Ui_SetIndicators(const UiIndicator *items, uint32_t count)
{
    if (items == NULL)
    {
        count = 0U;
    }
    g_ui.ind_count = (count > UI_MAX_INDICATORS) ? UI_MAX_INDICATORS : count;
    for (uint32_t i = 0U; i < g_ui.ind_count; i++)
    {
        g_ui.indicators[i] = items[i];
    }
    render_indicators();
}

uint32_t Ui_IndicatorCount(void)
{
    return g_ui.ind_count;
}

void Ui_SetPictureBuffer(uint8_t *buffer)
{
    g_ui.pic_buf = buffer;
    g_ui.pic_valid = false;
    (void)memset(&g_ui.pic_dsc, 0, sizeof(g_ui.pic_dsc));
    g_ui.pic_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_ui.pic_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_ui.pic_dsc.header.w = UI_PICTURE_W;
    g_ui.pic_dsc.header.h = UI_PICTURE_H;
    g_ui.pic_dsc.header.stride = UI_PICTURE_W * 2U;
    g_ui.pic_dsc.data_size = UI_PICTURE_BYTES;
    g_ui.pic_dsc.data = buffer;
    render_picture_card();
}

void Ui_SetPicture(const uint8_t *pixels)
{
    if ((pixels != NULL) && (g_ui.pic_buf != NULL))
    {
        (void)memcpy(g_ui.pic_buf, pixels, UI_PICTURE_BYTES);
        g_ui.pic_valid = true;
        if (g_ui.photo_obj != NULL)
        {
            lv_obj_invalidate(g_ui.photo_obj);  /* the open page shows the new one */
        }
    }
    else
    {
        g_ui.pic_valid = false;
        ui_photo_close();
    }
    render_picture_card();
}

bool Ui_PictureShown(void)
{
    return g_ui.pic_valid;
}

bool Ui_PhotoShown(void)
{
    return g_ui.photo_obj != NULL;
}

void Ui_ShowBanner(const char *text)
{
    char *head[4];
    char *nl;
    unsigned long seconds;
    unsigned long slot;

    if ((text == NULL) || (g_ui.prompt_id != UI_NO_PROMPT) || g_ui.screen_off)
    {
        return; /* never over a prompt; does not wake a dark screen */
    }
    (void)snprintf(g_ui.banner_text, sizeof(g_ui.banner_text), "%s", text);

    /* The header fields: seconds and slot. */
    (void)memcpy(s_banner_scratch, g_ui.banner_text, sizeof(s_banner_scratch));
    nl = strchr(s_banner_scratch, '\n');
    if (nl != NULL)
    {
        *nl = '\0';
    }
    split_fields(s_banner_scratch, head, 4U);
    seconds = strtoul(head[2], NULL, 10);
    if ((seconds == 0UL) || (seconds > BANNER_MAX_S))
    {
        seconds = BANNER_DEFAULT_S;
    }
    slot = ((head[3][0] >= '0') && (head[3][0] <= '9')) ? strtoul(head[3], NULL, 10) : UI_DASH_NO_SLOT;
    g_ui.banner_ms = (uint32_t)seconds * 1000U;
    g_ui.banner_slot = (slot < UI_DASH_CARDS) ? (uint8_t)slot : (uint8_t)UI_DASH_NO_SLOT;
    g_ui.banner_tick = lv_tick_get();
    g_ui.banner_on = true;

    if (g_ui.banner_obj != NULL)
    {
        lv_obj_delete(g_ui.banner_obj);
        g_ui.banner_obj = NULL;
        g_ui.banner_count = NULL;
    }
    build_banner_view();
    if (g_ui.banner_timer != NULL)
    {
        lv_timer_resume(g_ui.banner_timer);
    }
}

bool Ui_BannerShown(void)
{
    return g_ui.banner_on;
}

void Ui_ShowPrompt(uint8_t id, const char *text)
{
    ui_banner_close(); /* a banner never covers a prompt */
    (void)snprintf(g_ui.prompt_text, sizeof(g_ui.prompt_text), "%s", (text != NULL) ? text : "");
    g_ui.prompt_id = id;
    g_ui.prompt_answered = false;
    g_ui.prompt_tick = lv_tick_get();
    if (g_ui.prompt_obj != NULL)
    {
        lv_obj_delete(g_ui.prompt_obj);
        g_ui.prompt_obj = NULL;
        g_ui.prompt_count = NULL;
    }
    build_prompt_view();
    ui_wake_screen();
}

void Ui_ClosePrompt(uint8_t id)
{
    if (g_ui.prompt_id != id)
    {
        return;
    }
    g_ui.prompt_id = UI_NO_PROMPT;
    close_prompt_view();
}

uint16_t Ui_PromptShown(void)
{
    return g_ui.prompt_id;
}
