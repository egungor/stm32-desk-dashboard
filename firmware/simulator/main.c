/**
 * @file    main.c
 * @brief   Headless LVGL simulator for firmware/ui.
 *
 * Renders the UI into a 480 x 272 RGB565 framebuffer (the LCD format) and
 * plays a fixed script of UI states and simulated touches. After each step
 * the framebuffer is written as a raw dump, <out>/<name>.bin, which
 * tools/fb2png.py converts to PNG (tools/run-ui-simulator.sh does both).
 *
 * The script also checks the UI behaviour (selection callback, paging) and
 * exits with a non-zero status if a check fails, so CI can run it.
 *
 * Usage: ui_simulator <output directory>
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"

#define WIDTH  480
#define HEIGHT 272

static uint16_t s_framebuffer[WIDTH * HEIGHT];
static uint16_t s_turned[WIDTH * HEIGHT];  /* what the panel shows when flipped */
static uint32_t s_now_ms;
static const char *s_out_dir;
static int s_failures;

static struct
{
    bool    pressed;
    int32_t x;
    int32_t y;
} s_touch;

static struct
{
    int     calls;
    uint8_t last_id;
} s_select;

static struct
{
    int     calls;
    int     released;
    uint8_t last;
    int     mute_calls;
    bool    mute_wanted;
} s_audio;

static int s_flip_calls;

static struct
{
    int  calls;
    bool on;
} s_backlight;

/* Stand-in for the host's preference store in the edit mode tests: applies
 * an edit to its list and sends the new list, as the host does. */
static struct
{
    UiDevice list[8];
    size_t   count;
    int      favorite_calls;
    int      alias_calls;
    int      move_calls;
    uint8_t  last_id;
    uint8_t  last_position;
    char     last_alias[UI_MAX_ALIAS_BYTES + 1U];
} s_host;

static struct
{
    int        calls;
    UiSettings last;
} s_settings;

/* ---------------------------------------------------------------- platform */

static uint32_t tick_cb(void)
{
    return s_now_ms;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* Direct mode: LVGL draws into s_framebuffer itself. */
    LV_UNUSED(area);
    LV_UNUSED(px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);
    data->point.x = s_touch.x;
    data->point.y = s_touch.y;
    data->state = s_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Advance simulated time in small steps so timers and input run as on the
 * board (the firmware calls lv_timer_handler about every 5 ms). */
static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0U; t < ms; t += 5U)
    {
        s_now_ms += 5U;
        (void)lv_timer_handler();
    }
}

/* Application screen (ADR-005): the rail is 56 px wide; the Audio tab's
 * content starts right of it. */
#define RAIL_DASH_X  28
#define RAIL_DASH_Y  30
#define RAIL_MUSIC_X 28
#define RAIL_MUSIC_Y 82
#define RAIL_AUDIO_X 28
#define RAIL_AUDIO_Y 134
#define RAIL_COG_X   28
#define RAIL_COG_Y   208
/* Layout A 2x2 in the Audio tab: the top-left card, the sub-tabs, the
 * pencil, and the bottom strip (mute 64..116, the slider 136..272 while
 * the pager is shown). */
#define CARD0_X      162
#define CARD0_Y      85
#define TAB_ALL_X    122
#define TAB_FAV_X    238
#define PENCIL_X     454
#define MUTE_X       90
#define MUTE_EDGE_X  115
#define SLIDER_X(p)  (136 + (((p) * 136) / 100)) /* p: percent of the track */

static void tap(int32_t x, int32_t y)
{
    s_touch.x = x;
    s_touch.y = y;
    s_touch.pressed = true;
    run_ms(100U);
    s_touch.pressed = false;
    run_ms(100U);
}

/* Press at (x0, y), move to x1 in 10 px steps, release. */
static void drag(int32_t x0, int32_t x1, int32_t y)
{
    const int32_t step = (x1 > x0) ? 10 : -10;

    s_touch.x = x0;
    s_touch.y = y;
    s_touch.pressed = true;
    run_ms(60U);
    while ((step > 0) ? (s_touch.x < x1) : (s_touch.x > x1))
    {
        s_touch.x += step;
        run_ms(20U);
    }
    s_touch.x = x1;
    run_ms(60U);
    s_touch.pressed = false;
    run_ms(60U);
}

/* Press at (x, y0), move to y1 in 10 px steps, release (scrolling). */
static void drag_v(int32_t x, int32_t y0, int32_t y1)
{
    const int32_t step = (y1 > y0) ? 10 : -10;

    s_touch.x = x;
    s_touch.y = y0;
    s_touch.pressed = true;
    run_ms(60U);
    while ((step > 0) ? (s_touch.y < y1) : (s_touch.y > y1))
    {
        s_touch.y += step;
        run_ms(20U);
    }
    s_touch.y = y1;
    run_ms(60U);
    s_touch.pressed = false;
    run_ms(600U); /* let the scroll come to rest */
}

/* Press at (x, y) for @p ms, release. */
static void hold(int32_t x, int32_t y, uint32_t ms)
{
    s_touch.x = x;
    s_touch.y = y;
    s_touch.pressed = true;
    run_ms(ms);
    s_touch.pressed = false;
    run_ms(100U);
}

/* Write the framebuffer; @p turned: as the board's display port shows it
 * when flipped (pixel order reversed). */
static void snapshot_as(const char *name, bool turned)
{
    char path[512];
    const uint16_t *pixels = s_framebuffer;
    FILE *f;

    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    if (turned)
    {
        for (size_t i = 0U; i < (size_t)(WIDTH * HEIGHT); i++)
        {
            s_turned[(WIDTH * HEIGHT) - 1U - i] = s_framebuffer[i];
        }
        pixels = s_turned;
    }
    (void)snprintf(path, sizeof(path), "%s/%s.bin", s_out_dir, name);
    f = fopen(path, "wb");
    if ((f == NULL) || (fwrite(pixels, sizeof(s_framebuffer), 1U, f) != 1U))
    {
        printf("FAIL cannot write %s\n", path);
        s_failures++;
    }
    else
    {
        printf("wrote %s\n", path);
    }
    if (f != NULL)
    {
        (void)fclose(f);
    }
}

static void snapshot(const char *name)
{
    snapshot_as(name, false);
}

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        printf("FAIL %s\n", what);
        s_failures++;
    }
}

static void on_select(uint8_t device_id, void *user)
{
    LV_UNUSED(user);
    s_select.calls++;
    s_select.last_id = device_id;
}

static void on_volume(uint8_t volume, bool released, void *user)
{
    LV_UNUSED(user);
    s_audio.calls++;
    s_audio.released += released ? 1 : 0;
    s_audio.last = volume;
}

static void on_backlight(bool on, void *user)
{
    LV_UNUSED(user);
    s_backlight.calls++;
    s_backlight.on = on;
}

static size_t host_find(uint8_t id)
{
    for (size_t i = 0U; i < s_host.count; i++)
    {
        if (s_host.list[i].id == id)
        {
            return i;
        }
    }
    return s_host.count;
}

static bool host_set_favorite(uint8_t id, bool favorite, void *user)
{
    const size_t i = host_find(id);

    LV_UNUSED(user);
    s_host.favorite_calls++;
    s_host.last_id = id;
    if (i < s_host.count)
    {
        s_host.list[i].favorite = favorite ? 1U : 0U;
        Ui_SetDevices(s_host.list, s_host.count);
    }
    return true;
}

static bool host_set_alias(uint8_t id, const char *alias, void *user)
{
    const size_t i = host_find(id);

    LV_UNUSED(user);
    s_host.alias_calls++;
    s_host.last_id = id;
    (void)snprintf(s_host.last_alias, sizeof(s_host.last_alias), "%s", alias);
    if (i < s_host.count)
    {
        (void)snprintf(s_host.list[i].alias, sizeof(s_host.list[i].alias), "%s", alias);
        Ui_SetDevices(s_host.list, s_host.count);
    }
    return true;
}

static bool host_move(uint8_t id, uint8_t new_position, void *user)
{
    const size_t i = host_find(id);

    LV_UNUSED(user);
    s_host.move_calls++;
    s_host.last_id = id;
    s_host.last_position = new_position;
    if ((i < s_host.count) && (new_position < s_host.count))
    {
        const UiDevice d = s_host.list[i];

        (void)memmove(&s_host.list[i], &s_host.list[i + 1U], (s_host.count - i - 1U) * sizeof(UiDevice));
        (void)memmove(&s_host.list[new_position + 1U], &s_host.list[new_position],
                      (s_host.count - new_position - 1U) * sizeof(UiDevice));
        s_host.list[new_position] = d;
        Ui_SetDevices(s_host.list, s_host.count);
    }
    return true;
}

static void on_flip(void *user)
{
    LV_UNUSED(user);
    s_flip_calls++;
}

static void on_settings(const UiSettings *settings, void *user)
{
    LV_UNUSED(user);
    s_settings.calls++;
    s_settings.last = *settings;
}

/* LVGL memory after the step before, and its peak so far. */
static void report_memory(const char *when)
{
    uint32_t used;
    uint32_t peak;

    Ui_MemoryUse(&used, &peak);
    printf("memory %-22s used=%lu peak=%lu of %lu\n", when, (unsigned long)used, (unsigned long)peak,
           (unsigned long)LV_MEM_SIZE);
    check(peak < (LV_MEM_SIZE * 9U) / 10U, "LVGL memory peak stays below 90 % of LV_MEM_SIZE");
}

/* Apply @p s, let LVGL redraw, take a screenshot and check the memory. */
static void apply_and_snapshot(const UiSettings *s, const char *name)
{
    Ui_ApplySettings(s);
    run_ms(50U);
    snapshot(name);
    report_memory(name);
}

static void on_mute(bool muted, void *user)
{
    LV_UNUSED(user);
    s_audio.mute_calls++;
    s_audio.mute_wanted = muted;
}

/* ---------------------------------------------------------------- script */

static UiDevice device(uint8_t id, uint8_t icon, const char *name)
{
    UiDevice d;

    memset(&d, 0, sizeof(d));
    d.id = id;
    d.icon = icon;
    (void)snprintf(d.name, sizeof(d.name), "%s", name);
    return d;
}

/* M7 (#60): tabs, themes, accent colours, grids, language. */
static void run_settings_script(const UiDevice *base, size_t count)
{
    UiDevice devices[8];
    UiSettings s = *Ui_Settings();

    memcpy(devices, base, count * sizeof(UiDevice));
    devices[0].favorite = 1U;
    devices[3].favorite = 1U;
    devices[4].favorite = 1U;
    (void)snprintf(devices[1].alias, sizeof(devices[1].alias), "%s", "Oyun kulakl\xC4\xB1\xC4\x9F\xC4\xB1");
    Ui_SetDevices(devices, count);
    Ui_SetActive(1U);
    Ui_SetVolume(45U);
    Ui_SetMuted(0U);
    run_ms(50U);
    snapshot("20-favorites-marked");
    report_memory("start");

    /* The first settings choose the startup screen (Audio) and the startup
     * tab: "last used" = favorites. */
    s.startup_tab = UI_STARTUP_LAST;
    s.last_tab = UI_TAB_FAVORITES;
    s.startup_screen = UI_STARTUP_SCREEN_AUDIO;
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    Ui_ApplySettings(&s);
    check(Ui_CurrentAppTab() == UI_APP_TAB_AUDIO, "startup screen \"Audio\" shows the Audio tab");
    check(Ui_CurrentTab() == UI_TAB_FAVORITES, "startup tab \"last used\" opens the favorites tab");
    run_ms(50U);
    snapshot("21-tab-favorites");

    /* Tapping "All" switches back and reports last_tab. */
    tap(TAB_ALL_X, 18);
    check(Ui_CurrentTab() == UI_TAB_ALL, "tapping the first tab shows all devices");
    check((s_settings.calls == 1) && (s_settings.last.last_tab == UI_TAB_ALL),
          "a tab change reports the settings with last_tab");
    tap(TAB_FAV_X, 18);
    check(Ui_CurrentTab() == UI_TAB_FAVORITES, "tapping the second tab shows the favorites");
    check((s_settings.calls == 2) && (s_settings.last.last_tab == UI_TAB_FAVORITES), "last_tab follows the tab");
    /* The first favorite is device 0 (top left). */
    tap(CARD0_X, CARD0_Y);
    check(s_select.last_id == 0U, "a tap in the favorites tab selects the shown device");
    tap(TAB_ALL_X, 18);

    s = *Ui_Settings();
    s.theme = UI_THEME_LIGHT;
    apply_and_snapshot(&s, "22-light");
    s.accent = UI_ACCENT_BLUE;
    apply_and_snapshot(&s, "23-light-blue");
    s.theme = UI_THEME_DARK;
    s.accent = UI_ACCENT_GREEN;
    apply_and_snapshot(&s, "24-dark-green");
    s.accent = UI_ACCENT_ORANGE;
    s.grid = UI_GRID_3X2;
    apply_and_snapshot(&s, "25-grid-3x2");
    s.grid = UI_GRID_3X3;
    apply_and_snapshot(&s, "26-grid-3x3");
    s.grid = UI_GRID_LIST;
    apply_and_snapshot(&s, "27-grid-list");
    s.theme = UI_THEME_LIGHT;
    apply_and_snapshot(&s, "28-list-light");
    s.theme = UI_THEME_DARK;
    s.grid = UI_GRID_2X2;
    s.language = UI_LANGUAGE_ENGLISH;
    apply_and_snapshot(&s, "29-english");
    Ui_ShowNotice(UI_NOTICE_HOST_TIMEOUT);
    run_ms(50U);
    snapshot("30-english-notice");
    Ui_ShowWaiting();
    run_ms(50U);
    snapshot("31-english-waiting");

    /* A rebuild on the waiting screen stays on the waiting screen. */
    s.language = UI_LANGUAGE_TURKISH;
    Ui_ApplySettings(&s);
    check(lv_screen_active() != NULL, "a screen is loaded after a rebuild");
    Ui_SetDevices(devices, count);
    check(Ui_CurrentPage() == 0U, "page within range after a rebuild");

    /* Out-of-range settings are ignored. */
    s.accent = UI_ACCENT_COUNT;
    Ui_ApplySettings(&s);
    check(Ui_Settings()->accent == UI_ACCENT_ORANGE, "an out-of-range accent is ignored");

    /* Many rebuilds do not leak LVGL memory. */
    {
        uint32_t before;
        uint32_t after;

        s = *Ui_Settings();
        Ui_MemoryUse(&before, NULL);
        for (uint32_t i = 0U; i < 20U; i++)
        {
            s.theme = (uint8_t)(i % 2U);
            s.grid = (uint8_t)(i % UI_GRID_COUNT);
            Ui_ApplySettings(&s);
            run_ms(20U);
        }
        s.theme = UI_THEME_DARK;
        s.grid = UI_GRID_2X2;
        Ui_ApplySettings(&s);
        run_ms(20U);
        Ui_MemoryUse(&after, NULL);
        printf("memory after 21 rebuilds: before=%lu after=%lu\n", (unsigned long)before, (unsigned long)after);
        check(after <= before + 512U, "rebuilding the screens does not leak memory");
    }
}

/* Flip row after the short drag in run_settings_page_script. */
#define SET_FLIP_Y 137

/* Tap at (x, y) and check that it reported the settings exactly once. */
static void tap_setting(int32_t x, int32_t y, const char *what)
{
    const int calls = s_settings.calls;

    tap(x, y);
    run_ms(50U); /* the rebuild after an appearance change runs here */
    check(s_settings.calls == calls + 1, what);
    check(Ui_CurrentScreen() == UI_SCREEN_SETTINGS, "the settings page stays open after a change");
}

/* Settings page (#60; sections since ADR-005). Rows are 48 px high under
 * the 36 px bar; the controls sit at the right edge. */
static void run_settings_page_script(const UiDevice *devices, size_t count)
{
    uint32_t base;
    uint32_t used;
    int calls;

    tap(RAIL_COG_X, RAIL_COG_Y); /* cog on the rail */
    check(Ui_CurrentScreen() == UI_SCREEN_SETTINGS, "the cog button opens the settings page");
    run_ms(50U);
    snapshot("40-settings");
    report_memory("40-settings");

    /* GENEL from the top: theme 90, colour 138, language 186, screen
     * timeout 234. */
    tap_setting(425, 90, "the theme control reports the settings");
    check(Ui_Settings()->theme == UI_THEME_LIGHT, "\"Light\" sets the light theme");
    tap_setting(307, 138, "the colour swatches report the settings");
    check(Ui_Settings()->accent == UI_ACCENT_BLUE, "the second swatch sets the blue accent");
    snapshot("41-settings-light-blue");
    report_memory("41-settings-light-blue");
    tap_setting(363, 234, "the screen timeout control reports the settings");
    check(Ui_Settings()->screen_timeout == 5U, "\"5 min\" sets a 5 minute screen timeout");

    /* Tapping the option that is already chosen reports nothing. */
    calls = s_settings.calls;
    tap(363, 234);
    check(s_settings.calls == calls, "the chosen option again reports nothing");

    /* A short drag down the page; one that starts on a control scrolls
     * and changes nothing. Then: flip FLIP_Y, startup screen FLIP_Y + 48. */
    calls = s_settings.calls;
    drag_v(240, 250, 150);
    check((s_settings.calls == calls) && (Ui_Settings()->screen_timeout == 5U),
          "scrolling over a control does not change it");
    snapshot("42-settings-middle");
    tap_setting(444, SET_FLIP_Y, "the flip switch reports the settings");
    check(Ui_Settings()->flipped == 1U, "the flip switch asks for the flipped display");
    /* Startup screen: Dashboard, Music, Audio (the rail's order). */
    tap_setting(340, SET_FLIP_Y + 48, "the startup screen control reports the settings");
    check(Ui_Settings()->startup_screen == UI_STARTUP_SCREEN_MUSIC, "\"M\xC3\xBCzik\" sets the Music tab as startup screen");
    tap_setting(250, SET_FLIP_Y + 48, "the startup screen control reports the settings");
    check(Ui_Settings()->startup_screen == UI_STARTUP_SCREEN_DASHBOARD,
          "\"Dashboard\" sets the Dashboard tab as startup screen");

    /* SES at the end of the list: layout 95, grid 143, startup tab 190,
     * volume limit 239. */
    drag_v(240, 250, 40);
    drag_v(240, 250, 40);
    snapshot("42-settings-bottom");
    tap_setting(312, 143, "the grid control reports the settings");
    check(Ui_Settings()->grid == UI_GRID_3X2, "\"3x2\" sets the 3x2 grid");
    tap_setting(422, 190, "the startup tab control reports the settings");
    check(Ui_Settings()->startup_tab == UI_STARTUP_FAVORITES, "\"Favorites\" sets the startup tab");

    tap_setting(444, 239, "the volume limit switch reports the settings");
    check(Ui_Settings()->volume_limit_on == 1U, "the volume limit switch turns the limit on");
    drag_v(240, 250, 40);
    run_ms(50U);
    snapshot("43-settings-limit");

    /* The slider reports once, on release, in steps of 5 %. */
    calls = s_settings.calls;
    drag(212, 300, 219);
    check(s_settings.calls == calls + 1, "the volume limit slider reports once, on release");
    check((Ui_Settings()->volume_limit > 50U) && ((Ui_Settings()->volume_limit % 5U) == 0U),
          "the volume limit moved up in steps of 5");
    snapshot("44-settings-limit-moved");

    /* Back to the top: the language row. */
    drag_v(240, 40, 250);
    drag_v(240, 40, 250);
    drag_v(240, 40, 250);
    tap_setting(419, 186, "the language control reports the settings");
    check(Ui_Settings()->language == UI_LANGUAGE_ENGLISH, "\"English\" sets the English UI");
    snapshot("45-settings-english");
    report_memory("45-settings-english");

    /* A new device list or settings from the host leave the page open. */
    {
        UiSettings s = *Ui_Settings();

        Ui_SetDevices(devices, count);
        check(Ui_CurrentScreen() == UI_SCREEN_SETTINGS, "a new device list leaves the settings page open");
        s.accent = UI_ACCENT_GREEN;
        Ui_ApplySettings(&s);
        run_ms(50U);
        check(Ui_CurrentScreen() == UI_SCREEN_SETTINGS, "settings from the host leave the settings page open");
    }

    /* Back to the main screen: the settings chosen above are in use. */
    tap(60, 18);
    run_ms(50U);
    check(Ui_CurrentScreen() == UI_SCREEN_MAIN, "the back button shows the main screen");
    snapshot("46-main-after-settings");

    /* Opening and closing it again and again does not leak. */
    Ui_MemoryUse(&base, NULL);
    for (uint32_t i = 0U; i < 10U; i++)
    {
        tap(RAIL_COG_X, RAIL_COG_Y);
        tap(60, 18);
    }
    run_ms(50U);
    Ui_MemoryUse(&used, NULL);
    printf("memory after 10 settings pages: before=%lu after=%lu\n", (unsigned long)base, (unsigned long)used);
    check(used <= base + 512U, "opening the settings page does not leak memory");

    /* The waiting screen closes the page. */
    tap(RAIL_COG_X, RAIL_COG_Y);
    Ui_ShowWaiting();
    run_ms(50U);
    check(Ui_CurrentScreen() == UI_SCREEN_WAITING, "the waiting screen replaces the settings page");
    report_memory("end");
}

/* Screen timeout (#60): dark after settings.screen_timeout minutes without
 * a touch; the first touch only wakes the screen. */
static void run_screen_timeout_script(const UiDevice *devices, size_t count)
{
    UiSettings s = *Ui_Settings();
    int selects;

    Ui_SetDevices(devices, count);
    Ui_SetActive(1U);
    s.screen_timeout = 0U;
    s.grid = UI_GRID_2X2;
    s.language = UI_LANGUAGE_TURKISH;
    Ui_ApplySettings(&s);
    run_ms(120000U);
    check(Ui_ScreenOn() && (s_backlight.calls == 0), "with the timeout off the screen stays lit");

    s.screen_timeout = 1U;
    Ui_ApplySettings(&s);
    run_ms(58000U);
    check(Ui_ScreenOn(), "the screen is lit before the timeout");
    run_ms(3000U);
    check(!Ui_ScreenOn() && (s_backlight.calls == 1) && !s_backlight.on,
          "after 1 minute without a touch the screen goes dark once");
    snapshot("50-screen-off");

    /* The waking touch does not select the device under it. */
    selects = s_select.calls;
    tap(CARD0_X, CARD0_Y);
    check(Ui_ScreenOn() && (s_backlight.calls == 2) && s_backlight.on, "a touch wakes the screen");
    check(s_select.calls == selects, "the waking touch does not reach the controls");
    tap(CARD0_X, CARD0_Y);
    check(s_select.calls == selects + 1, "the next touch works normally");
    snapshot("51-screen-woken");

    /* A touch restarts the count. */
    run_ms(40000U);
    tap(240, 160);
    run_ms(40000U);
    check(Ui_ScreenOn(), "a touch restarts the timeout");
    run_ms(25000U);
    check(!Ui_ScreenOn(), "dark again a minute after the last touch");

    /* "Never" from the host lights a dark screen and keeps it lit. */
    s.screen_timeout = 0U;
    Ui_ApplySettings(&s);
    check(Ui_ScreenOn() && s_backlight.on, "switching the timeout off wakes the screen");
    run_ms(70000U);
    check(Ui_ScreenOn(), "and it stays lit");
}

/* Keyboard keys (Turkish Q layout, 480 x 220 under a 52 px bar): rows
 * 50 px high; row 1 has 12 equal keys, row 3 starts with a mode key. */
#define KB_ROW1_Y  81
#define KB_ROW3_Y  189
#define KB_ROW4_Y  243
static int32_t kb_row1_x(int32_t index) { return 4 + (index * 40) + 18; }
static int32_t kb_row3_x(int32_t letter) { return 62 + (letter * 40) + 18; } /* 0 = z */
#define KB_MODE_X  31   /* "ABC" / "abc" */
#define KB_OK_X    442
#define KB_CLOSE_X 28
#define KB_CLOSE_Y 26

/* Edit mode (#60): favorite, order and alias through UiEditHandlers. Cards
 * 2 x 2; a card's buttons (left, right, star, pencil) on its bottom row. */
static void run_edit_script(const UiDevice *devices, size_t count)
{
    static const UiEditHandlers handlers = { host_set_favorite, host_set_alias, host_move };
    uint32_t base;
    uint32_t used;
    int calls;

    memcpy(s_host.list, devices, count * sizeof(UiDevice));
    s_host.count = count;
    Ui_SetDevices(s_host.list, s_host.count);
    Ui_SetActive(1U);
    Ui_SetEditHandlers(&handlers, NULL);
    run_ms(50U);
    snapshot("60-main-pencil");
    Ui_MemoryUse(&base, NULL);

    tap(PENCIL_X, 18); /* pencil */
    check(Ui_CurrentScreen() == UI_SCREEN_EDIT, "the pencil button opens the edit mode");
    run_ms(50U);
    snapshot("61-edit");
    report_memory("61-edit");

    tap(128, 106); /* first card: star */
    check((s_host.favorite_calls == 1) && (s_host.last_id == 0U) && (s_host.list[0].favorite == 1U),
          "the star marks the first device as favorite");
    check(Ui_CurrentScreen() == UI_SCREEN_EDIT, "the edit mode stays open when the new list arrives");

    calls = s_host.move_calls;
    tap(36, 106); /* first card: left, disabled */
    check(s_host.move_calls == calls, "the first device cannot move further left");
    tap(82, 106); /* first card: right */
    check((s_host.move_calls == calls + 1) && (s_host.last_id == 0U) && (s_host.last_position == 1U) &&
              (s_host.list[1].id == 0U),
          "the right arrow moves the device one place on");
    run_ms(50U);
    snapshot("62-edit-moved");

    /* Alias: pencil of the first card (now device 1), type "Oyun", OK. */
    tap(174, 106);
    check(Ui_CurrentScreen() == UI_SCREEN_KEYBOARD, "the pencil on a card opens the keyboard");
    run_ms(50U);
    snapshot("63-keyboard");
    report_memory("63-keyboard");
    tap(KB_MODE_X, KB_ROW3_Y);   /* ABC */
    tap(kb_row1_x(8), KB_ROW1_Y); /* O */
    tap(KB_MODE_X, KB_ROW3_Y);   /* abc */
    tap(kb_row1_x(5), KB_ROW1_Y); /* y */
    tap(kb_row1_x(6), KB_ROW1_Y); /* u */
    tap(kb_row3_x(5), KB_ROW3_Y); /* n */
    tap(kb_row1_x(7), KB_ROW1_Y); /* ı */
    run_ms(50U);
    snapshot("64-keyboard-typed");
    tap(KB_OK_X, KB_ROW4_Y);
    run_ms(50U);
    check((s_host.alias_calls == 1) && (s_host.last_id == 1U) &&
              (strcmp(s_host.last_alias, "Oyun\xC4\xB1") == 0),
          "OK on the keyboard sets the typed alias");
    check(Ui_CurrentScreen() == UI_SCREEN_EDIT, "saving the alias returns to the edit mode");

    /* The alias is limited to UI_MAX_ALIAS_BYTES of UTF-8 ("\xC4\x9F" is 2 bytes). */
    tap(174 + 236, 106); /* second card's pencil */
    for (uint32_t i = 0U; i < 20U; i++)
    {
        tap(kb_row1_x(10), KB_ROW1_Y); /* ğ */
    }
    tap(KB_OK_X, KB_ROW4_Y);
    run_ms(50U);
    check((s_host.alias_calls == 2) && (strlen(s_host.last_alias) == UI_MAX_ALIAS_BYTES),
          "the alias stops at 32 bytes");

    /* Close without saving. */
    tap(174 + 236, 106);
    tap(kb_row1_x(0), KB_ROW1_Y);
    Ui_SetDevices(s_host.list, s_host.count);
    check(Ui_CurrentScreen() == UI_SCREEN_KEYBOARD, "a new list leaves the keyboard open");
    tap(KB_CLOSE_X, KB_CLOSE_Y);
    run_ms(50U);
    check((s_host.alias_calls == 2) && (Ui_CurrentScreen() == UI_SCREEN_EDIT), "x closes the keyboard without saving");

    /* Pages: 6 devices, 4 per page. */
    tap(452, 252);
    run_ms(50U);
    snapshot("65-edit-page2");

    tap(440, 18); /* Done */
    run_ms(50U);
    check(Ui_CurrentScreen() == UI_SCREEN_MAIN, "Done returns to the main screen");
    snapshot("66-main-after-edit");

    /* Opening and closing does not leak. */
    for (uint32_t i = 0U; i < 5U; i++)
    {
        tap(PENCIL_X, 18);
        tap(174, 106);
        tap(KB_CLOSE_X, KB_CLOSE_Y);
        tap(440, 18);
    }
    run_ms(50U);
    Ui_MemoryUse(&used, NULL);
    printf("memory after 5 edit sessions: before=%lu after=%lu\n", (unsigned long)base, (unsigned long)used);
    check(used <= base + 512U, "the edit mode and keyboard do not leak memory");
    check(Ui_CurrentScreen() == UI_SCREEN_MAIN, "back on the main screen");
    report_memory("edit-end");
}

/* Layouts B (list) and C (large tile), #60. */
static void run_layouts_script(void)
{
    UiSettings s = *Ui_Settings();
    int selects;
    int volumes;
    uint32_t before;
    uint32_t after;

    Ui_SetActive(1U);
    Ui_SetVolume(45U);
    Ui_SetMuted(0U);
    s.theme = UI_THEME_DARK;
    s.accent = UI_ACCENT_ORANGE;
    s.language = UI_LANGUAGE_TURKISH;
    s.layout = UI_LAYOUT_LIST;
    apply_and_snapshot(&s, "70-list");
    check(Ui_CurrentScreen() == UI_SCREEN_MAIN, "the list layout shows the main screen");

    /* B: rows from y 44, 44 px apart, under layout A's top bar. */
    selects = s_select.calls;
    tap(200, 44 + 44 + 20); /* second row */
    check(s_select.calls == selects + 1, "a tap on a row selects its device");
    tap(TAB_FAV_X, 18); /* favorites */
    check(Ui_CurrentTab() == UI_TAB_FAVORITES, "the top bar's star shows the favorites");
    run_ms(50U);
    snapshot("71-list-favorites");
    tap(TAB_ALL_X, 18);
    check(Ui_CurrentTab() == UI_TAB_ALL, "the top bar's first tab shows all devices");

    /* Vertical volume on the right. */
    volumes = s_audio.calls;
    drag_v(448, 150, 70);
    check((s_audio.calls > volumes) && (s_audio.last > 45U), "dragging the vertical slider up raises the volume");
    Ui_SetVolume(s_audio.last);
    s.theme = UI_THEME_LIGHT;
    apply_and_snapshot(&s, "72-list-light");

    /* C: the active device in the panel, the others as tiles. */
    s.layout = UI_LAYOUT_TILES;
    s.theme = UI_THEME_DARK;
    apply_and_snapshot(&s, "73-tiles");
    selects = s_select.calls;
    tap(300, 80); /* first tile */
    check((s_select.calls == selects + 1) && (s_select.last_id != 1U),
          "the first tile is not the active device and selects its own");
    Ui_SetActive(s_select.last_id);
    run_ms(50U);
    snapshot("74-tiles-switched");
    s.theme = UI_THEME_LIGHT;
    s.accent = UI_ACCENT_PURPLE;
    apply_and_snapshot(&s, "75-tiles-light");

    /* The layout row on the settings page: back to cards. */
    tap(RAIL_COG_X, RAIL_COG_Y); /* cog */
    check(Ui_CurrentScreen() == UI_SCREEN_SETTINGS, "the rail's cog opens the settings page in layout C");
    drag_v(240, 250, 40); /* SES (below "Açılış ekranı", #32): layout 188, grid 237 */
    tap_setting(277, 188, "the layout control reports the settings");
    check(Ui_Settings()->layout == UI_LAYOUT_CARDS, "\"Kart\" sets the cards layout");
    tap_setting(351, 188, "the layout control reports the settings again");
    check(Ui_Settings()->layout == UI_LAYOUT_LIST, "\"Liste\" sets the list layout");
    snapshot("76-settings-layout-list");
    {
        const int before = s_settings.calls;

        tap(312, 237); /* "3x2", greyed out outside the cards layout */
        check((s_settings.calls == before) && (Ui_Settings()->grid == UI_GRID_2X2),
              "the grid row does nothing outside the cards layout");
    }
    tap(60, 18);
    run_ms(50U);
    check(Ui_CurrentScreen() == UI_SCREEN_MAIN, "back on the main screen in the list layout");

    /* Switching layouts again and again does not leak. */
    s = *Ui_Settings();
    Ui_MemoryUse(&before, NULL);
    for (uint32_t i = 0U; i < 12U; i++)
    {
        s.layout = (uint8_t)(i % UI_LAYOUT_COUNT);
        s.theme = (uint8_t)((i / 3U) % 2U);
        Ui_ApplySettings(&s);
        run_ms(20U);
    }
    s.layout = UI_LAYOUT_CARDS;
    s.theme = UI_THEME_DARK;
    s.accent = UI_ACCENT_ORANGE;
    s.grid = UI_GRID_2X2;
    Ui_ApplySettings(&s);
    run_ms(20U);
    Ui_MemoryUse(&after, NULL);
    printf("memory after 13 layout changes: before=%lu after=%lu\n", (unsigned long)before, (unsigned long)after);
    report_memory("layouts-end");
}

/* Dashboard tab (ADR-006, #14): cards from the host, the generic detail,
 * the bottom strip and the clock. Cards are 200 x 92 at x 64 / 272 and
 * y 36 / 136; the media buttons at x 86, 142, 198, the mute button at x
 * 258 and the volume slider from about x 300 to 420, y 250 (#32). */
#define DASH_STRIP_Y 250
/* Status icons at the top right (ADR-010): the bell with "3", the robot. */
#define IND_Y        18
#define IND_BELL_X   408
#define PIC_CARD_X   374 /* the picture card, bottom right */
#define PIC_CARD_Y   180
#define IND_CLAUDE_X 456
#define DASH_PREV_X  86
#define DASH_PLAY_X  142
#define DASH_NEXT_X  198
#define DASH_MUTE_X  258
/* Music tab (#32): controls at x 138, 240, 342, y 220; the volume column
 * on the right, x 424 to 480 (#70). */
#define MUSIC_CTRL_Y 220
#define MUSIC_PREV_X 138
#define MUSIC_PLAY_X 240
#define MUSIC_NEXT_X 342
#define MUSIC_BAR_Y  166 /* the progress bar, x 124 to 356 (#55) */
#define MUSIC_BAR_MID_X 240
#define MUSIC_VOL_X  448

static struct
{
    int     open_calls;
    uint8_t open_slot;
    int8_t  open_arg;
    int     close_calls;
    int     action_calls;
    uint8_t action_slot;
    uint8_t action;
    int     media_calls;
    uint8_t media;
    int     seek_calls;
    uint32_t seek_s;
    int     prompt_calls;
    uint8_t prompt_id;
    uint8_t prompt_action;
} s_dash;

static void dash_open(uint8_t slot, int8_t arg, void *user)
{
    (void)user;
    s_dash.open_calls++;
    s_dash.open_slot = slot;
    s_dash.open_arg = arg;
}

static void dash_close(void *user)
{
    (void)user;
    s_dash.close_calls++;
}

static void dash_action(uint8_t slot, uint8_t action, void *user)
{
    (void)user;
    s_dash.action_calls++;
    s_dash.action_slot = slot;
    s_dash.action = action;
}

static void dash_media(uint8_t action, void *user)
{
    (void)user;
    s_dash.media_calls++;
    s_dash.media = action;
}

static void dash_seek(uint32_t seconds, void *user)
{
    (void)user;
    s_dash.seek_calls++;
    s_dash.seek_s = seconds;
}

#define F "\x1F"

static void dash_prompt(uint8_t id, uint8_t action, void *user)
{
    LV_UNUSED(user);
    s_dash.prompt_calls++;
    s_dash.prompt_id = id;
    s_dash.prompt_action = action;
}

/* Prompt texts as host/Hook/ApprovalService makes them (#26). */
static const char *const prompt_bash =
    "Claude Code izin istiyor" "\x1F" "ayrac \xC2\xB7 1/2" "\x1F" "60\n"
    "\x1F" "Ara\xC3\xA7" "\x1F" "Bash\n"
    "\x1F" "Komut" "\x1F" "npm install --save-dev vitest\n"
    "\x1F" "A\xC3\xA7\xC4\xB1klama" "\x1F" "Test arac\xC4\xB1n\xC4\xB1 kur\n"
    "!1" "\x1F" "Onayla\n"
    "!2" "\x1F" "Reddet\n"
    "!3" "\x1F" "Terminalde sor";
static const char *const prompt_cut =
    "Claude Code izin istiyor" "\x1F" "ayrac \xC2\xB7 2/2" "\x1F" "60\n"
    "\x1F" "Ara\xC3\xA7" "\x1F" "Write\n"
    "\x1F" "Dosya" "\x1F" "src/test/long.test.ts\n"
    "\x1F" "\xC4\xB0\xC3\xA7" "erik" "\x1F" "import { describe, it, expect } from 'vitest'; \xC2\xB6 \xC2\xB6 describe('long'\xE2\x80\xA6\n"
    "\x1F" "\x1F" "(k\xC4\xB1salt\xC4\xB1ld\xC4\xB1: tamam\xC4\xB1 ekrana s\xC4\xB1\xC4\x9Fm\xC4\xB1yor, burada onaylanamaz)\n"
    "!2" "\x1F" "Reddet\n"
    "!3" "\x1F" "Terminalde sor";

static void run_prompt_script(UiSettings *s);

static void run_dashboard_script(void)
{
    static const UiDashHandlers handlers = { dash_open, dash_close, dash_action, dash_media, dash_prompt, dash_seek };
    /* Card and detail as host/Weather/WeatherCard makes them (#18). */
    /* Card as host/Weather/WeatherCard makes it (ADR-010). */
    static const char *const weather_card =
        "3" F "Ankara" F "" F "3D86C6" F "L\n"
        "18\xC2\xB0\n"
        "11\xC2\xB0 / 21\xC2\xB0";
    static const char *const weather_detail =
        "Hava durumu" F "\xC4\xB0stanbul \xC2\xB7 14:00" F "\n"
        "#\xC5\x9Eimdi\n"
        "3" F "18\xC2\xB0" F "Par\xC3\xA7" "al\xC4\xB1 bulutlu\n"
        "0" F "Hissedilen" F "16\xC2\xB0\n"
        "0" F "Nem" F "%65\n"
        "0" F "R\xC3\xBCzg\xC3\xA2r" F "12 km/s\n"
        "#10 g\xC3\xBCn\n"
        "3" F "Bug\xC3\xBCn" F "11\xC2\xB0 / 21\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %20\n"
        "7" F "Yar\xC4\xB1n" F "13\xC2\xB0 / 17\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %85\n"
        "5" F "Cum 23" F "11\xC2\xB0 / 16\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %30\n"
        "1" F "Cmt 24" F "10\xC2\xB0 / 20\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %0\n"
        "7" F "Paz 25" F "10\xC2\xB0 / 18\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %60\n"
        "10" F "Pzt 26" F "10\xC2\xB0 / 16\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %90\n"
        "6" F "Sal 27" F "0\xC2\xB0 / 15\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %10\n"
        "9" F "\xC3\x87" "ar 28" F "-4\xC2\xB0 / 4\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %70\n"
        "3" F "Per 29" F "5\xC2\xB0 / 13\xC2\xB0 \xC2\xB7 Ya\xC4\x9F\xC4\xB1\xC5\x9F %5\n"
        "5" F "Cum 30" F "6\xC2\xB0 / 13\xC2\xB0";
    /* Card and detail as host/Menu/MenuService makes them (#20). */
    static const char *const menu_card =
        "11" F "" F "" F "3E9B5F" F "\n"
        "Ka\xC4\x9F\xC4\xB1t kebab\xC4\xB1\n"
        "Tarhana \xC3\xA7orba\n"
        "Meyve / f\xC4\xB1st\xC4\xB1kl\xC4\xB1 baklava";
    static const char *const menu_detail =
        "Yemek" F "5 Ekim Pazartesi" F "n\n"
        F "Ana yemek" F "Ka\xC4\x9F\xC4\xB1t kebab\xC4\xB1\n"
        F "Yan yemek" F "Kalem b\xC3\xB6rek\n"
        F "\xC3\x87orba" F "Tarhana \xC3\xA7orba\n"
        F "Tatl\xC4\xB1" F "Meyve / f\xC4\xB1st\xC4\xB1kl\xC4\xB1 baklava\n"
        F "Yan\xC4\xB1nda" F "Yo\xC4\x9Furt / \xC3\xBCz\xC3\xBCm komposto\n"
        "#Salata bar\n"
        F F "Akdeniz salata \xC2\xB7 Havu\xC3\xA7 tarator \xC2\xB7 Zyt. b\xC3\xB6r\xC3\xBClce \xC2\xB7 "
        "Cevizli biber \xC2\xB7 Makarna salata \xC2\xB7 Zeytin salata\n"
        /* not sent by the host's menu: tests the generic button */
        "!1" F "Yenile";
    UiSettings s = *Ui_Settings();

    Ui_SetDashHandlers(&handlers, NULL);
    Ui_SetTime(1792593125U); /* 2026-10-21 14:32:05, a Wednesday */
    Ui_SetCard(0U, weather_card);
    Ui_SetCard(1U, menu_card);
    Ui_SetCard(7U, "ignored");
    {
        /* The bell (3 toasts, not seen) and the Claude Code icon (ADR-010). */
        static const UiIndicator ind[2] = { { 12U, UI_INDICATOR_RED, 3U, 2U }, { 13U, UI_INDICATOR_NORMAL, 0U, 3U } };

        Ui_SetIndicators(ind, 2U);
    }
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);
    check(Ui_CurrentAppTab() == UI_APP_TAB_DASHBOARD, "the rail shows the Dashboard tab");
    check(Ui_DashView() == UI_DASH_VIEW_CARDS, "the Dashboard tab shows the cards");
    snapshot("80-dashboard");
    report_memory("80-dashboard");

    /* The now-playing card opens the Music tab. */
    tap(164, 182);
    run_ms(50U);
    check((s_dash.open_calls == 0) && (Ui_CurrentAppTab() == UI_APP_TAB_MUSIC), "the now-playing card opens the Music tab");
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);

    /* Weather: tap, loading, detail. */
    tap(164, 82);
    check((s_dash.open_calls == 1) && (s_dash.open_slot == 0U) && (s_dash.open_arg == 0),
          "a tap on a card asks for its detail");
    check(Ui_DashView() == UI_DASH_VIEW_LOADING, "the detail shows \"loading\" until it arrives");
    run_ms(50U);
    snapshot("81-detail-loading");
    Ui_ShowDetail(1U, menu_detail); /* another slot: ignored */
    check(Ui_DashView() == UI_DASH_VIEW_LOADING, "a detail for another card is ignored");
    Ui_ShowDetail(0U, weather_detail);
    run_ms(50U);
    check(Ui_DashView() == UI_DASH_VIEW_DETAIL, "the detail is shown when it arrives");
    snapshot("82-detail-weather");
    report_memory("82-detail-weather");

    snapshot("82b-detail-back-button");
    /* Back to the cards: a touch a little below the button counts (#49: the
     * panel reports touches lower than the finger). */
    tap(90, 42);
    run_ms(50U);
    check((Ui_DashView() == UI_DASH_VIEW_CARDS) && (s_dash.close_calls == 1),
          "a tap just below the back button closes the detail");

    /* Menu: previous / next and a button. */
    tap(372, 82);
    check((s_dash.open_calls == 2) && (s_dash.open_slot == 1U), "the menu card asks for its detail");
    Ui_ShowDetail(1U, menu_detail);
    run_ms(50U);
    snapshot("83-detail-menu");
    tap(460, 18);
    check((s_dash.open_calls == 3) && (s_dash.open_arg == 1) && (Ui_DashView() == UI_DASH_VIEW_LOADING),
          "next asks for the next day");
    Ui_ShowDetail(1U, menu_detail);
    run_ms(50U);
    tap(396, 18); /* previous (#49: 48 px keys) */
    check((s_dash.open_calls == 4) && (s_dash.open_arg == -1), "previous asks for the previous day");
    Ui_ShowDetail(1U, menu_detail);
    run_ms(50U);
    tap(268, 250);
    check((s_dash.action_calls == 1) && (s_dash.action_slot == 1U) && (s_dash.action == 1U),
          "a detail button sends its action");

    /* A rebuild (e.g. light theme from the host) keeps the detail open. */
    s.theme = UI_THEME_LIGHT;
    Ui_ApplySettings(&s);
    run_ms(50U);
    check(Ui_DashView() == UI_DASH_VIEW_DETAIL, "a rebuild keeps the detail open");
    snapshot("84-detail-menu-light");
    s.theme = UI_THEME_DARK;
    Ui_ApplySettings(&s);
    run_ms(50U);

    /* No detail: back to the cards with a notice. */
    tap(76, 18);
    tap(IND_CLAUDE_X, IND_Y);
    check((Ui_DashView() == UI_DASH_VIEW_LOADING) && (s_dash.open_slot == 3U),
          "the Claude Code icon asks for its detail (slot 3)");
    Ui_DetailFailed();
    run_ms(50U);
    check(Ui_DashView() == UI_DASH_VIEW_CARDS, "a failed detail returns to the cards");
    snapshot("85-detail-failed");
    run_ms(UI_NOTICE_MS + 100U);

    /* Media controls on the bottom strip. */
    tap(DASH_PREV_X, DASH_STRIP_Y);
    check((s_dash.media_calls == 1) && (s_dash.media == UI_MEDIA_PREVIOUS), "|< sends previous track");
    tap(DASH_PLAY_X, DASH_STRIP_Y);
    check((s_dash.media_calls == 2) && (s_dash.media == UI_MEDIA_PLAY_PAUSE), "play sends play/pause");
    tap(DASH_NEXT_X, DASH_STRIP_Y);
    check((s_dash.media_calls == 3) && (s_dash.media == UI_MEDIA_NEXT), ">| sends next track");

    /* Claude Code prompt (ADR-007, #26). */
    run_prompt_script(&s);

    /* The clock runs on; a card update replaces the card. */
    run_ms(60000U);
    Ui_SetCard(0U, "0" F "HAVA" F "yeni\n9\xC2\xB0  Ya\xC4\x9Fmurlu");
    run_ms(50U);
    snapshot("86-dashboard-updated");

    /* Opening and closing details does not leak. */
    {
        uint32_t before;
        uint32_t after;

        Ui_MemoryUse(&before, NULL);
        for (uint32_t i = 0U; i < 10U; i++)
        {
            tap(372, 82);
            Ui_ShowDetail(1U, menu_detail);
            run_ms(20U);
            tap(76, 18);
            run_ms(20U);
        }
        Ui_MemoryUse(&after, NULL);
        printf("memory after 10 details: before=%lu after=%lu\n", (unsigned long)before, (unsigned long)after);
        check(after <= before + 512U, "opening and closing details does not leak memory");
    }
    report_memory("dashboard-end");
}

/* The prompt over every screen: buttons answer once, the host closes it,
 * it wakes a dark screen and closes itself after its countdown. Buttons
 * of the 3-button strip at x 106 / 236 / 370, y 236. */
static void run_prompt_script(UiSettings *s)
{
    Ui_ShowPrompt(5U, prompt_bash);
    run_ms(50U);
    check(Ui_PromptShown() == 5U, "a prompt is shown over the Dashboard tab");
    snapshot("87-prompt");
    tap(106, 236);
    check((s_dash.prompt_calls == 1) && (s_dash.prompt_id == 5U) && (s_dash.prompt_action == 1U),
          "\"Onayla\" answers the prompt with action 1");
    tap(236, 236);
    check(s_dash.prompt_calls == 1, "a prompt is answered only once");
    run_ms(50U);
    snapshot("88-prompt-answered");
    Ui_ClosePrompt(4U);
    check(Ui_PromptShown() == 5U, "closing another prompt changes nothing");
    Ui_ClosePrompt(5U);
    run_ms(50U);
    check(Ui_PromptShown() == UI_NO_PROMPT, "the host closes the prompt");

    /* A request that did not fit: only Reddet and Terminalde sor; over the
     * Audio tab; it wakes the dark screen. */
    s->screen_timeout = 1U;
    Ui_ApplySettings(s);
    tap(RAIL_AUDIO_X, RAIL_AUDIO_Y);
    run_ms(61000U + UI_SCREEN_OFF_CHECK_MS);
    check(!Ui_ScreenOn(), "the screen went dark");
    Ui_ShowPrompt(6U, prompt_cut);
    run_ms(50U);
    check(Ui_ScreenOn() && (Ui_PromptShown() == 6U), "a prompt wakes the screen");
    snapshot("89-prompt-cut");
    run_ms(2U * 60000U);
    check(Ui_ScreenOn() || (Ui_PromptShown() == UI_NO_PROMPT), "a prompt keeps the screen lit until it closes");
    check(Ui_PromptShown() == UI_NO_PROMPT, "a prompt closes itself after its countdown");
    tap(170, 140); /* wakes the screen if it went dark after the prompt */
    run_ms(50U);
    s->screen_timeout = 0U;
    Ui_ApplySettings(s);
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);
}

/* Now playing (ADR-008, #32): the Dashboard's bottom strip with the volume
 * and the play / pause state, and the Music tab. */
static uint8_t s_art_shown[UI_ART_BYTES];
static uint8_t s_art_in[UI_ART_BYTES];

static void run_music_script(void)
{
    int calls;

    Ui_SetMediaArtBuffer(s_art_shown);
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);

    /* The strip's volume is the Audio tab's: either moves the other. */
    Ui_SetVolume(45U);
    Ui_SetMuted(0U);
    run_ms(50U);
    calls = s_audio.calls;
    drag(320, 400, DASH_STRIP_Y);
    check(s_audio.calls > calls, "the Dashboard's slider sets the volume");
    check((Ui_ShownVolume() != UI_VOLUME_UNKNOWN) && (Ui_ShownVolume() > 45U),
          "the Audio tab's slider follows the Dashboard's");
    Ui_SetVolume(30U);
    run_ms(50U);
    calls = s_audio.mute_calls;
    tap(DASH_MUTE_X, DASH_STRIP_Y);
    check((s_audio.mute_calls == calls + 1) && s_audio.mute_wanted, "the Dashboard's speaker button mutes");
    Ui_SetMuted(1U);
    run_ms(50U);
    snapshot("90-dashboard-strip-muted");
    Ui_SetMuted(0U);

    /* Nothing plays: the tab says so; the controls still work (media keys). */
    tap(RAIL_MUSIC_X, RAIL_MUSIC_Y);
    run_ms(50U);
    check(Ui_CurrentAppTab() == UI_APP_TAB_MUSIC, "the rail's music note shows the Music tab");
    check(Ui_MediaStatus() == UI_MEDIA_NONE, "nothing plays before the host says so");
    snapshot("91-music-none");
    calls = s_dash.media_calls;
    tap(MUSIC_PLAY_X, MUSIC_CTRL_Y);
    check((s_dash.media_calls == calls + 1) && (s_dash.media == UI_MEDIA_PLAY_PAUSE),
          "play works without a media session");

    /* Playing, with a cover (a colour gradient). */
    for (uint32_t y = 0U; y < UI_ART_SIZE; y++)
    {
        for (uint32_t x = 0U; x < UI_ART_SIZE; x++)
        {
            const uint32_t v = ((x * 31U / (UI_ART_SIZE - 1U)) << 11) | ((y * 63U / (UI_ART_SIZE - 1U)) << 5) | 12U;
            const uint32_t i = ((y * UI_ART_SIZE) + x) * 2U;

            s_art_in[i] = (uint8_t)(v & 0xFFU);
            s_art_in[i + 1U] = (uint8_t)(v >> 8);
        }
    }
    Ui_SetMedia("Bohemian Rhapsody" F "Queen" F "A Night at the Opera" F "Spotify");
    Ui_SetMediaState(UI_MEDIA_PLAYING, UI_MEDIA_CAN_PREVIOUS | UI_MEDIA_CAN_PLAY_PAUSE | UI_MEDIA_CAN_NEXT, 83U, 354U);
    Ui_SetMediaArt(s_art_in);
    run_ms(50U);
    check(Ui_MediaStatus() == UI_MEDIA_PLAYING, "the state from the host is shown");
    check(memcmp(s_art_shown, s_art_in, UI_ART_BYTES) == 0, "the cover is copied into the UI's buffer");
    snapshot("92-music-playing");
    run_ms(10000U);
    check((Ui_MediaPosition() >= 92U) && (Ui_MediaPosition() <= 94U), "the position counts on while playing");
    snapshot("92-music-playing-later");
    tap(MUSIC_PREV_X, MUSIC_CTRL_Y);
    check(s_dash.media == UI_MEDIA_PREVIOUS, "|< on the Music tab sends previous track");
    tap(MUSIC_NEXT_X, MUSIC_CTRL_Y);
    check(s_dash.media == UI_MEDIA_NEXT, ">| on the Music tab sends next track");

    /* The progress bar (#55): only a session that can seek takes a tap. */
    calls = s_dash.seek_calls;
    tap(MUSIC_BAR_MID_X, MUSIC_BAR_Y);
    check(s_dash.seek_calls == calls, "the bar ignores taps when the session cannot seek");
    Ui_SetMediaState(UI_MEDIA_PLAYING,
                     UI_MEDIA_CAN_PREVIOUS | UI_MEDIA_CAN_PLAY_PAUSE | UI_MEDIA_CAN_NEXT | UI_MEDIA_CAN_SEEK, 83U, 354U);
    run_ms(50U);
    snapshot("92-music-seekable");
    tap(MUSIC_BAR_MID_X, MUSIC_BAR_Y);
    run_ms(50U);
    check((s_dash.seek_calls == calls + 1) && (s_dash.seek_s >= 170U) && (s_dash.seek_s <= 184U),
          "a tap in the middle of the bar seeks to the middle of the track");
    check((Ui_MediaPosition() >= 170U) && (Ui_MediaPosition() <= 184U), "the bar shows the new position at once");
    tap(MUSIC_BAR_MID_X + 70, MUSIC_BAR_Y - 12); /* within the larger touch area */
    run_ms(50U);
    check((s_dash.seek_calls == calls + 2) && (s_dash.seek_s > 250U), "a tap just above the bar, further right");

    /* The volume on the right edge (#70): dragging it up raises the volume. */
    {
        const int volumes = s_audio.calls;

        drag_v(MUSIC_VOL_X, 190, 60);
        check((s_audio.calls > volumes) && (s_audio.last > 60U), "the Music tab's volume slider sets the volume");
        Ui_SetVolume(s_audio.last);
        run_ms(50U);
    }

    /* Paused, long Turkish texts, no cover, no duration, only play/pause. */
    Ui_SetMedia("\xC3\x87ok uzun bir \xC5\x9F" "ark\xC4\xB1 ad\xC4\xB1: G\xC3\xBCl\xC3\xBC Soldurmam - "
                "Canl\xC4\xB1 Kay\xC4\xB1t, Harbiye A\xC3\xA7\xC4\xB1khava 1998" F "Sezen Aksu" F "" F "Chrome");
    Ui_SetMediaState(UI_MEDIA_PAUSED, UI_MEDIA_CAN_PLAY_PAUSE, 120U, 0U);
    Ui_SetMediaArt(NULL);
    run_ms(5000U);
    check(Ui_MediaPosition() == 120U, "a paused position does not count on");
    snapshot("93-music-paused");
    calls = s_dash.media_calls;
    tap(MUSIC_NEXT_X, MUSIC_CTRL_Y);
    check(s_dash.media_calls == calls, "a control the session does not take is disabled");

    /* Counting stops at the end of the track. */
    Ui_SetMediaState(UI_MEDIA_PLAYING, UI_MEDIA_CAN_PLAY_PAUSE, 350U, 354U);
    run_ms(10000U);
    check(Ui_MediaPosition() == 354U, "the position stops at the duration");

    /* The Dashboard's middle button shows pause while playing. */
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);
    snapshot("94-dashboard-playing");

    /* A rebuild (light theme) keeps the state and the cover. */
    {
        UiSettings light = *Ui_Settings();

        light.theme = UI_THEME_LIGHT;
        Ui_SetMediaArt(s_art_in);
        Ui_ApplySettings(&light);
        run_ms(50U);
        tap(RAIL_MUSIC_X, RAIL_MUSIC_Y);
        run_ms(50U);
        check(Ui_MediaStatus() == UI_MEDIA_PLAYING, "a rebuild keeps the media state");
        snapshot("95-music-light");
        light.theme = UI_THEME_DARK;
        Ui_ApplySettings(&light);
        run_ms(50U);
    }
    report_memory("music-end");
    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);
}

/* Notifications (ADR-009, #34): the fourth card, its list, and the banner
 * over every screen. Texts as host/Notify/NotificationService makes them. */
static void run_banner_script(void)
{
    static const char *const notify_detail =
        "Bildirimler" F "3 bildirim" F "\n"
        "#WhatsApp \xC2\xB7 14:32\n"
        F "Ay\xC5\x9F" "e" F "Ak\xC5\x9F" "am geliyor musun? Saat yedide kap\xC4\xB1n\xC4\xB1n \xC3\xB6n\xC3\xBC" "nde olal\xC4\xB1m.\n"
        "#Outlook \xC2\xB7 14:13\n"
        F "Toplant\xC4\xB1 notlar\xC4\xB1" F "Per\xC5\x9F" "embe sunumu i\xC3\xA7in son hali ekte.\n"
        "#Microsoft Teams \xC2\xB7 13:58\n"
        F "Ali Y\xC4\xB1lmaz" F "PR'a bakabilir misin?";
    static const char *const banner =
        "12" F "WhatsApp" F "5" F "2\n"
        "Ay\xC5\x9F" "e\n"
        "Ak\xC5\x9F" "am geliyor musun? Saat yedide kap\xC4\xB1n\xC4\xB1n \xC3\xB6n\xC3\xBC" "nde olal\xC4\xB1m, ge\xC3\xA7 kalma.";
    static const char *const banner_no_slot = "12" F "Snipping Tool" F "" F "";
    UiSettings s = *Ui_Settings();
    int opens;

    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);
    snapshot("96-dashboard-notifications");

    Ui_ShowBanner(banner);
    run_ms(50U);
    check(Ui_BannerShown(), "a banner is shown over the Dashboard tab");
    snapshot("97-banner");
    run_ms(4500U);
    check(Ui_BannerShown(), "a banner stays for its seconds");
    run_ms(800U);
    check(!Ui_BannerShown(), "a banner closes itself after its seconds");

    /* Over the Audio tab; a tap opens the notifications detail. */
    tap(RAIL_AUDIO_X, RAIL_AUDIO_Y);
    run_ms(50U);
    Ui_ShowBanner(banner);
    run_ms(50U);
    snapshot("98-banner-audio");
    opens = s_dash.open_calls;
    tap(240, 40);
    run_ms(50U);
    check(!Ui_BannerShown() && (Ui_CurrentAppTab() == UI_APP_TAB_DASHBOARD) && (s_dash.open_calls == opens + 1) &&
              (s_dash.open_slot == 2U) && (s_dash.open_arg == 0),
          "a tap on the banner opens its card's detail on the Dashboard tab");
    Ui_ShowDetail(2U, notify_detail);
    run_ms(50U);
    check(Ui_DashView() == UI_DASH_VIEW_DETAIL, "the notifications detail is shown");
    snapshot("99-notifications-detail");
    tap(76, 18);
    run_ms(50U);

    /* Without a slot a tap only closes it; a newer one replaces the shown one. */
    Ui_ShowBanner(banner);
    Ui_ShowBanner(banner_no_slot);
    run_ms(50U);
    snapshot("100-banner-no-texts");
    opens = s_dash.open_calls;
    tap(240, 30);
    run_ms(50U);
    check(!Ui_BannerShown() && (s_dash.open_calls == opens) && (Ui_DashView() == UI_DASH_VIEW_CARDS),
          "a banner without a slot only closes");
    Ui_ShowBanner(banner_no_slot);
    run_ms(4900U);
    check(Ui_BannerShown(), "an empty seconds field means 5 s");
    run_ms(400U);
    check(!Ui_BannerShown(), "and it closes after 5 s");

    /* Never over a prompt; a prompt closes a shown banner. */
    Ui_ShowPrompt(9U, prompt_bash);
    Ui_ShowBanner(banner);
    run_ms(50U);
    check(!Ui_BannerShown() && (Ui_PromptShown() == 9U), "no banner while a prompt is shown");
    Ui_ClosePrompt(9U);
    Ui_ShowBanner(banner);
    Ui_ShowPrompt(10U, prompt_bash);
    run_ms(50U);
    check(!Ui_BannerShown(), "a prompt closes the banner");
    Ui_ClosePrompt(10U);
    run_ms(50U);

    /* On the settings page a tap only closes it. */
    tap(RAIL_COG_X, RAIL_COG_Y);
    run_ms(50U);
    check(Ui_CurrentScreen() == UI_SCREEN_SETTINGS, "the settings page is open");
    Ui_ShowBanner(banner);
    run_ms(50U);
    snapshot("101-banner-settings");
    opens = s_dash.open_calls;
    tap(240, 40);
    run_ms(50U);
    check(!Ui_BannerShown() && (Ui_CurrentScreen() == UI_SCREEN_SETTINGS) && (s_dash.open_calls == opens),
          "on the settings page a tap only closes the banner");
    tap(60, 18);
    run_ms(50U);

    /* A rebuild (light theme) keeps it; a dark screen drops it and is not woken. */
    Ui_ShowBanner(banner);
    s.theme = UI_THEME_LIGHT;
    Ui_ApplySettings(&s);
    run_ms(50U);
    check(Ui_BannerShown(), "a rebuild keeps the banner");
    snapshot("102-banner-light");
    s.theme = UI_THEME_DARK;
    s.screen_timeout = 1U;
    Ui_ApplySettings(&s);
    run_ms(61000U + UI_SCREEN_OFF_CHECK_MS);
    check(!Ui_ScreenOn() && !Ui_BannerShown(), "the screen went dark, no banner");
    Ui_ShowBanner(banner);
    run_ms(50U);
    check(!Ui_ScreenOn() && !Ui_BannerShown(), "a banner does not wake a dark screen");
    tap(170, 140); /* wakes the screen */
    run_ms(50U);
    s.screen_timeout = 0U;
    Ui_ApplySettings(&s);
    run_ms(50U);

    /* Showing banners again and again does not leak. */
    {
        uint32_t before;
        uint32_t after;

        Ui_MemoryUse(&before, NULL);
        for (uint32_t i = 0U; i < 10U; i++)
        {
            Ui_ShowBanner(banner);
            run_ms(5300U);
        }
        Ui_MemoryUse(&after, NULL);
        printf("memory after 10 banners: before=%lu after=%lu\n", (unsigned long)before, (unsigned long)after);
        check(after <= before + 512U, "banners do not leak memory");
    }
    report_memory("banner-end");
}

/* Dashboard redesign (ADR-010, #47): the picture card, the cover on the
 * now-playing card, the colours of the cards and status icons, the light
 * theme, the morning menu warning. Texts as the host makes them. */
static uint8_t s_pic_shown[UI_PICTURE_BYTES];
static uint8_t s_pic_in[UI_PICTURE_BYTES];

static void run_redesign_script(void)
{
    static const char *const sunny = "1" F "Ankara" F "" F "E59B2E" F "L\n24\xC2\xB0\n12\xC2\xB0 / 26\xC2\xB0";
    static const char *const night_rain = "7" F "Ankara" F "" F "3A6EA5" F "L\n9\xC2\xB0\n7\xC2\xB0 / 13\xC2\xB0";
    static const char *const menu_never =
        "11" F "" F "" F "E0702F" F "\nEtli bamya\nMercimek \xC3\xA7orba\nS\xC3\xBCtla\xC3\xA7";
    static const char *const menu_dislike =
        "11" F "" F "Pzt 26" F "D9A520" F "\nKuru fasulye\nTarhana \xC3\xA7orba\nKazandibi";
    static const char *const warning =
        "Bug\xC3\xBCnk\xC3\xBC men\xC3\xBC" "de" F "22 Ekim Per\xC5\x9F" "embe" F "\n"
        "11" F "\xC4\xB0stemiyorum" F "Etli bamya\n"
        "11" F "Sevmiyorum" F "Tarhana \xC3\xA7orba\n"
        "!1" F "Tamam";
    UiSettings s = *Ui_Settings();

    tap(RAIL_DASH_X, RAIL_DASH_Y);
    run_ms(50U);

    /* The picture: a test pattern (sky, sun, hills) drawn on a 200 x 92
     * grid, stretched to the picture's 480 x 272. */
    for (uint32_t y = 0U; y < UI_PICTURE_H; y++)
    {
        for (uint32_t x = 0U; x < UI_PICTURE_W; x++)
        {
            const uint32_t gx = (x * 200U) / UI_PICTURE_W;
            const uint32_t gy = (y * 92U) / UI_PICTURE_H;
            const int32_t dx = (int32_t)gx - 150;
            const int32_t dy = (int32_t)gy - 30;
            const bool sun = ((dx * dx) + (dy * dy)) < (16 * 16);
            const bool hill =
                (int32_t)gy > (60 + (int32_t)((gx % 80U) < 40U ? (gx % 40U) / 3U : (40U - (gx % 40U)) / 3U));
            uint32_t r = 40U + (y * 120U) / UI_PICTURE_H;
            uint32_t g = 110U + (y * 100U) / UI_PICTURE_H;
            uint32_t b = 220U;
            uint16_t px;

            if (sun)
            {
                r = 255U;
                g = 210U;
                b = 60U;
            }
            else if (hill)
            {
                r = 40U;
                g = 140U - (gy - 60U);
                b = 70U;
            }
            px = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            s_pic_in[(y * UI_PICTURE_W + x) * 2U] = (uint8_t)(px & 0xFFU);
            s_pic_in[(y * UI_PICTURE_W + x) * 2U + 1U] = (uint8_t)(px >> 8);
        }
    }
    Ui_SetPictureBuffer(s_pic_shown);
    check(!Ui_PictureShown(), "no picture before one arrives");
    Ui_SetPicture(s_pic_in);
    run_ms(50U);
    check(Ui_PictureShown(), "the picture card shows the picture");

    /* A track with a cover on the now-playing card. */
    Ui_SetMedia("Gidiyorum" F "Sezen Aksu" F "Deniz Y\xC4\xB1ld\xC4\xB1z\xC4\xB1" F "Spotify");
    Ui_SetMediaState(UI_MEDIA_PLAYING, 7U, 63U, 245U);
    Ui_SetMediaArt(s_art_in);
    Ui_SetCard(0U, sunny);
    Ui_SetCard(1U, menu_never);
    {
        static const UiIndicator ind[2] = { { 12U, UI_INDICATOR_NORMAL, 5U, 2U },
                                            { 13U, UI_INDICATOR_YELLOW, 0U, 3U } };

        Ui_SetIndicators(ind, 2U);
    }
    run_ms(50U);
    check(Ui_IndicatorCount() == 2U, "two status icons");
    snapshot("103-dashboard-redesign");

    /* A tap on the picture card shows the picture full screen; a tap closes it (#53). */
    tap(PIC_CARD_X, PIC_CARD_Y);
    run_ms(50U);
    check(Ui_PhotoShown(), "a tap on the picture card opens the full-screen picture");
    snapshot("107-picture-full-screen");
    tap(240, 136);
    run_ms(50U);
    check(!Ui_PhotoShown() && (Ui_DashView() == UI_DASH_VIEW_CARDS), "a tap closes it");

    /* The bell opens the notifications list (slot 2). */
    {
        const int opens = s_dash.open_calls;

        tap(IND_BELL_X, IND_Y);
        check((s_dash.open_calls == opens + 1) && (s_dash.open_slot == 2U) &&
                  (Ui_DashView() == UI_DASH_VIEW_LOADING),
              "the bell asks for the notifications list");
        tap(76, 18);
        run_ms(50U);
    }

    /* Night rain, "don't like" on Monday, Claude Code waiting (red, 2) and allowed (green). */
    Ui_SetCard(0U, night_rain);
    Ui_SetCard(1U, menu_dislike);
    {
        static const UiIndicator ind[2] = { { 12U, UI_INDICATOR_RED, 120U, 2U },
                                            { 13U, UI_INDICATOR_RED, 2U, 3U } };

        Ui_SetIndicators(ind, 2U);
    }
    run_ms(50U);
    snapshot("104-dashboard-rain-red");
    {
        static const UiIndicator ind[1] = { { 13U, UI_INDICATOR_GREEN, 0U, 3U } };

        Ui_SetIndicators(ind, 1U);
    }
    tap(PIC_CARD_X, PIC_CARD_Y);
    run_ms(50U);
    Ui_SetPicture(NULL);
    run_ms(50U);
    check(!Ui_PictureShown() && Ui_IndicatorCount() == 1U, "no picture: the placeholder; one icon");
    check(!Ui_PhotoShown(), "no picture: the full-screen picture closes");
    tap(PIC_CARD_X, PIC_CARD_Y);
    run_ms(50U);
    check(!Ui_PhotoShown(), "the placeholder opens nothing");

    /* The light theme keeps the colours of the cards. */
    Ui_SetPicture(s_pic_in);
    s.theme = UI_THEME_LIGHT;
    Ui_ApplySettings(&s);
    run_ms(50U);
    check(Ui_PictureShown() && Ui_IndicatorCount() == 1U, "a rebuild keeps the picture and the icons");
    snapshot("105-dashboard-light");
    s.theme = UI_THEME_DARK;
    Ui_ApplySettings(&s);
    run_ms(50U);

    /* The morning warning: a prompt with one button, it wakes the screen. */
    Ui_ShowPrompt(255U, warning);
    run_ms(50U);
    snapshot("106-menu-warning");
    tap(240, 236);
    check((s_dash.prompt_id == 255U) && (s_dash.prompt_action == 1U), "\"Tamam\" answers the warning");
    Ui_ClosePrompt(255U);
    run_ms(50U);
    report_memory("redesign-end");
}

#undef F

static void run_script(void)
{
    /* Example names, including Turkish letters and a long name. */
    const UiDevice devices[] = {
        device(0U, UI_ICON_SPEAKERS, "Hoparl\xC3\xB6rler"),
        device(1U, UI_ICON_HEADPHONES, "Kulakl\xC4\xB1k"),
        device(2U, UI_ICON_DISPLAY, "Monit\xC3\xB6r (HDMI)"),
        device(3U, UI_ICON_HEADSET, "Kablosuz kulakl\xC4\xB1k (Bluetooth)"),
        device(4U, UI_ICON_DIGITAL, "USB DAC"),
        device(5U, UI_ICON_GENERIC, "Dijital \xC3\xA7\xC4\xB1k\xC4\xB1\xC5\x9F (S/PDIF) - "
                                    "\xC3\x87ok uzun bir cihaz ad\xC4\xB1"),
    };

    snapshot("01-waiting");

    Ui_SetDevices(devices, sizeof(devices) / sizeof(devices[0]));
    Ui_SetActive(1U);
    Ui_SetVolume(45U);
    Ui_SetMuted(0U);
    run_ms(50U);
    /* Before any settings the application screen opens on the Dashboard
     * tab (startup_screen 0). */
    check((Ui_CurrentScreen() == UI_SCREEN_MAIN) && (Ui_CurrentAppTab() == UI_APP_TAB_DASHBOARD),
          "the device list opens the main screen on the Dashboard tab");
    snapshot("02-dashboard");
    tap(RAIL_AUDIO_X, RAIL_AUDIO_Y);
    check(Ui_CurrentAppTab() == UI_APP_TAB_AUDIO, "the rail's speaker shows the Audio tab");
    run_ms(50U);
    snapshot("02-main");

    /* Tap "Hoparlörler" (top-left button). */
    tap(CARD0_X, CARD0_Y);
    check(s_select.calls == 1, "tap on a device calls the select callback once");
    check(s_select.last_id == 0U, "tap on the top-left device selects id 0");
    Ui_SetPending(0U);
    run_ms(50U);
    snapshot("03-pending");

    Ui_SetActive(0U);
    run_ms(50U);
    snapshot("04-switched");

    /* Tapping the active device does nothing. */
    tap(CARD0_X, CARD0_Y);
    check(s_select.calls == 1, "tap on the active device does not call the callback");

    /* Next page button (bottom right). */
    tap(452, 252);
    check(Ui_CurrentPage() == 1U, "next page button shows page 2");
    snapshot("05-page2");

    Ui_ShowNotice(UI_NOTICE_HOST_TIMEOUT);
    run_ms(50U);
    snapshot("06-error");
    run_ms(UI_NOTICE_MS + 100U);
    snapshot("07-error-gone");

    /* Drag the volume slider to the right; the host's report while the
     * finger is down does not move the knob. */
    drag(SLIDER_X(30), SLIDER_X(78), 252);
    check(s_audio.calls > 2, "dragging the slider reports several values");
    check(s_audio.released == 1, "releasing the slider reports once with released");
    check(s_audio.last > 45U, "the slider moved up");
    Ui_SetVolume(s_audio.last);
    run_ms(50U);
    snapshot("08-volume");

    /* Mute: the tap asks; the host's MUTE_CHANGED changes the icon. */
    tap(MUTE_X, 252);
    check((s_audio.mute_calls == 1) && s_audio.mute_wanted, "mute button asks to mute");
    Ui_SetMuted(1U);
    run_ms(50U);
    snapshot("09-muted");

    /* The right edge of the mute button belongs to the button: the
     * slider's touch margin no longer covers it, so the tap does not set
     * the volume to the touched point. */
    {
        const int volume_calls = s_audio.calls;

        tap(MUTE_EDGE_X, 252);
        check((s_audio.mute_calls == 2) && !s_audio.mute_wanted, "a tap on the mute button's edge asks to unmute");
        check(s_audio.calls == volume_calls, "a tap on the mute button's edge leaves the volume");
    }

    /* Moving the slider while muted asks to unmute, once until the host
     * reports the mute state. */
    drag(SLIDER_X(36), SLIDER_X(62), 252);
    check((s_audio.mute_calls == 3) && !s_audio.mute_wanted, "moving the slider while muted asks to unmute once");
    Ui_SetMuted(0U);
    run_ms(50U);
    drag(SLIDER_X(62), SLIDER_X(52), 252);
    check(s_audio.mute_calls == 3, "moving the slider while not muted leaves mute");

    /* The host confirms a dragged volume without reporting it again (the
     * link shows nothing when the value is already shown): a mute change
     * keeps the slider where it was dragged, not at the volume from before
     * the drag. */
    Ui_SetVolume(30U);
    run_ms(50U);
    drag(SLIDER_X(36), SLIDER_X(94), 252);
    {
        const uint8_t dragged = Ui_ShownVolume();

        check(dragged > 30U, "the slider shows the dragged volume");
        Ui_SetMuted(1U);
        run_ms(50U);
        check(Ui_ShownVolume() == dragged, "muting keeps the dragged volume");
        Ui_SetMuted(0U);
        run_ms(50U);
        check(Ui_ShownVolume() == dragged, "unmuting keeps the dragged volume");
    }

    /* No default device: volume and mute unknown, controls disabled. */
    Ui_SetActive(UI_NO_DEVICE);
    Ui_SetVolume(UI_VOLUME_UNKNOWN);
    Ui_SetMuted(UI_VOLUME_UNKNOWN);
    run_ms(50U);
    {
        const int mute_calls = s_audio.mute_calls;

        tap(MUTE_X, 252);
        check(s_audio.mute_calls == mute_calls, "disabled mute button does nothing");
    }
    snapshot("10-no-volume");

    Ui_SetDevices(NULL, 0U);
    run_ms(50U);
    snapshot("11-empty");

    /* Flip button, top right corner of the waiting screen (the main screen
     * has none since ADR-005; the settings page has a switch): a tap does
     * nothing, a long press asks once, when the finger lifts. */
    Ui_ShowWaiting();
    run_ms(50U);
    tap(458, 18);
    check(s_flip_calls == 0, "a short tap on the flip button does nothing");
    s_touch.x = 458;
    s_touch.y = 18;
    s_touch.pressed = true;
    run_ms(UI_FLIP_HOLD_MS + 100U);
    check(s_flip_calls == 0, "the flip waits for the finger to lift");
    snapshot("12-flip-armed");
    s_touch.pressed = false;
    run_ms(100U);
    check(s_flip_calls == 1, "a long press on the flip button asks to flip once");
    snapshot_as("13-flipped", true);
    hold(458, 18, UI_FLIP_HOLD_MS + 100U);
    check(s_flip_calls == 2, "the flip button works again");
    Ui_SetDevices(devices, sizeof(devices) / sizeof(devices[0]));
    Ui_SetActive(1U);
    Ui_SetVolume(45U);
    Ui_SetMuted(0U);
    run_ms(50U);
    check(Ui_CurrentAppTab() == UI_APP_TAB_AUDIO, "the shown tab is kept while the host was away");

    run_settings_script(devices, sizeof(devices) / sizeof(devices[0]));
    run_settings_page_script(devices, sizeof(devices) / sizeof(devices[0]));
    run_screen_timeout_script(devices, sizeof(devices) / sizeof(devices[0]));
    run_edit_script(devices, sizeof(devices) / sizeof(devices[0]));
    run_layouts_script();
    run_dashboard_script();
    run_music_script();
    run_banner_script();
    run_redesign_script();
}

int main(int argc, char **argv)
{
    lv_display_t *disp;
    lv_indev_t *indev;

    if (argc != 2)
    {
        printf("usage: %s <output directory>\n", argv[0]);
        return 2;
    }
    s_out_dir = argv[1];

    lv_init();
    lv_tick_set_cb(tick_cb);

    disp = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_framebuffer, NULL, sizeof(s_framebuffer),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    Ui_Init(on_select, NULL);
    Ui_SetAudioHandlers(on_volume, on_mute, NULL);
    Ui_SetFlipHandler(on_flip, NULL);
    Ui_SetSettingsHandler(on_settings, NULL);
    Ui_SetBacklightHandler(on_backlight, NULL);
    run_ms(50U);
    run_script();

    printf("%d failures\n", s_failures);
    return (s_failures == 0) ? 0 : 1;
}
