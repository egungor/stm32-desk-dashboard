/**
 * @file    ui.h
 * @brief   Touch UI of the desk dashboard (LVGL, hardware independent).
 *
 * The module only depends on LVGL. It knows nothing about the LCD, the
 * touch controller, USB or Windows: the caller creates the LVGL display
 * and input device, feeds in the state reported by the host, and receives
 * the user's choice through a callback. The same code runs in the firmware
 * and in the Linux simulator (firmware/simulator).
 *
 * Screens:
 *   - waiting: no host connected;
 *   - main (the application screen, ADR-005): a rail on the left (56 px)
 *     with the Dashboard, Music and Audio tabs, the settings button and
 *     the connection mark; the shown tab fills the remaining 424 x 272;
 *   - Dashboard tab (ui_dash.c, ADR-006): a clock strip, four cards on a
 *     2x2 grid and a bottom strip: media controls on the left (previous,
 *     play or pause by the playback state, next), the volume of the
 *     Windows default output on the right. The cards' text comes from the
 *     host (Ui_SetCard); a tap opens the card's detail in the tab (header
 *     with back and, if the host asks, previous / next; rows; up to three
 *     buttons), filled by Ui_ShowDetail;
 *   - Music tab (ui_music.c, ADR-008): what plays on the PC (cover, title,
 *     artist, album, app, progress) and the media controls (Ui_SetMedia,
 *     Ui_SetMediaState, Ui_SetMediaArt);
 *   - Audio tab: two sub-tabs, all devices and favorites, in one of three
 *     layouts (UiSettings.layout): A cards in a 2x2, 3x2 or 3x3 grid or as
 *     a list (UiSettings.grid); B a list with vertical volume; C the active
 *     device in a large panel and the others as tiles. The active device
 *     is marked by colour and text; page buttons when there are more
 *     devices than fit on one page;
 *   - settings: opened with the cog button on the rail; general (theme,
 *     accent colour, language, screen timeout, flip, startup screen) and
 *     audio (layout, grid, startup tab, volume limit) settings. A change is
 *     applied at once and reported with the whole record
 *     (UiSettingsCallback);
 *   - edit mode: opened with the pencil button in the Audio tab's top
 *     bar; every device as a card with buttons to move it one place left
 *     or right, mark it as favorite, and give it an alias on an on-screen
 *     keyboard (Turkish layout). Edits go out through UiEditHandlers;
 *   - error notice: shown over any screen for UI_NOTICE_MS;
 *   - screen timeout: after settings.screen_timeout minutes without a touch
 *     the screen goes black and the backlight handler is told to switch
 *     the backlight off; the next touch only wakes it;
 *   - flip button (top right corner of the waiting screen; on the
 *     settings page as a switch): held for UI_FLIP_HOLD_MS, it asks to
 *     turn the image by 180° when released.
 *
 * Appearance (theme, accent colour, language, grid) follows UiSettings
 * (Ui_ApplySettings); a change of these rebuilds the screens.
 *
 * Call everything from the task that runs lv_timer_handler(). No dynamic
 * allocation besides the LVGL objects created once in Ui_Init().
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MAX_DEVICES       32U  /**< = PROTO_MAX_DEVICES */
#define UI_MAX_NAME_BYTES    48U  /**< = PROTO_MAX_NAME_BYTES, UTF-8 */
#define UI_MAX_ALIAS_BYTES   32U  /**< = PROTO_MAX_ALIAS_BYTES, UTF-8 */
#define UI_MAX_PER_PAGE      9U   /**< 3x3 grid */
#define UI_NO_DEVICE         0xFFU
#define UI_NOTICE_MS         3000U
#define UI_FLIP_HOLD_MS      1000U

/** Device icon; values match ProtoIcon in protocol/include/protocol.h. */
typedef enum
{
    UI_ICON_GENERIC    = 0,
    UI_ICON_SPEAKERS   = 1,
    UI_ICON_HEADPHONES = 2,
    UI_ICON_HEADSET    = 3,
    UI_ICON_DISPLAY    = 4,
    UI_ICON_DIGITAL    = 5
} UiIcon;

typedef struct
{
    uint8_t id;                            /**< host's device_index */
    uint8_t icon;                          /**< UiIcon */
    char    name[UI_MAX_NAME_BYTES + 1U];  /**< Windows name, UTF-8, NUL-terminated */
    char    alias[UI_MAX_ALIAS_BYTES + 1U]; /**< user's alias, "" if none; shown instead of name */
    uint8_t favorite;                      /**< 1 if marked as favorite */
} UiDevice;

/** Called when the user taps a device that is not already active. */
typedef void (*UiSelectCallback)(uint8_t device_id, void *user);

#define UI_VOLUME_UNKNOWN    0xFFU  /**< volume or mute not reported */

/**
 * Called while the user drags the volume slider (0-100), and once more with
 * @p released true when the finger lifts. Rate limiting is the caller's job.
 */
typedef void (*UiVolumeCallback)(uint8_t volume, bool released, void *user);

/** Called when the user taps the mute button: the state wanted. */
typedef void (*UiMuteCallback)(bool muted, void *user);

/**
 * @brief  Build all screens on the default LVGL display and show the
 *         waiting screen. Call once, after the display is created.
 */
void Ui_Init(UiSelectCallback on_select, void *user);

/** Show the waiting screen (host not connected). Keeps the device list. */
void Ui_ShowWaiting(void);

/**
 * @brief  Replace the device list and show the main screen.
 *
 * Copies at most UI_MAX_DEVICES entries. Keeps the current page if it
 * still exists. Clears a pending selection.
 */
void Ui_SetDevices(const UiDevice *devices, size_t count);

/** Mark the device reported as default by the host (UI_NO_DEVICE: none). */
void Ui_SetActive(uint8_t device_id);

/**
 * @brief  Mark a selection that was sent to the host but not confirmed
 *         yet ("Geçiliyor…"). Cleared by Ui_SetActive or Ui_SetDevices.
 */
void Ui_SetPending(uint8_t device_id);

/** Show a short notice with a warning icon for UI_NOTICE_MS. */
void Ui_ShowError(const char *text);

/** Notices with fixed UI text. */
typedef enum
{
    UI_NOTICE_SELECT_FAILED,  /**< the computer could not switch the device */
    UI_NOTICE_HOST_TIMEOUT,   /**< the computer did not answer */
    UI_NOTICE_EDIT_FAILED,    /**< the computer did not store a change */
    UI_NOTICE_DETAIL_FAILED,  /**< no detail for a card (error or timeout) */
    UI_NOTICE_ACTION_FAILED,  /**< the computer could not do a detail / media action */
} UiNotice;

/** Show one of the fixed notices (Ui_ShowError with the UI's own text). */
void Ui_ShowNotice(UiNotice notice);

/** Set the volume and mute handlers (optional; without them the controls
 *  do nothing). */
void Ui_SetAudioHandlers(UiVolumeCallback on_volume, UiMuteCallback on_mute, void *user);

/**
 * @brief  Volume reported by the host, 0-100, or UI_VOLUME_UNKNOWN (slider
 *         disabled). Ignored while the user is dragging the slider.
 */
void Ui_SetVolume(uint8_t volume);

/** Mute reported by the host: 0, 1, or UI_VOLUME_UNKNOWN (button disabled). */
void Ui_SetMuted(uint8_t muted);

/**
 * Called when the user asks to turn the image by 180° (long press on the
 * flip button, on release). The UI does not rotate anything itself: the
 * display port does, and the UI's layout stays the same.
 */
typedef void (*UiFlipCallback)(void *user);

/** Set the flip handler (optional; without it the button does nothing). */
void Ui_SetFlipHandler(UiFlipCallback on_flip, void *user);

/* ------------------------------------------------------------- settings */

/** UiSettings values; the same fields and values as the protocol's
 *  settings record (protocol/protocol.md, "Preferences"). */
enum
{
    UI_LAYOUT_CARDS = 0, UI_LAYOUT_LIST = 1, UI_LAYOUT_TILES = 2, UI_LAYOUT_COUNT = 3,
    UI_THEME_DARK = 0, UI_THEME_LIGHT = 1, UI_THEME_COUNT = 2,
    UI_ACCENT_ORANGE = 0, UI_ACCENT_BLUE = 1, UI_ACCENT_GREEN = 2, UI_ACCENT_PURPLE = 3,
    UI_ACCENT_PINK = 4, UI_ACCENT_YELLOW = 5, UI_ACCENT_COUNT = 6,
    UI_GRID_2X2 = 0, UI_GRID_3X2 = 1, UI_GRID_3X3 = 2, UI_GRID_LIST = 3, UI_GRID_COUNT = 4,
    UI_STARTUP_LAST = 0, UI_STARTUP_ALL = 1, UI_STARTUP_FAVORITES = 2,
    UI_TAB_ALL = 0, UI_TAB_FAVORITES = 1,
    UI_LANGUAGE_TURKISH = 0, UI_LANGUAGE_ENGLISH = 1, UI_LANGUAGE_COUNT = 2,
    UI_STARTUP_SCREEN_DASHBOARD = 0, UI_STARTUP_SCREEN_AUDIO = 1, UI_STARTUP_SCREEN_MUSIC = 2,
    UI_STARTUP_SCREEN_COUNT = 3,
};

typedef struct
{
    uint8_t layout;          /**< UI_LAYOUT_* */
    uint8_t theme;           /**< UI_THEME_* */
    uint8_t accent;          /**< UI_ACCENT_* */
    uint8_t grid;            /**< UI_GRID_* (cards layout) */
    uint8_t startup_tab;     /**< UI_STARTUP_* */
    uint8_t last_tab;        /**< UI_TAB_*, kept up to date by the UI */
    uint8_t screen_timeout;  /**< minutes without a touch, 0 = never (Ui_SetBacklightHandler) */
    uint8_t flipped;         /**< 0/1 (display port) */
    uint8_t language;        /**< UI_LANGUAGE_* */
    uint8_t volume_limit_on; /**< 0/1 (host) */
    uint8_t volume_limit;    /**< percent (host) */
    uint8_t startup_screen;  /**< UI_STARTUP_SCREEN_* (shell, ADR-005; kept and passed back for now) */
} UiSettings;

/**
 * @brief  Settings to use. Theme, accent, language, layout and grid are
 *         applied by the UI (screens rebuilt if one of them changed); the
 *         other fields are kept and passed back with the next change.
 *
 * The first call after Ui_Init also chooses the tab shown at startup
 * (startup_tab; "last used" means last_tab). Out-of-range fields are
 * ignored (the call does nothing).
 */
void Ui_ApplySettings(const UiSettings *settings);

/** The settings currently in use. */
const UiSettings *Ui_Settings(void);

/** Called with the whole record when the user changed something in the UI
 *  (the tab, stored as last_tab, or a setting on the settings page). The UI
 *  has already applied what it handles itself (including screen_timeout);
 *  flipped and the volume limit are for the caller (display port, host). */
typedef void (*UiSettingsCallback)(const UiSettings *settings, void *user);

/** Set the settings handler (optional). */
void Ui_SetSettingsHandler(UiSettingsCallback on_settings, void *user);

/* -------------------------------------------------------------- edit mode */

/**
 * Edits asked for in the edit mode (pencil button in the top bar): favorite
 * flag, alias (UTF-8, at most UI_MAX_ALIAS_BYTES, "" removes it) and the
 * position in the user's order (0 = first). Each returns false if the edit
 * was not sent (e.g. another one is still in flight): the tap then does
 * nothing and can be repeated; a refusal by the host comes as
 * Ui_ShowNotice(UI_NOTICE_EDIT_FAILED). The UI shows an edit only when the
 * new list arrives (Ui_SetDevices), as the protocol says.
 */
typedef struct
{
    bool (*set_favorite)(uint8_t device_id, bool favorite, void *user);
    bool (*set_alias)(uint8_t device_id, const char *alias, void *user);
    bool (*move)(uint8_t device_id, uint8_t new_position, void *user);
} UiEditHandlers;

/** Set the edit handlers (optional; without them the pencil button is
 *  hidden). @p handlers is copied. */
void Ui_SetEditHandlers(const UiEditHandlers *handlers, void *user);

/* --------------------------------------------------------- screen timeout */

#define UI_SCREEN_OFF_CHECK_MS 1000U  /**< how often the timeout is checked */

/**
 * Called when the screen goes dark (@p on false: settings.screen_timeout
 * minutes without a touch) and when a touch wakes it (@p on true). The
 * port switches the backlight. While dark, the first touch only wakes the
 * screen: it does not reach the controls under it.
 */
typedef void (*UiBacklightCallback)(bool on, void *user);

/** Set the backlight handler (optional; without it the timeout still runs). */
void Ui_SetBacklightHandler(UiBacklightCallback on_backlight, void *user);

/** True while the screen is lit (for tests). */
bool Ui_ScreenOn(void);

/** Currently shown page, 0-based (for tests). */
uint32_t Ui_CurrentPage(void);

/** Volume the slider shows, 0-100, or UI_VOLUME_UNKNOWN (for tests). */
uint8_t Ui_ShownVolume(void);

/** Currently shown tab, UI_TAB_* (for tests). */
uint32_t Ui_CurrentTab(void);

/** Screens, for Ui_CurrentScreen. */
enum
{
    UI_SCREEN_WAITING = 0,
    UI_SCREEN_MAIN = 1,
    UI_SCREEN_SETTINGS = 2,
    UI_SCREEN_EDIT = 3,
    UI_SCREEN_KEYBOARD = 4,
};

/** Currently shown screen, UI_SCREEN_* (for tests). */
uint32_t Ui_CurrentScreen(void);

/** Tabs of the main screen, for Ui_CurrentAppTab; the same values as
 *  UI_STARTUP_SCREEN_*. The rail shows them as Dashboard, Music, Audio. */
enum
{
    UI_APP_TAB_DASHBOARD = 0,
    UI_APP_TAB_AUDIO = 1,
    UI_APP_TAB_MUSIC = 2,
    UI_APP_TAB_COUNT = 3,
};

/** Tab shown on the main screen, UI_APP_TAB_* (for tests). */
uint32_t Ui_CurrentAppTab(void);

/* ------------------------------------------------------------- dashboard */

#define UI_DASH_CARDS        4U    /**< shown slots (2x2); the protocol has 8 */
#define UI_DASH_CARD_BYTES   256U  /**< = PROTO_CARD_MAX_BYTES */
#define UI_DASH_DETAIL_BYTES 2048U /**< = PROTO_DETAIL_MAX_BYTES */
#define UI_DASH_NO_SLOT      0xFFU

/** Media strip actions; values match PROTO_MEDIA_*. */
enum
{
    UI_MEDIA_PREVIOUS = 1,
    UI_MEDIA_PLAY_PAUSE = 2,
    UI_MEDIA_NEXT = 3,
};

/** What the Dashboard tab shows, for Ui_DashView (tests). */
enum
{
    UI_DASH_VIEW_CARDS = 0,
    UI_DASH_VIEW_LOADING = 1, /**< detail asked for, not here yet */
    UI_DASH_VIEW_DETAIL = 2,
};

/**
 * Requests from the Dashboard tab. open_detail: a card was tapped (arg 0)
 * or previous / next in its detail (arg -1 / +1); close_detail: the back
 * button; action: a detail button (its action number); media: the media
 * strip (UI_MEDIA_*); prompt_action: a button of the prompt. All optional.
 */
typedef struct
{
    void (*open_detail)(uint8_t slot, int8_t arg, void *user);
    void (*close_detail)(void *user);
    void (*action)(uint8_t slot, uint8_t action, void *user);
    void (*media)(uint8_t action, void *user);
    /** A button of the shown prompt @p id (Ui_ShowPrompt). */
    void (*prompt_action)(uint8_t id, uint8_t action, void *user);
} UiDashHandlers;

/** Set the dashboard handlers (copied). */
void Ui_SetDashHandlers(const UiDashHandlers *handlers, void *user);

/**
 * @brief  Record text of card slot @p slot (protocol.md, "Record text"),
 *         "" for an empty slot. Slots >= UI_DASH_CARDS are ignored; text
 *         beyond UI_DASH_CARD_BYTES is cut.
 */
void Ui_SetCard(uint8_t slot, const char *text);

/** The detail record of @p slot arrived; shown if that card's detail is
 *  open (or loading), otherwise ignored. */
void Ui_ShowDetail(uint8_t slot, const char *text);

/** The detail could not be loaded: back to the cards with a notice. */
void Ui_DetailFailed(void);

/** Local time from the host (seconds since 1970, as TIME), 0 = unknown.
 *  The clock strip counts on from it. */
void Ui_SetTime(uint32_t local_time);

/** UI_DASH_VIEW_* (for tests). */
uint32_t Ui_DashView(void);

/* ------------------------------------------- status icons, picture card */

#define UI_MAX_INDICATORS 4U       /**< = PROTO_MAX_INDICATORS */
#define UI_PICTURE_W      200U     /**< = PROTO_PICTURE_W, the card's size */
#define UI_PICTURE_H      92U      /**< = PROTO_PICTURE_H */
#define UI_PICTURE_BYTES  (UI_PICTURE_W * UI_PICTURE_H * 2U) /**< RGB565, = PROTO_PICTURE_BYTES */

/** Indicator colours; values match PROTO_INDICATOR_*. */
enum
{
    UI_INDICATOR_NORMAL = 0,
    UI_INDICATOR_RED = 1,
    UI_INDICATOR_YELLOW = 2,
    UI_INDICATOR_GREEN = 3,
};

/** One status icon at the right of the clock strip (ADR-010). */
typedef struct
{
    uint8_t icon;   /**< icon number (protocol.md "Icon") */
    uint8_t colour; /**< UI_INDICATOR_* */
    uint8_t number; /**< shown next to the icon, 0 = none, above 99 "99+" */
    uint8_t slot;   /**< detail a tap opens (0-7), UI_DASH_NO_SLOT = none */
} UiIndicator;

/** The status icons, in their order (at most UI_MAX_INDICATORS; copied). */
void Ui_SetIndicators(const UiIndicator *items, uint32_t count);

/** Number of status icons shown (for tests). */
uint32_t Ui_IndicatorCount(void);

/** The buffer the shown picture is kept in (UI_PICTURE_BYTES; SDRAM on the
 *  board). Without one the picture card shows its placeholder. */
void Ui_SetPictureBuffer(uint8_t *buffer);

/** The picture card's picture (UI_PICTURE_BYTES of RGB565, copied), or
 *  NULL: the placeholder. */
void Ui_SetPicture(const uint8_t *pixels);

/** True while the picture card shows a picture (for tests). */
bool Ui_PictureShown(void);

/* ---------------------------------------------------------------- prompt */

#define UI_PROMPT_BYTES 1024U   /**< = PROTO_PROMPT_MAX_BYTES */
#define UI_NO_PROMPT    0xFFFFU

/**
 * @brief  Show prompt @p id over every screen (ADR-007: a Claude Code
 *         permission request) and light the screen. Record text as a
 *         detail (protocol.md "Record text"), first line
 *         `title ␟ subtitle ␟ seconds`: the countdown. Replaces a shown
 *         prompt. A button calls UiDashHandlers.prompt_action once; the
 *         prompt stays until Ui_ClosePrompt, or closes itself 5 s after its
 *         countdown ran out.
 */
void Ui_ShowPrompt(uint8_t id, const char *text);

/** Close prompt @p id if it is shown. */
void Ui_ClosePrompt(uint8_t id);

/** The shown prompt's id, or UI_NO_PROMPT (for tests). */
uint16_t Ui_PromptShown(void);

/* ---------------------------------------------------------------- banner */

#define UI_BANNER_BYTES 256U    /**< = PROTO_BANNER_MAX_BYTES */

/**
 * @brief  Show a banner over the top of every screen (ADR-009: a new
 *         notification). Record text (protocol.md "Banner"): first line
 *         `icon ␟ title ␟ seconds ␟ slot`, then up to two text lines. It
 *         closes itself after `seconds` (1-30, otherwise 5) and replaces a
 *         shown banner. Dropped while a prompt is shown or the screen is
 *         dark (it does not wake it); a prompt that appears closes it.
 *         A tap closes it; on the main screen a tap with a `slot` of a shown
 *         card also opens that card's detail on the Dashboard tab, as a tap
 *         on the card would (elsewhere, e.g. the settings, it only closes).
 */
void Ui_ShowBanner(const char *text);

/** True while a banner is shown (for tests). */
bool Ui_BannerShown(void);

/* ----------------------------------------------------------- now playing */

#define UI_MEDIA_BYTES 512U                           /**< = PROTO_MEDIA_MAX_BYTES */
#define UI_ART_SIZE    120U                           /**< = PROTO_ART_SIZE */
#define UI_ART_BYTES   (UI_ART_SIZE * UI_ART_SIZE * 2U) /**< RGB565, = PROTO_ART_BYTES */

/** Playback status; values match PROTO_MEDIA_STATUS_*. */
enum
{
    UI_MEDIA_NONE = 0,    /**< no media session */
    UI_MEDIA_STOPPED = 1,
    UI_MEDIA_PLAYING = 2,
    UI_MEDIA_PAUSED = 3,
};

/** Enabled controls; values match PROTO_MEDIA_CAN_*. */
#define UI_MEDIA_CAN_PREVIOUS   0x01U
#define UI_MEDIA_CAN_PLAY_PAUSE 0x02U
#define UI_MEDIA_CAN_NEXT       0x04U

/** The buffer the shown cover is kept in (UI_ART_BYTES); without one no
 *  cover is shown. The firmware passes SDRAM. */
void Ui_SetMediaArtBuffer(uint8_t *buffer);

/** The media record (protocol.md "Media record": title ␟ artist ␟ album ␟
 *  app); cut to UI_MEDIA_BYTES. */
void Ui_SetMedia(const char *text);

/**
 * @brief  Playback state. While UI_MEDIA_PLAYING the shown position counts
 *         on by itself (up to @p duration_s when it is not 0).
 */
void Ui_SetMediaState(uint8_t status, uint8_t controls, uint32_t position_s, uint32_t duration_s);

/** The cover (UI_ART_BYTES of RGB565, copied), or NULL: none. */
void Ui_SetMediaArt(const uint8_t *pixels);

/** UI_MEDIA_* shown (for tests). */
uint8_t Ui_MediaStatus(void);

/** Position shown on the Music tab in seconds (for tests). */
uint32_t Ui_MediaPosition(void);

/** LVGL memory in use and its peak, in bytes (for tests and the log). */
void Ui_MemoryUse(uint32_t *used, uint32_t *peak);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
