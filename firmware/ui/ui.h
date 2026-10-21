/**
 * @file    ui.h
 * @brief   Touch UI of the audio controller (LVGL, hardware independent).
 *
 * The module only depends on LVGL. It knows nothing about the LCD, the
 * touch controller, USB or Windows: the caller creates the LVGL display
 * and input device, feeds in the state reported by the host, and receives
 * the user's choice through a callback. The same code runs in the firmware
 * and in the Linux simulator (firmware/simulator).
 *
 * Screens:
 *   - waiting: no host connected;
 *   - main: two tabs, all devices and favorites, in one of three layouts
 *     (UiSettings.layout): A cards in a 2x2, 3x2 or 3x3 grid or as a list
 *     (UiSettings.grid); B a list with a tab rail and vertical volume; C
 *     the active device in a large panel and the others as tiles. The
 *     active device is marked by colour and text; page buttons when there
 *     are more devices than fit on one page;
 *   - settings: opened with the cog button in the main screen's top bar;
 *     theme, accent colour, grid, startup tab, screen timeout, startup
 *     volume limit, flip and language. A change is applied at once and
 *     reported with the whole record (UiSettingsCallback);
 *   - edit mode: opened with the pencil button in the main screen's top
 *     bar; every device as a card with buttons to move it one place left
 *     or right, mark it as favorite, and give it an alias on an on-screen
 *     keyboard (Turkish layout). Edits go out through UiEditHandlers;
 *   - error notice: shown over any screen for UI_NOTICE_MS;
 *   - screen timeout: after settings.screen_timeout minutes without a touch
 *     the screen goes black and the backlight handler is told to switch
 *     the backlight off; the next touch only wakes it;
 *   - flip button (top right corner of both screens): held for
 *     UI_FLIP_HOLD_MS, it asks to turn the image by 180° when released.
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
    UI_STARTUP_SCREEN_DASHBOARD = 0, UI_STARTUP_SCREEN_AUDIO = 1, UI_STARTUP_SCREEN_COUNT = 2,
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

/** LVGL memory in use and its peak, in bytes (for tests and the log). */
void Ui_MemoryUse(uint32_t *used, uint32_t *peak);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
