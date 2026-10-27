# UI Simulator

Headless Linux build of the touch UI in `firmware/ui/`, using the same
LVGL version and `lv_conf.h` as the firmware. It renders into a
480 × 272 RGB565 framebuffer (the LCD format), plays a fixed script of
UI states and taps, and writes one screenshot per step.

It needs no display, SDL or board, so it runs in CI and in the cloud
development container.

## Run

```text
git submodule update --init          # once: fetches LVGL
tools/run-ui-simulator.sh            # build, run, convert to PNG
```

On Windows (Visual Studio 2022 with C++ tools, CMake, Python 3):

```text
powershell -ExecutionPolicy Bypass -File tools\run-ui-simulator.ps1
```

`-CMake <path>` selects a CMake that is not on the PATH; `-OutDir`
changes the output directory.

Screenshots land in `build/ui-sim/` (or the directory given as the first
argument):

| File | State |
|---|---|
| `01-waiting.png` | No computer connected |
| `02-main.png` | Device list, "Kulaklık" active, page 1 of 2 |
| `03-pending.png` | "Hoparlörler" tapped, waiting for the host to confirm |
| `04-switched.png` | Host confirmed: "Hoparlörler" active |
| `05-page2.png` | Next page button tapped |
| `06-error.png` | "Bilgisayar yanıt vermiyor" notice over the main screen |
| `07-error-gone.png` | Same screen after the notice timed out |
| `08-volume.png` | Volume slider dragged up (bottom strip) |
| `09-muted.png` | Mute tapped and confirmed by the host |
| `10-no-volume.png` | No default device: volume and mute disabled |
| `11-empty.png` | Host reports no playback devices |
| `12-flip-armed.png` | Flip button held for 1 s (icon orange, not yet released) |
| `13-flipped.png` | After release, as the panel shows it with the board upside down |
| `20-favorites-marked.png` | Favorite stars and an alias ("Oyun kulaklığı") |
| `21-tab-favorites.png` | Favorites tab, chosen by the startup tab "last used" |
| `22-light.png`, `23-light-blue.png` | Light theme, orange and blue accent |
| `24-dark-green.png` | Dark theme, green accent |
| `25-grid-3x2.png`, `26-grid-3x3.png`, `27-grid-list.png` | Card grids and the list |
| `28-list-light.png` | List on the light theme |
| `29-english.png` … `31-english-waiting.png` | English text: main screen, notice, waiting screen |
| `40-settings.png` | Settings page, opened with the cog button |
| `41-settings-light-blue.png` | After tapping "Açık" and the blue swatch: rebuilt, still open |
| `42-settings-bottom.png` | Scrolled to the end: volume limit, flip, language |
| `43-settings-limit.png`, `44-settings-limit-moved.png` | Volume limit on; its slider moved to 75 % |
| `45-settings-english.png` | Flip on, language English |
| `46-main-after-settings.png` | Back on the main screen with the chosen settings |
| `60-main-pencil.png` | Main screen with the pencil button (edit handlers set) |
| `61-edit.png`, `62-edit-moved.png` | Edit mode; after the star and the right arrow on the first card |
| `63-keyboard.png`, `64-keyboard-typed.png` | Alias keyboard, empty and after typing "Oyunı" |
| `65-edit-page2.png` | Second page of the edit mode |
| `66-main-after-edit.png` | Main screen with the new order, alias and favorite |
| `70-list.png`, `71-list-favorites.png`, `72-list-light.png` | Layout B: all devices, favorites tab, light theme after raising the vertical volume |
| `73-tiles.png`, `74-tiles-switched.png`, `75-tiles-light.png` | Layout C; after tapping the first tile; light theme with the purple accent |
| `76-settings-layout-list.png` | Settings page with "Liste" chosen: the grid row greyed out |
| `82-detail-weather.png`, `82c-detail-weather-days.png`, `82d-detail-weather-days-scrolled.png` | The weather detail: the band, the tiles, the days in a strip, swiped sideways |
| `90-dashboard-strip-muted.png` | Dashboard bottom strip (#32): media controls on the left, volume muted on the right |
| `91-music-none.png` | Music tab, nothing playing |
| `92-music-playing.png`, `92-music-playing-later.png` | Music tab: cover (a test gradient), texts, progress at 1:23 and 10 s later |
| `92-music-seekable.png` | The session can seek (#55): the progress bar has a knob and takes taps |
| `93-music-paused.png` | Paused, long Turkish title, no cover and no duration, only play/pause enabled |
| `94-dashboard-playing.png` | Dashboard strip while playing: pause in the middle |
| `95-music-light.png` | Music tab in the light theme after a rebuild |
| `96-dashboard-notifications.png` | Dashboard before a notification banner |
| `103-dashboard-redesign.png` | Dashboard of ADR-010 (#47): sunny weather (dark text), a "never" dish (orange), a cover on the now-playing card, the picture, the bell (5, seen) and a yellow Claude Code icon |
| `104-dashboard-rain-red.png` | Rain, a "don't like" dish on Monday (yellow, badge), the bell "99+" and two Claude Code requests waiting (red) |
| `105-dashboard-light.png` | The light theme: the cards keep their colours; one green status icon |
| `106-menu-warning.png` | The morning menu warning (a prompt with one button) |
| `112-menu-liked.png`, `112b-menu-liked-light.png` | A liked main dish: the gold-leaf frame and a gold icon, dark and light theme; four lines (main dish, side dish, soup, dessert) |
| `113-menu-detail-marks.png` | The menu detail with dishes of the three lists in their colours |
| `114-menu-warning-liked.png`, `115-menu-warning-never.png` | The warning when the main dish is listed: the panel in the card's colour (gold leaf for a liked one) |
| `116-menu-warning-other.png` | The warning when only other dishes are listed: the title strip in the card's colour |
| `107-picture-full-screen.png` | The picture card tapped (#53): the picture full screen, 480 × 272 (taken between 103 and 104) |
| `97-banner.png` | A notification banner over the Dashboard (bell, app, countdown, two lines) |
| `98-banner-audio.png` | The banner over the Audio tab; a tap opens the notifications list |
| `99-notifications-detail.png` | The notifications list: app and time, sender and text |
| `100-banner-no-texts.png` | A banner with only the app (a toast without texts) |
| `101-banner-settings.png` | The banner over the settings page (a tap only closes it) |
| `102-banner-light.png` | The banner in the light theme after a rebuild |
| `50-screen-off.png` | Screen timeout of 1 minute run out: black, backlight off |
| `51-screen-woken.png` | After the waking touch and one more tap |

The script prints the LVGL memory in use after each rebuild and fails if
the peak passes 90 % of `LV_MEM_SIZE` or repeated rebuilds leak. LVGL
asserts abort the simulator (`UI_SIMULATOR` in `lv_conf.h`) instead of
hanging.

The script also checks behaviour: a tap calls the selection callback with
the right device, a tap on the active device does nothing, the page
button changes the page, dragging the slider reports values and one
release, the mute button asks for the opposite state, the disabled
mute button does nothing, and the flip button asks to flip only after a
long press, on release, on both screens. On the settings page every
control reports the whole record once with the right value, the page
stays open across the rebuild a change causes, scrolling over a control
changes nothing, the volume limit slider reports once on release in 5 %
steps, a new list or host settings leave the page open, and opening and
closing it leaks no memory. The screen timeout goes dark once after the
set minutes without a touch (and never with "never"), the waking touch
does not reach the control under it, a touch restarts the count, and
switching the timeout off from the host wakes the screen. In the edit
mode the star, the arrows (disabled at the ends) and the keyboard call the
edit handlers with the right device, position and UTF-8 text, the alias
stops at 32 bytes, × saves nothing, a new list keeps the screens open,
and repeated sessions leak no memory. In layouts B and C a tap on a row or
tile selects its device, C keeps the active device out of the tiles, the
rail and pills switch tabs, the vertical slider reports the volume, the
layout row switches layouts, and the grid row does nothing outside A. The
simulator exits non-zero if a check fails.

## How it works

- `main.c` creates the LVGL display in direct mode on a static
  framebuffer and a pointer input device driven by the script.
- Time is simulated (`lv_tick_set_cb`), so timers such as the 3 s error
  notice are deterministic.
- Each step writes the raw framebuffer (`<name>.bin`), which
  `tools/fb2png.py` converts. The same converter is used for SDRAM
  framebuffer dumps from the board, so simulator and hardware
  screenshots can be compared directly.

The simulator is a preview of layout and behaviour, not a hardware test.
Rendering speed, touch accuracy and colours on the real panel are
REQUIRES HARDWARE.
