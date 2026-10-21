/**
 * @file    ui_music.c
 * @brief   Music tab and the playback state (ADR-008, #32).
 *
 *     ┌──────────────────────────────────────────┐
 *     │ ┌────────┐  Title of the track           │
 *     │ │ cover  │  Artist                       │
 *     │ │120x120 │  Album                        │
 *     │ │        │  Spotify                      │
 *     │ └────────┘                               │
 *     │ 1:23 ━━━━━━━━━━━━━━━━━━──────────── 3:45 │  progress
 *     │          [|<]    [ > ]    [>|]           │  controls
 *     └──────────────────────────────────────────┘
 *
 * What plays comes from the host as a media record (title ␟ artist ␟
 * album ␟ app), a state (status, enabled controls, position, duration) and
 * a 120 x 120 RGB565 cover. The board counts the position on while
 * playing, as the clock. The Dashboard's bottom strip shows the same
 * state: its middle button is pause while something plays, play
 * otherwise. Without a media session the tab says so; the controls still
 * work (the host sends media keys then).
 */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define PAD_M      16
#define TEXT_X     (PAD_M + (int32_t)UI_ART_SIZE + PAD_M)  /* 152 */
#define TEXT_W     (UI_CONTENT_W - TEXT_X - PAD_M)          /* 256 */
#define TIME_W     44
#define PROGRESS_Y 156
#define CTRL_Y     188
#define CTRL_H     64
#define SEP        '\x1F'

/* Working copy of the media record, split in place. */
static char s_media_scratch[UI_MEDIA_BYTES + 1U];

/* Position now: counted on while playing, up to the duration. */
static uint32_t position_now(void)
{
    uint32_t pos = g_ui.media_position;

    if (g_ui.media_status == UI_MEDIA_PLAYING)
    {
        pos += lv_tick_elaps(g_ui.media_tick) / 1000U;
    }
    if ((g_ui.media_duration != 0U) && (pos > g_ui.media_duration))
    {
        pos = g_ui.media_duration;
    }
    return pos;
}

static void set_time_text(lv_obj_t *label, uint32_t seconds)
{
    if (label == NULL)
    {
        return;
    }
    if (seconds >= 3600U)
    {
        lv_label_set_text_fmt(label, "%u:%02u:%02u", (unsigned)(seconds / 3600U), (unsigned)((seconds / 60U) % 60U),
                              (unsigned)(seconds % 60U));
    }
    else
    {
        lv_label_set_text_fmt(label, "%u:%02u", (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
    }
}

static void render_progress(void)
{
    const bool known = (g_ui.media_status != UI_MEDIA_NONE) && (g_ui.media_duration != 0U);
    const uint32_t pos = position_now();

    if ((g_ui.music_bar == NULL) || (g_ui.music_pos == NULL) || (g_ui.music_dur == NULL))
    {
        return;
    }
    if (!known)
    {
        lv_obj_add_flag(g_ui.music_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.music_dur, LV_OBJ_FLAG_HIDDEN);
        if (g_ui.media_status == UI_MEDIA_NONE)
        {
            lv_obj_add_flag(g_ui.music_pos, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_remove_flag(g_ui.music_pos, LV_OBJ_FLAG_HIDDEN);  /* position of a stream */
            set_time_text(g_ui.music_pos, pos);
        }
        return;
    }
    lv_obj_remove_flag(g_ui.music_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_ui.music_pos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_ui.music_dur, LV_OBJ_FLAG_HIDDEN);
    /* Per mille, so a long track still moves smoothly. */
    lv_bar_set_value(g_ui.music_bar, (int32_t)(((uint64_t)pos * 1000U) / g_ui.media_duration), LV_ANIM_OFF);
    set_time_text(g_ui.music_pos, pos);
    set_time_text(g_ui.music_dur, g_ui.media_duration);
}

static void set_button(lv_obj_t *btn, bool enabled)
{
    if (btn != NULL)
    {
        lv_obj_set_state(btn, LV_STATE_DISABLED, !enabled);
    }
}

static void set_label(lv_obj_t *label, const char *text)
{
    if (label == NULL)
    {
        return;
    }
    lv_label_set_text(label, text);
    if (text[0] == '\0')
    {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_media_render(void)
{
    const bool playing = (g_ui.media_status == UI_MEDIA_PLAYING);
    const bool session = (g_ui.media_status != UI_MEDIA_NONE);
    const char *fields[4] = { "", "", "", "" };
    char app[UI_MEDIA_BYTES + 32U];

    /* Without a session the host sends media keys: every control works. */
    const bool can_prev = !session || ((g_ui.media_controls & UI_MEDIA_CAN_PREVIOUS) != 0U);
    const bool can_play = !session || ((g_ui.media_controls & UI_MEDIA_CAN_PLAY_PAUSE) != 0U);
    const bool can_next = !session || ((g_ui.media_controls & UI_MEDIA_CAN_NEXT) != 0U);

    if (g_ui.dash_play_label != NULL)
    {
        lv_label_set_text_static(g_ui.dash_play_label, playing ? SYM_PAUSE : SYM_PLAY);
    }
    if (g_ui.music_title == NULL)
    {
        return;
    }

    (void)snprintf(s_media_scratch, sizeof(s_media_scratch), "%s", g_ui.media_text);
    {
        char *p = s_media_scratch;

        for (uint32_t i = 0U; (i < 4U) && (p != NULL); i++)
        {
            char *sep = strchr(p, SEP);

            fields[i] = p;
            if (sep != NULL)
            {
                *sep = '\0';
                p = sep + 1;
            }
            else
            {
                p = NULL;
            }
        }
    }

    if (!session)
    {
        set_label(g_ui.music_title, ui_text(TXT_NOTHING_PLAYING));
        set_label(g_ui.music_artist, "");
        set_label(g_ui.music_album, "");
        set_label(g_ui.music_app, "");
    }
    else
    {
        set_label(g_ui.music_title, (fields[0][0] != '\0') ? fields[0] : "-");
        set_label(g_ui.music_artist, fields[1]);
        set_label(g_ui.music_album, fields[2]);
        if ((g_ui.media_status == UI_MEDIA_PAUSED) && (fields[3][0] != '\0'))
        {
            (void)snprintf(app, sizeof(app), "%s  \xC2\xB7  %s", fields[3], ui_text(TXT_MEDIA_PAUSED));
        }
        else if (g_ui.media_status == UI_MEDIA_PAUSED)
        {
            (void)snprintf(app, sizeof(app), "%s", ui_text(TXT_MEDIA_PAUSED));
        }
        else
        {
            (void)snprintf(app, sizeof(app), "%s", fields[3]);
        }
        set_label(g_ui.music_app, app);
    }

    /* Cover, or the music note. */
    if (session && g_ui.art_valid && (g_ui.art_buf != NULL))
    {
        lv_image_set_src(g_ui.music_art, &g_ui.art_dsc);
        lv_obj_invalidate(g_ui.music_art);
        lv_obj_remove_flag(g_ui.music_art, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.music_art_ph, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(g_ui.music_art, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_ui.music_art_ph, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_static(g_ui.music_play_label, playing ? SYM_PAUSE : SYM_PLAY);
    set_button(g_ui.music_btns[0], can_prev);
    set_button(g_ui.music_btns[1], can_play);
    set_button(g_ui.music_btns[2], can_next);
    render_progress();
}

void ui_media_tick(void)
{
    if (g_ui.media_status == UI_MEDIA_PLAYING)
    {
        render_progress();
    }
}

void ui_on_media_clicked(lv_event_t *e)
{
    const uint8_t action = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    if (g_ui.dash.media != NULL)
    {
        g_ui.dash.media(action, g_ui.dash_user);
    }
}

/* ----------------------------------------------------------------- build */

static lv_obj_t *make_control(lv_obj_t *parent, int32_t w, int32_t h, const lv_font_t *font, const char *symbol,
                              uint8_t action, bool accent, lv_obj_t **label)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *l;

    lv_obj_set_size(btn, w, h);
    ui_style_card(btn, h / 2);
    l = ui_make_label(btn, font, accent ? g_ui.pal.on_acc : g_ui.pal.text, symbol);
    lv_obj_center(l);
    if (accent)
    {
        lv_obj_set_style_bg_color(btn, ui_color(g_ui.pal.accent), 0);
    }
    lv_obj_add_event_cb(btn, ui_on_media_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    if (label != NULL)
    {
        *label = l;
    }
    return btn;
}

static lv_obj_t *make_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = ui_make_label(parent, font, color, "");

    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

void ui_music_build(void)
{
    lv_obj_t *tab = g_ui.music_tab;
    lv_obj_t *cover;
    lv_obj_t *note;
    lv_obj_t *texts;
    lv_obj_t *row;

    /* Cover: the image over a card with the music note. */
    cover = ui_make_box(tab);
    lv_obj_set_pos(cover, PAD_M, PAD_M);
    lv_obj_set_size(cover, (int32_t)UI_ART_SIZE, (int32_t)UI_ART_SIZE);
    g_ui.music_art_ph = ui_make_box(cover);
    lv_obj_set_size(g_ui.music_art_ph, LV_PCT(100), LV_PCT(100));
    ui_style_card(g_ui.music_art_ph, BTN_RADIUS);
    note = ui_make_label(g_ui.music_art_ph, &ui_icons_40, g_ui.pal.muted, SYM_MUSIC);
    lv_obj_center(note);
    g_ui.music_art = lv_image_create(cover);
    lv_obj_set_pos(g_ui.music_art, 0, 0);
    lv_obj_add_flag(g_ui.music_art, LV_OBJ_FLAG_HIDDEN);

    /* Texts beside it. */
    texts = ui_make_box(tab);
    lv_obj_set_pos(texts, TEXT_X, PAD_M + 4);
    lv_obj_set_size(texts, TEXT_W, (int32_t)UI_ART_SIZE);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 4, 0);
    g_ui.music_title = make_text(texts, &ui_font_22, g_ui.pal.text);
    lv_label_set_long_mode(g_ui.music_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_height(g_ui.music_title, 56, 0);  /* two lines of 22 px */
    g_ui.music_artist = make_text(texts, &ui_font_16, g_ui.pal.text);
    g_ui.music_album = make_text(texts, &ui_font_14, g_ui.pal.muted);
    g_ui.music_app = make_text(texts, &ui_font_14, g_ui.pal.muted);

    /* Progress: position, bar, duration. */
    row = ui_make_box(tab);
    lv_obj_set_pos(row, PAD_M, PROGRESS_Y);
    lv_obj_set_size(row, UI_CONTENT_W - (2 * PAD_M), 20);
    g_ui.music_pos = ui_make_label(row, &ui_font_14, g_ui.pal.muted, "");
    lv_obj_align(g_ui.music_pos, LV_ALIGN_LEFT_MID, 0, 0);
    g_ui.music_dur = ui_make_label(row, &ui_font_14, g_ui.pal.muted, "");
    lv_obj_align(g_ui.music_dur, LV_ALIGN_RIGHT_MID, 0, 0);
    g_ui.music_bar = lv_bar_create(row);
    lv_obj_set_size(g_ui.music_bar, UI_CONTENT_W - (2 * PAD_M) - (2 * TIME_W) - 16, 6);
    lv_obj_align(g_ui.music_bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(g_ui.music_bar, 0, 1000);
    lv_obj_set_style_bg_color(g_ui.music_bar, ui_color(g_ui.pal.track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui.music_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui.music_bar, ui_color(g_ui.pal.accent), LV_PART_INDICATOR);

    /* Controls. */
    row = ui_make_box(tab);
    lv_obj_set_pos(row, 0, CTRL_Y);
    lv_obj_set_size(row, UI_CONTENT_W, CTRL_H);
    lv_obj_set_style_pad_column(row, 28, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_ui.music_btns[0] = make_control(row, 64, 52, &ui_font_22, SYM_BACKWARD, UI_MEDIA_PREVIOUS, false, NULL);
    g_ui.music_btns[1] = make_control(row, 84, 64, &ui_icons_40, SYM_PLAY, UI_MEDIA_PLAY_PAUSE, true,
                                      &g_ui.music_play_label);
    g_ui.music_btns[2] = make_control(row, 64, 52, &ui_font_22, SYM_FORWARD, UI_MEDIA_NEXT, false, NULL);

    ui_media_render();
}

/* ------------------------------------------------------------------- API */

void Ui_SetMediaArtBuffer(uint8_t *buffer)
{
    g_ui.art_buf = buffer;
    g_ui.art_valid = false;
    (void)memset(&g_ui.art_dsc, 0, sizeof(g_ui.art_dsc));
    g_ui.art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_ui.art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_ui.art_dsc.header.w = UI_ART_SIZE;
    g_ui.art_dsc.header.h = UI_ART_SIZE;
    g_ui.art_dsc.header.stride = UI_ART_SIZE * 2U;
    g_ui.art_dsc.data_size = UI_ART_BYTES;
    g_ui.art_dsc.data = buffer;
    ui_media_render();
}

void Ui_SetMedia(const char *text)
{
    (void)snprintf(g_ui.media_text, sizeof(g_ui.media_text), "%s", (text != NULL) ? text : "");
    ui_media_render();
}

void Ui_SetMediaState(uint8_t status, uint8_t controls, uint32_t position_s, uint32_t duration_s)
{
    g_ui.media_status = (status <= UI_MEDIA_PAUSED) ? status : UI_MEDIA_NONE;
    g_ui.media_controls = controls;
    g_ui.media_position = position_s;
    g_ui.media_duration = duration_s;
    g_ui.media_tick = lv_tick_get();
    ui_media_render();
}

void Ui_SetMediaArt(const uint8_t *pixels)
{
    if ((pixels != NULL) && (g_ui.art_buf != NULL))
    {
        (void)memcpy(g_ui.art_buf, pixels, UI_ART_BYTES);
        g_ui.art_valid = true;
    }
    else
    {
        g_ui.art_valid = false;
    }
    ui_media_render();
}

uint8_t Ui_MediaStatus(void)
{
    return g_ui.media_status;
}

uint32_t Ui_MediaPosition(void)
{
    return position_now();
}
