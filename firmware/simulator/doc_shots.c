/**
 * @file    doc_shots.c
 * @brief   Screenshots of every screen for the README and the wiki.
 *
 * Like main.c (the test script) this runs the firmware's own UI code
 * (firmware/ui) headless on a 480 x 272 RGB565 framebuffer, the LCD's
 * format, and writes one raw dump per screen. Unlike main.c it checks
 * nothing: it fills the screens with one consistent set of demo data (a
 * Wednesday morning in Istanbul, a few audio outputs, a track, a menu,
 * notifications, a Claude Code request) so the pictures in the
 * documentation look like a day of use. The texts are those the host
 * sends (protocol.md "Dashboard records"), written here by hand.
 *
 * tools/doc-screens/make.sh builds it, draws the demo picture and cover
 * (tools/doc-screens/assets.py) and converts the dumps to PNG.
 *
 * Usage: ui_doc_shots <output directory> <assets directory>
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
static uint32_t s_now_ms;
static const char *s_out_dir;
static int s_failures;

static struct
{
    bool    pressed;
    int32_t x;
    int32_t y;
} s_touch;

static uint8_t s_cover[UI_ART_BYTES];
static uint8_t s_cover_shown[UI_ART_BYTES];
static uint8_t s_picture[UI_PICTURE_BYTES];
static uint8_t s_picture_shown[UI_PICTURE_BYTES];

/* ---------------------------------------------------------------- platform */

static uint32_t tick_cb(void)
{
    return s_now_ms;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
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

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0U; t < ms; t += 5U)
    {
        s_now_ms += 5U;
        (void)lv_timer_handler();
    }
}

static void tap(int32_t x, int32_t y)
{
    s_touch.x = x;
    s_touch.y = y;
    s_touch.pressed = true;
    run_ms(100U);
    s_touch.pressed = false;
    run_ms(150U);
}

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
    run_ms(800U);
}

static void drag_h(int32_t x0, int32_t x1, int32_t y)
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
    run_ms(800U);
}

static void shot(const char *name)
{
    char path[512];
    FILE *f;

    run_ms(100U);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    (void)snprintf(path, sizeof(path), "%s/%s.bin", s_out_dir, name);
    f = fopen(path, "wb");
    if ((f == NULL) || (fwrite(s_framebuffer, sizeof(s_framebuffer), 1U, f) != 1U))
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

static bool load(const char *dir, const char *name, uint8_t *buffer, size_t bytes)
{
    char path[512];
    FILE *f;
    bool ok;

    (void)snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    ok = (f != NULL) && (fread(buffer, bytes, 1U, f) == 1U);
    if (f != NULL)
    {
        (void)fclose(f);
    }
    if (!ok)
    {
        printf("FAIL cannot read %s\n", path);
        s_failures++;
    }
    return ok;
}

/* Handlers: the screenshots need none of the answers, but the UI enables
 * its controls only when a handler is set. */
static void on_select(uint8_t id, void *user) { LV_UNUSED(id); LV_UNUSED(user); }
static void on_volume(uint8_t v, bool r, void *user) { LV_UNUSED(v); LV_UNUSED(r); LV_UNUSED(user); }
static void on_mute(bool m, void *user) { LV_UNUSED(m); LV_UNUSED(user); }
static void on_flip(void *user) { LV_UNUSED(user); }
static void on_settings(const UiSettings *s, void *user) { LV_UNUSED(s); LV_UNUSED(user); }
static void on_backlight(bool on, void *user) { LV_UNUSED(on); LV_UNUSED(user); }
static bool ed_fav(uint8_t id, bool f, void *user) { LV_UNUSED(id); LV_UNUSED(f); LV_UNUSED(user); return true; }
static bool ed_alias(uint8_t id, const char *a, void *user) { LV_UNUSED(id); LV_UNUSED(a); LV_UNUSED(user); return true; }
static bool ed_move(uint8_t id, uint8_t p, void *user) { LV_UNUSED(id); LV_UNUSED(p); LV_UNUSED(user); return true; }
static void d_open(uint8_t s, int8_t a, void *user) { LV_UNUSED(s); LV_UNUSED(a); LV_UNUSED(user); }
static void d_close(void *user) { LV_UNUSED(user); }
static void d_action(uint8_t s, uint8_t a, void *user) { LV_UNUSED(s); LV_UNUSED(a); LV_UNUSED(user); }
static void d_media(uint8_t a, void *user) { LV_UNUSED(a); LV_UNUSED(user); }
static void d_prompt(uint8_t id, uint8_t a, void *user) { LV_UNUSED(id); LV_UNUSED(a); LV_UNUSED(user); }
static void d_seek(uint32_t s, void *user) { LV_UNUSED(s); LV_UNUSED(user); }

/* ------------------------------------------------------------- demo data */

#define F "\x1F"

/* Rail and dashboard positions (as in main.c). */
#define RAIL_X       28
#define RAIL_DASH_Y  30
#define RAIL_MUSIC_Y 82
#define RAIL_AUDIO_Y 134
#define RAIL_COG_Y   208
#define CARD_WEATHER_X 164
#define CARD_MENU_X    372
#define CARD_TOP_Y     82
#define PIC_CARD_X     374
#define PIC_CARD_Y     180
#define BACK_X         76
#define BACK_Y         18
#define PENCIL_X       454
#define TAB_FAV_X      238

/* 2026-10-28 09:41, a Wednesday (local time as TIME sends it). */
#define DEMO_TIME 1793180460U

static UiDevice device(uint8_t id, uint8_t icon, const char *name, const char *alias, uint8_t favorite)
{
    UiDevice d;

    memset(&d, 0, sizeof(d));
    d.id = id;
    d.icon = icon;
    (void)snprintf(d.name, sizeof(d.name), "%s", name);
    (void)snprintf(d.alias, sizeof(d.alias), "%s", alias);
    d.favorite = favorite;
    return d;
}

static UiDevice s_devices[6];

static void set_devices(void)
{
    s_devices[0] = device(0U, UI_ICON_SPEAKERS, "Hoparlörler (Realtek(R) Audio)", "Masaüstü hoparlör", 1U);
    s_devices[1] = device(1U, UI_ICON_HEADSET, "Kulaklık (USB Gaming Headset)", "Oyun kulaklığı", 1U);
    s_devices[2] = device(2U, UI_ICON_DISPLAY, "Monitör (NVIDIA High Definition Audio)", "Monitör", 0U);
    s_devices[3] = device(3U, UI_ICON_HEADPHONES, "Kablosuz kulaklık (Bluetooth)", "", 1U);
    s_devices[4] = device(4U, UI_ICON_DIGITAL, "Dijital çıkış (S/PDIF)", "", 0U);
    s_devices[5] = device(5U, UI_ICON_GENERIC, "USB DAC", "", 0U);
    Ui_SetDevices(s_devices, 6U);
    Ui_SetActive(0U);
    Ui_SetVolume(62U);
    Ui_SetMuted(0U);
}

static const char *const weather_sunny =
    "1" F "İstanbul" F "" F "E59B2E" F "L\n"
    "19°\n"
    "12° / 21°";
static const char *const weather_rain =
    "7" F "İstanbul" F "" F "3A6EA5" F "L\n"
    "11°\n"
    "9° / 14°";

static const char *const weather_detail =
    "Hava durumu" F "İstanbul · 09:30" F "\n"
    "*1" F "19°" F "Açık" F "12° / 21°" F "E59B2E\n"
    "=Hissedilen" F "18°" F "Nem" F "%58" F "Rüzgâr" F "14 km/s\n"
    "#10 gün\n"
    ">Bugün" F "1" F "21°" F "12°" F "%5\n"
    ">Yarın" F "3" F "19°" F "13°" F "%20\n"
    ">Cum 30" F "7" F "15°" F "11°" F "%80\n"
    ">Cmt 31" F "8" F "13°" F "10°" F "%90\n"
    ">Paz 1" F "5" F "16°" F "10°" F "%40\n"
    ">Pzt 2" F "3" F "18°" F "11°" F "%15\n"
    ">Sal 3" F "1" F "20°" F "12°" F "%0\n"
    ">Çar 4" F "10" F "14°" F "9°" F "%70\n"
    ">Per 5" F "6" F "13°" F "8°" F "%30\n"
    ">Cum 6" F "3" F "16°" F "9°" F "%10";

/* A menu card as host/Menu/MenuService makes it: no title, four lines. */
static const char *const menu_green =
    "11" F "" F "" F "3E9B5F" F "\n"
    "Izgara köfte\n"
    "Bulgur pilavı\n"
    "Mercimek çorba\n"
    "Sütlaç";
static const char *const menu_yellow =
    "11" F "" F "" F "D9A520" F "\n"
    "Kuru fasulye\n"
    "Pirinç pilavı\n"
    "Tarhana çorba\n"
    "Kazandibi";
static const char *const menu_orange =
    "11" F "" F "" F "E0702F" F "\n"
    "Etli bamya\n"
    "Şehriyeli pilav\n"
    "Yayla çorba\n"
    "Revani";
static const char *const menu_liked =
    "11" F "" F "" F "3E9B5F" F "" F "D4AF37" F "F2C94C\n"
    "Kağıt kebabı\n"
    "Kalem börek\n"
    "Ezogelin çorba\n"
    "Fıstıklı baklava";

static const char *const menu_detail =
    "Yemek" F "28 Ekim Çarşamba" F "n\n"
    "0" F "Ana yemek" F "Izgara köfte\n"
    "0" F "Yan yemek" F "Bulgur pilavı\n"
    "0" F "Çorba" F "Mercimek çorba\n"
    "0" F "Tatlı" F "Sütlaç\n"
    "0" F "Yanında" F "Ayran\n"
    "#Salata bar\n"
    F F "Çoban salata · Havuç tarator · Rus salatası · Kısır · Zeytinyağlı fasulye · Turşu";
static const char *const menu_detail_marks =
    "Yemek" F "29 Ekim Perşembe" F "n\n"
    "14" F "Ana yemek" F "Kağıt kebabı" F "D4AF37\n"
    "0" F "Yan yemek" F "Kalem börek\n"
    "16" F "Çorba" F "Tarhana çorba" F "D9A520\n"
    "15" F "Tatlı" F "Kabak tatlısı" F "E0702F\n"
    "0" F "Yanında" F "Cacık";
static const char *const menu_warning =
    "Bugünkü menüde" F "28 Ekim Çarşamba" F "" F "E0702F" F "" F "" F "11\n"
    "15" F "İstemiyorum" F "Etli bamya\n"
    "16" F "Sevmiyorum" F "Yayla çorba\n"
    "!1" F "Tamam";
static const char *const menu_warning_liked =
    "Bugünkü menüde" F "29 Ekim Perşembe" F "" F "3E9B5F" F "D4AF37" F "" F "11\n"
    "14" F "Seviyorum" F "Kağıt kebabı" F "F2C94C\n"
    "16" F "Sevmiyorum" F "Tarhana çorba\n"
    "!1" F "Tamam";
static const char *const menu_warning_other =
    "Bugünkü menüde" F "30 Ekim Cuma" F "" F "3E9B5F" F "" F "s" F "11\n"
    "15" F "İstemiyorum" F "Kabak tatlısı" F "E0702F\n"
    "!1" F "Tamam";

static const char *const notify_detail =
    "Bildirimler" F "4 bildirim" F "c\n"
    "#WhatsApp · 09:40\n"
    F "Elif" F "Öğlen yemeğe çıkıyor muyuz? 12:30'da aşağıda buluşalım.\n"
    "#Outlook · 09:22\n"
    F "Haftalık toplantı" F "Perşembe 14:00, 3. kat toplantı odası.\n"
    "#Microsoft Teams · 09:05\n"
    F "Can Demir" F "PR'a bakabilir misin? Yorumları düzelttim.\n"
    "#Ayarlar · 08:51\n"
    F "Windows Update" F "Güncellemeler yüklendi, yeniden başlatma bekliyor.";
static const char *const banner =
    "12" F "WhatsApp" F "5" F "2\n"
    "Elif\n"
    "Öğlen yemeğe çıkıyor muyuz? 12:30'da aşağıda buluşalım.";

static const char *const prompt_bash =
    "Claude Code izin istiyor" F "desk-dashboard · 1/1" F "60\n"
    F "Araç" F "Bash\n"
    F "Komut" F "git push origin docs/88-readme-wiki\n"
    F "Açıklama" F "Dalı GitHub'a gönder\n"
    "!1" F "Onayla\n"
    "!2" F "Reddet\n"
    "!3" F "Terminalde sor";
static const char *const prompt_edit =
    "Claude Code izin istiyor" F "desk-dashboard · 2/2" F "41\n"
    F "Araç" F "Edit\n"
    F "Dosya" F "firmware/ui/ui_dash.c\n"
    F "Eski" F "#define BANNER_MS 5000U\n"
    F "Yeni" F "#define BANNER_MS 6000U\n"
    "!1" F "Onayla\n"
    "!2" F "Reddet\n"
    "!3" F "Terminalde sor";
static const char *const claude_detail =
    "Claude Code" F "Onaylar" F "c\n"
    "#Bekleyen\n"
    F "Bash" F "Testleri çalıştır\n"
    "#Son kararlar\n"
    F "09:38 Bash" F "onaylandı · Dalı GitHub'a gönder\n"
    F "09:31 Edit" F "onaylandı · firmware/ui/ui_dash.c\n"
    F "09:12 Write" F "PC'de cevaplandı · README.md\n"
    F "08:57 Bash" F "reddedildi · Derleme klasörünü sil";

static const UiIndicator ind_normal[2] = { { 12U, UI_INDICATOR_RED, 4U, 2U }, { 13U, UI_INDICATOR_NORMAL, 0U, 3U } };
static const UiIndicator ind_waiting[2] = { { 12U, UI_INDICATOR_NORMAL, 4U, 2U }, { 13U, UI_INDICATOR_RED, 1U, 3U } };
static const UiIndicator ind_allowed[2] = { { 12U, UI_INDICATOR_NORMAL, 0U, 2U }, { 13U, UI_INDICATOR_GREEN, 0U, 3U } };

static UiSettings s_set;

static void apply(void)
{
    Ui_ApplySettings(&s_set);
    run_ms(100U);
}

static void go(int32_t rail_y)
{
    tap(RAIL_X, rail_y);
    run_ms(50U);
}

static void playing(void)
{
    Ui_SetMedia("Mavi Saat" F "Deniz Işıkları" F "Kıyıda Bir Akşam" F "Spotify");
    Ui_SetMediaState(UI_MEDIA_PLAYING,
                     UI_MEDIA_CAN_PREVIOUS | UI_MEDIA_CAN_PLAY_PAUSE | UI_MEDIA_CAN_NEXT | UI_MEDIA_CAN_SEEK, 83U, 236U);
    Ui_SetMediaArt(s_cover);
}

static void dashboard_default(void)
{
    Ui_SetCard(0U, weather_sunny);
    Ui_SetCard(1U, menu_green);
    Ui_SetIndicators(ind_normal, 2U);
    Ui_SetPicture(s_picture);
    playing();
}

/* ---------------------------------------------------------------- scenes */

static void scene_first_run(void)
{
    shot("waiting");
    set_devices();
    run_ms(50U);
    /* Before the host sent anything for the cards: the empty state. */
    Ui_SetTime(DEMO_TIME);
    Ui_SetIndicators(NULL, 0U);
    go(RAIL_DASH_Y);
    shot("dash-empty");
}

static void scene_dashboard(void)
{
    dashboard_default();
    go(RAIL_DASH_Y);
    shot("dash-main");

    tap(CARD_WEATHER_X, CARD_TOP_Y);
    Ui_ShowDetail(0U, weather_detail);
    run_ms(100U);
    shot("dash-weather");
    drag_v(300, 230, 60);
    shot("dash-weather-days");
    drag_h(420, 120, 235);
    shot("dash-weather-days-scrolled");
    tap(BACK_X, BACK_Y);

    tap(CARD_MENU_X, CARD_TOP_Y);
    Ui_ShowDetail(1U, menu_detail);
    run_ms(100U);
    shot("dash-menu");
    Ui_ShowDetail(1U, menu_detail_marks);
    run_ms(100U);
    shot("dash-menu-marks");
    tap(BACK_X, BACK_Y);

    Ui_SetCard(1U, menu_liked);
    run_ms(50U);
    shot("dash-menu-liked");
    Ui_SetCard(1U, menu_yellow);
    Ui_SetCard(0U, weather_rain);
    Ui_SetIndicators(ind_waiting, 2U);
    run_ms(50U);
    shot("dash-status");
    Ui_SetCard(1U, menu_orange);
    Ui_SetIndicators(ind_allowed, 2U);
    run_ms(50U);
    shot("dash-menu-orange");

    Ui_ShowPrompt(255U, menu_warning);
    run_ms(100U);
    shot("dash-menu-warning");
    Ui_ClosePrompt(255U);
    Ui_ShowPrompt(255U, menu_warning_liked);
    run_ms(100U);
    shot("dash-menu-warning-liked");
    Ui_ClosePrompt(255U);
    Ui_ShowPrompt(255U, menu_warning_other);
    run_ms(100U);
    shot("dash-menu-warning-other");
    Ui_ClosePrompt(255U);
    run_ms(50U);

    dashboard_default();
    run_ms(50U);
    tap(408, 18); /* the bell */
    Ui_ShowDetail(2U, notify_detail);
    run_ms(100U);
    shot("dash-notifications");
    tap(BACK_X, BACK_Y);

    Ui_ShowBanner(banner);
    run_ms(100U);
    shot("dash-banner");
    run_ms(5500U);

    Ui_SetIndicators(ind_waiting, 2U);
    Ui_ShowPrompt(7U, prompt_bash);
    run_ms(100U);
    shot("dash-claude-prompt");
    Ui_ClosePrompt(7U);
    Ui_ShowPrompt(8U, prompt_edit);
    run_ms(100U);
    shot("dash-claude-prompt-edit");
    Ui_ClosePrompt(8U);
    run_ms(50U);

    tap(456, 18); /* the Claude Code icon */
    Ui_ShowDetail(3U, claude_detail);
    run_ms(100U);
    shot("dash-claude-list");
    tap(BACK_X, BACK_Y);
    Ui_SetIndicators(ind_normal, 2U);

    tap(PIC_CARD_X, PIC_CARD_Y);
    run_ms(100U);
    shot("dash-picture");
    tap(240, 136);

    /* Over the Audio tab: the banner and the prompt cover every screen. */
    go(RAIL_AUDIO_Y);
    Ui_ShowBanner(banner);
    run_ms(100U);
    shot("audio-banner");
    run_ms(5500U);
    go(RAIL_DASH_Y);
}

static void scene_music(void)
{
    playing();
    go(RAIL_MUSIC_Y);
    shot("music-playing");
    Ui_SetMediaState(UI_MEDIA_PAUSED, UI_MEDIA_CAN_PREVIOUS | UI_MEDIA_CAN_PLAY_PAUSE | UI_MEDIA_CAN_NEXT, 141U, 236U);
    run_ms(50U);
    shot("music-paused");
    Ui_SetMedia(NULL);
    Ui_SetMediaState(UI_MEDIA_NONE, 0U, 0U, 0U);
    Ui_SetMediaArt(NULL);
    run_ms(50U);
    shot("music-none");
    playing();
    go(RAIL_DASH_Y);
}

static void scene_audio(void)
{
    static const UiEditHandlers edit = { ed_fav, ed_alias, ed_move };

    Ui_SetEditHandlers(&edit, NULL);
    go(RAIL_AUDIO_Y);
    shot("audio-cards");
    Ui_SetPending(1U);
    run_ms(50U);
    shot("audio-pending");
    Ui_SetActive(0U);
    Ui_SetMuted(1U);
    run_ms(50U);
    shot("audio-muted");
    Ui_SetMuted(0U);
    tap(TAB_FAV_X, 18);
    shot("audio-favorites");
    tap(122, 18);

    s_set.grid = UI_GRID_3X2;
    apply();
    shot("audio-grid-3x2");
    s_set.grid = UI_GRID_3X3;
    apply();
    shot("audio-grid-3x3");
    s_set.grid = UI_GRID_LIST;
    apply();
    shot("audio-grid-list");
    s_set.grid = UI_GRID_2X2;
    s_set.layout = UI_LAYOUT_LIST;
    apply();
    shot("audio-layout-list");
    s_set.layout = UI_LAYOUT_TILES;
    apply();
    shot("audio-layout-tiles");
    s_set.layout = UI_LAYOUT_CARDS;
    apply();

    tap(PENCIL_X, 18);
    run_ms(50U);
    shot("audio-edit");
    tap(174, 106); /* the first card's pencil: the alias keyboard */
    run_ms(50U);
    shot("audio-keyboard");
    tap(28, 26); /* × closes it */
    tap(PENCIL_X, 18);
    run_ms(50U);
}

static void scene_settings(void)
{
    go(RAIL_COG_Y);
    shot("settings-top");
    drag_v(240, 250, 120);
    shot("settings-middle");
    drag_v(240, 250, 40);
    drag_v(240, 250, 40);
    shot("settings-bottom");
    tap(60, 18);
    run_ms(50U);
}

static void scene_themes(void)
{
    static const char *const accents[UI_ACCENT_COUNT] = {
        "accent-orange", "accent-blue", "accent-green", "accent-purple", "accent-pink", "accent-yellow"
    };

    s_set.theme = UI_THEME_LIGHT;
    apply();
    go(RAIL_DASH_Y);
    shot("dash-light");
    go(RAIL_MUSIC_Y);
    shot("music-light");
    go(RAIL_AUDIO_Y);
    shot("audio-light");
    go(RAIL_COG_Y);
    shot("settings-light");
    tap(60, 18);

    s_set.theme = UI_THEME_DARK;
    go(RAIL_AUDIO_Y);
    for (uint8_t i = 0U; i < UI_ACCENT_COUNT; i++)
    {
        s_set.accent = i;
        apply();
        shot(accents[i]);
    }
    s_set.accent = UI_ACCENT_ORANGE;
    apply();

    s_set.language = UI_LANGUAGE_ENGLISH;
    apply();
    go(RAIL_DASH_Y);
    shot("en-dash");
    go(RAIL_MUSIC_Y);
    shot("en-music");
    go(RAIL_AUDIO_Y);
    shot("en-audio");
    go(RAIL_COG_Y);
    shot("en-settings");
    tap(60, 18);
    s_set.language = UI_LANGUAGE_TURKISH;
    apply();
    go(RAIL_DASH_Y);
}

int main(int argc, char **argv)
{
    static const UiDashHandlers dash = { d_open, d_close, d_action, d_media, d_prompt, d_seek };
    lv_display_t *disp;
    lv_indev_t *indev;

    if (argc != 3)
    {
        printf("usage: %s <output directory> <assets directory>\n", argv[0]);
        return 2;
    }
    s_out_dir = argv[1];
    (void)load(argv[2], "cover.rgb565", s_cover, sizeof(s_cover));
    (void)load(argv[2], "picture.rgb565", s_picture, sizeof(s_picture));

    lv_init();
    lv_tick_set_cb(tick_cb);
    disp = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_framebuffer, NULL, sizeof(s_framebuffer), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    Ui_Init(on_select, NULL);
    Ui_SetAudioHandlers(on_volume, on_mute, NULL);
    Ui_SetFlipHandler(on_flip, NULL);
    Ui_SetSettingsHandler(on_settings, NULL);
    Ui_SetBacklightHandler(on_backlight, NULL);
    Ui_SetDashHandlers(&dash, NULL);
    Ui_SetMediaArtBuffer(s_cover_shown);
    Ui_SetPictureBuffer(s_picture_shown);
    run_ms(50U);

    s_set = *Ui_Settings();
    s_set.layout = UI_LAYOUT_CARDS;
    s_set.theme = UI_THEME_DARK;
    s_set.accent = UI_ACCENT_ORANGE;
    s_set.grid = UI_GRID_2X2;
    s_set.language = UI_LANGUAGE_TURKISH;
    s_set.screen_timeout = 0U;
    s_set.volume_limit = 40U;

    scene_first_run();
    apply();
    scene_dashboard();
    scene_music();
    scene_audio();
    scene_settings();
    scene_themes();

    printf("%d failures\n", s_failures);
    return (s_failures == 0) ? 0 : 1;
}

#undef F
