# Windows Host Application

The Windows host application will:

1. Detect the STM32 HID device.
2. Enumerate Windows audio render endpoints.
3. Maintain a host-side audio device model.
4. Change the default playback endpoint.
5. Monitor endpoint/default-device changes.
6. Communicate state to the STM32.

Target language: C++17.

## Current state

The platform-independent core, the Windows Core Audio layer and the USB
HID transport exist. Two Windows programs share them:
`dashboard-tray.exe` serves the controller from the system tray and can
start with Windows (#74); `dashboard-host.exe` is the console program
(`run` serves the controller the same way with a log on screen; the other
commands test the audio layer on their own).

```text
host/
├── Audio/
│   ├── IAudioSystem.h      # interface to the Windows audio system (no Windows types)
│   ├── AudioMapping.*      # Windows form factor -> icon, volume scalar <-> percent
│   ├── DeviceRegistry.*    # endpoints -> device_index, DEVICE_LIST entries
│   └── VolumeLimit.*       # volume limit on switch (ADR-004)
├── Hid/
│   ├── IFrameSink.h        # outgoing 64-byte protocol frames
│   └── HidMatch.h          # which HID interface is the controller (ADR-003)
├── App/
│   ├── HostController.*    # protocol v5 audio exchanges (protocol/protocol.md)
│   ├── DashController.*    # dashboard records: cards, details, actions, TIME (ADR-006)
│   ├── FrameRouter.*       # one decode per frame; audio vs dashboard commands
│   ├── MediaKeys.*         # media strip ACTION -> previous / play-pause / next key
│   ├── HttpClient.h        # IHttpClient: GET and POST, for tests without a network
│   ├── BackgroundFetch.*   # one HTTP GET at a time on a worker thread
│   ├── SequencedSink.h     # one sequence counter for every frame the host sends
│   ├── Preferences.*       # aliases, order, favorites, settings record (ADR-004)
│   ├── PreferenceFile.*    # text format of the preference file
│   ├── PreferenceStore.*   # load, save at most once per second, reload on change
│   ├── EventQueue.h        # serialises Core Audio and HID events into the controller
│   ├── FrameText.*         # readable one-line form of a protocol message
│   └── Startup.*           # Run value of "start with Windows", tray text (#74)
├── Hook/
│   ├── ClaudeHook.*        # PermissionRequest decision JSON, pipe lines, settings snippet, probe (#24)
│   └── ApprovalService.*   # Claude Code requests: queue, prompt, timeouts, card slot 3 (#26)
├── Media/                  # now playing (ADR-008, #31)
│   ├── MediaSession.h      # IMediaSession: snapshot (texts, status, position, cover), commands
│   ├── MediaCover.*        # square crop, BGRA -> RGB565, app name from the AppUserModelID
│   └── MediaService.*      # MEDIA_PART / MEDIA_STATE / ART_PART, controls to the session or keys
├── Notify/                 # Windows notifications (ADR-009, #34; spike #33)
│   ├── NotificationSource.h # INotificationSource (the toasts), IFocusState (Do not disturb)
│   ├── NotificationService.* # card slot 2, its list, a BANNER_PART for each new toast
│   └── ToastText.*         # spike: <text> lines of a toast payload, FILETIME -> Unix time
├── Menu/                   # the menu of the day (#20)
│   ├── MenuData.*          # menu-YYYY-MM.json, merge by date, summary, dates
│   ├── MenuHtmlImport.*    # saved portal pages (.html) -> days, without the Claude API
│   ├── MenuImport.*        # images -> Claude API (structured output) -> month files
│   ├── MenuLists.*         # menu-lists.txt: "never" / "don't like" dishes, Turkish case rules (ADR-010)
│   └── MenuService.*       # card slot 1 (coloured by the lists), the day detail, the 08:00 warning,
│                           #   menu-settings.txt (the next-day hour)
├── Picture/                # the picture card (ADR-010, #47)
│   ├── PictureSource.h     # IPictureSource: the 480 x 272 RGB565 picture, or none
│   ├── PictureLibrary.*    # pictures.json: photos, crops, shuffle (ADR-012)
│   └── PictureService.*    # PICTURE and its PICTURE_PARTs after the cards
├── Weather/                # the weather card (#18)
│   ├── WeatherReport.*     # Open-Meteo URL and answer, WMO code -> icon and text
│   ├── WeatherLocation.*   # weather.txt, Open-Meteo geocoding
│   ├── WeatherCard.*       # when to fetch; card and 10-day detail records
│   └── WeatherService.*    # location file, fetch, card slot 0 and its detail
├── third_party/nlohmann/   # nlohmann/json 3.11.3, single header (MIT, ADR-006)
├── Platform/Windows/       # Windows only
│   ├── WinAudioSystem.*    # IAudioSystem on Core Audio / MMDevice
│   ├── WinHidLink.*        # HID discovery, reader thread, IFrameSink
│   ├── WinMediaKeys.*      # IMediaKeys on SendInput (VK_MEDIA_*)
│   ├── WinMediaSession.*   # IMediaSession on Windows.Media.Control (C++/WinRT), cover through WIC
│   ├── WinNotificationListener.* # UserNotificationListener polled every 500 ms (C++/WinRT);
│   │                       #   WinFocusState: SHQueryUserNotificationState (ADR-009)
│   ├── WinNotificationProbe.* # spike #33: UserNotificationListener and wpndatabase.db (winsqlite3)
│   ├── WinHookPipe.*       # \\.\pipe\stm32-desk-dashboard-hook server, current user only (#26)
│   ├── WinHttpClient.*     # IHttpClient on WinHTTP (HTTPS, system proxy)
│   ├── WinMenuImport.*     # menu import from images or saved pages; month files in %APPDATA%
│   ├── WinPicture.*        # the library's files, the shown picture, the shuffle, WIC (ADR-012)
│   ├── PolicyConfig.h      # undocumented IPolicyConfig (ADR-002)
│   ├── WinPreferenceFile.* # %APPDATA%\stm32-desk-dashboard\preferences.txt, weather.txt, ...
│   ├── WinStrings.*        # UTF-16 <-> UTF-8
│   ├── HostService.*       # the "run" loop, shared by console and tray (#74)
│   ├── WinStartup.*        # HKCU Run value, single instance mutex (#74)
│   ├── main.cpp            # dashboard-host console program
│   └── tray.cpp, tray.rc   # dashboard-tray tray program and its icon (#74)
├── Tests/
│   ├── test_host.cpp       # fake audio system + fake sink, no framework
│   └── WeatherSamples.h    # Open-Meteo answers in the documented format
└── CMakeLists.txt
```

## Windows audio layer

- Enumeration (active render endpoints, friendly name, form factor),
  default endpoint (`eConsole`), volume and mute of the default endpoint:
  documented MMDevice / `IAudioEndpointVolume` APIs.
- Switching the default: `IPolicyConfig::SetDefaultEndpoint` for the
  `eConsole` and `eMultimedia` roles. This interface is undocumented; see
  `docs/decisions/ADR-002-default-device-switching.md`.
- `IMMNotificationClient` and `IAudioEndpointVolumeCallback` only push
  events into `EventQueue`; repeated audio events of one kind collapse
  into one. The main thread pops them and calls `HostController`.

## HID transport

Details of the USB interface: `docs/decisions/ADR-003-usb-hid-interface.md`.

- Discovery: every second, while not connected, the HID interfaces
  present (`SetupDiGetClassDevs`) are checked with `HidD_GetAttributes`
  and `HidP_GetCaps`. The controller is VID:PID `1209:0002`, usage page
  `0xFF00`, usage `1`, 65-byte input and output reports (report ID 0 +
  one 64-byte frame). The rule is `isController()` in `Hid/HidMatch.h`
  and is unit tested.
- Reading: a worker thread reads input reports (overlapped I/O) and
  queues each frame (`EventQueue::pushFrame`). Frames are not validated
  there; `HostController` decodes them. At most 64 received frames wait
  in the queue; more are dropped and counted.
- Writing: `sendFrame()` writes one output report from the consumer
  thread and gives up after 1 s if the STM32 does not take it.
- Connect / disconnect are queued as `HidConnected` / `HidDisconnected`.
  On connect the controller sends the full list, default, volume and
  mute at once (`onConnected()`), so a board that stayed plugged in while
  the host restarted drops the previous session's list. On disconnect it
  forgets what was sent (`onDisconnected()`) and sends nothing.
- Only the consumer thread touches `HostController` and the audio
  system; the HID thread and the Core Audio threads only push events.

## Console program (`dashboard-host.exe`)

```text
dashboard-host run               serve the controller over USB HID; log every
                                   frame in both directions (Ctrl+C stops)
dashboard-host list              playback devices (* = default), volume, mute
dashboard-host set-default <n>   make device <n> from "list" the default
dashboard-host volume <0-100>    volume of the default device
dashboard-host mute on|off       mute of the default device
dashboard-host watch             run HostController; print the frames it
                                   would send to the STM32 (Ctrl+C stops)
dashboard-host alias <n> <name>  show device <n> from "list" as <name> on the
                                   board (at most 32 bytes of UTF-8)
dashboard-host alias <n> --clear remove the alias
dashboard-host startup on|off|status
                                   start dashboard-tray.exe (next to this
                                   program) at Windows logon, or not
dashboard-host weather           fetch the forecast for the saved location
                                   and print the card and the detail
dashboard-host weather <city>    find <city> with the Open-Meteo geocoding API
                                   and save it as the weather location
dashboard-host weather <lat> <lon> [name]
                                   save a location exactly
dashboard-host hook permission   Claude Code PermissionRequest hook: the request goes
                                   to the board through the tray program (ADR-007)
dashboard-host hook resolved     Claude Code PostToolUse, PostToolUseFailure, Stop and
                                   UserPromptSubmit hook: a request answered in Claude
                                   Desktop or the terminal leaves the board (ADR-007)
dashboard-host hook probe        Claude Code PermissionRequest hook for the step 10
                                   spike: logs to hook-probe.log, answers as
                                   hook-probe.txt says (docs/hook-spike.md)
dashboard-host media             what plays in the Windows media session: app,
                                   texts, status, position, controls, cover
dashboard-host media previous|play-pause|next
                                   send that control to the session, then print
dashboard-host notify probe [n]  notification spike (#33): the notification
                                   listener and the notification database; the
                                   last n toasts (docs/notification-spike.md)
dashboard-host notify watch      print new toasts as they arrive (Ctrl+C stops)
dashboard-host notify            the notifications card, its detail and the
                                   banner of the newest toast, as the tray
                                   sends them (ADR-009)
dashboard-host notify focus      whether Windows accepts pop-ups (Do not
                                   disturb), at every change (Ctrl+C stops)
dashboard-host picture [add <file>]
                                   the picture library (ADR-012): list it and
                                   check that the shown picture loads, or add
                                   a photo (a running tray reads it at once)
dashboard-host menu              print the menu card and the day's detail
dashboard-host menu import <file> [<file> ...]
                                   read menu images with the Claude API, or
                                   saved portal pages (.html) without it, into
                                   menu-YYYY-MM.json
```

Arguments are read as UTF-8 (`CommandLineToArgvW`), so aliases may
contain Turkish letters. `list` prints each device's alias and favorite
mark under its name.

`dashboard-host version` prints the program's version and protocol and,
if a board is connected, its firmware version and protocol (ADR-011),
also while the tray program serves it.

## Weather (#18)

The Dashboard's first card (slot 0) and its detail: now (condition,
feels like, humidity, wind) and 10 days (icon, min / max, chance of
rain). Data from the Open-Meteo forecast API (no key), for one fixed
location kept in `%APPDATA%\stm32-desk-dashboard\weather.txt`:

```text
# stm32-desk-dashboard weather location
name=İstanbul
latitude=41.0138
longitude=28.9497
```

`dashboard-host weather <city>` writes it; the tray program reads it again
every minute. The forecast is fetched every 30 minutes on a worker thread
(`BackgroundFetch`), 5 minutes after a failure; until then the card keeps
the last data and says "Güncellendi 14:00". Texts follow the board's
language (settings record). WMO weather codes map to the icon numbers of
`protocol/protocol.md` ("Icon") in `Weather/WeatherReport.cpp`.

## Menu of the day (#20)

The Dashboard's second card (slot 1): the main dish, the soup and
"Salata bar: 6 çeşit"; "Resmi tatil" on a holiday, "Menü yüklenmedi"
without data. From 14:00 on a working day (lunch is over) and at the
weekend it shows the next working day, with a badge ("Sal 27", "Pzt 2").
The hour is set in the settings window (Dashboard → Menu → "Ertesi
günün menüsü": 11:00 … 20:00 or off) and kept in `menu-settings.txt`
(`next_day_hour=14`; `0` off).
Its detail opens on the card's day and shows the day's five lines and the
salad bar; ‹ › move by working day.

Food lists (`menu-lists.txt`, settings window → Dashboard → food lists):
`[istemiyorum]`, `[sevmiyorum]` and `[seviyorum]`, one name per line
(ADR-010 and its amendment). The main dish alone colours the card:
orange, yellow, or green; a liked main dish keeps it green with a
gold-leaf frame and a gold icon. The detail shows every listed dish in
its list's colour and icon. The warning (a prompt, "Tamam") comes from
08:00 once a day, and again for the card's day when the lists change:
the panel in the card's colour when the main dish is listed, a title
strip when only other dishes are on "never" / "don't like"; a liked side
dish alone gives none.

Import: tray menu "Menü görüntülerini yükle…" (one or more PNG, JPEG,
WebP or GIF files, at most 10 and 5 MB each) or
`dashboard-host menu import <files>`. The images go to the Claude API
(Messages API, `https://api.anthropic.com/v1/messages`) in one call with
a structured output (`output_config.format`, a fixed JSON schema); the
host checks the answer, gives each day the year closest to the import
date and merges the days by date into
`%APPDATA%\stm32-desk-dashboard\menu-YYYY-MM.json` (meal and salad bar
images can be imported separately). A balloon shows a summary, e.g.
"Ekim 2026: 20 gün yemek, 17 gün salata bar, 2 tatil; salata bar eksik:
26, 27, 30 Ekim". The month file can be corrected in Notepad; the tray
reads it again within a minute.

Saved portal pages instead of images: open "Aylık Yemek Menüsü" in the
browser, choose the "Öğle" tab and save the page (Ctrl+S), then the
"Salatabar" tab into a second file, and load both `.html` files the same
way (`Menu/MenuHtmlImport`). No Claude API call and no key: each day
column (`portal-food-menu-table-column`: "05 Ekim / Pazartesi" and its
lines) is read directly, the text put into sentence case ("MEYVE/FISTIKLI
BAKLAVA" -> "Meyve / fıstıklı baklava"); "RESMİ TATİL" is a holiday,
"Yemek listesi bulunamadı" is left out. The page's active tab tells the
meal from the salad bar; a page saved on the "Akşam" tab is read as the
meal too (the portal has shown the lunch there), one saved on another tab
("Diyet", ...) is refused. Images and pages are loaded separately.

The API key: `%APPDATA%\stm32-desk-dashboard\claude.txt`

```text
api_key=sk-ant-...
model=claude-sonnet-5-5
```

(`model` is optional) or the `ANTHROPIC_API_KEY` environment variable.
The images are sent to Anthropic for this one call; nothing else is.

## Claude Code approvals (#26, ADR-007)

Permission requests of Claude Code sessions on this PC can be answered
on the board:

1. Tray menu → "Claude Code hook ayarını kopyala" and merge the snippet
   into `%USERPROFILE%\.claude\settings.json` (user settings of this PC,
   not a repository's `.claude/`).
2. When a session wants to run a command or edit a file, Claude Code
   starts `dashboard-host hook permission`; it hands the request to the
   tray program over `\\.\pipe\stm32-desk-dashboard-hook`, and the
   board shows it over every screen: Onayla / Reddet / Terminalde sor,
   with a 60 s countdown.
3. Onayla allows, Reddet denies; Terminalde sor, no answer, no tray
   program, no board, or a request that does not fit the screen leave the
   decision to the normal question in the terminal. Nothing is ever
   allowed by default.
4. A request answered in Claude Desktop (Code tab) or in the terminal
   instead leaves the board: the snippet also starts
   `dashboard-host hook resolved` on `PostToolUse` / `PostToolUseFailure`
   (the tool ran: that request closes), `Stop` and `UserPromptSubmit`
   (the turn ended or you wrote again: every request of the session
   closes). The list shows it as "PC'de cevaplandı". Copy the snippet
   again after an update that adds these entries.

The Claude Code card shows waiting requests and the last decision; its
detail the last five.

## Now playing (#31, ADR-008)

The Music tab and the Dashboard's media strip show what plays on the PC.
`WinMediaSession` reads Windows' system media transport controls
(`Windows.Media.Control`, the session Windows calls current, as in the
volume flyout) on its own thread every 500 ms: title, artist, album, the
app, playing / paused, the enabled controls and the timeline. For a new
track it reads the thumbnail (again for a few seconds if the player
publishes it late), crops a centred square, scales it to 120 × 120 with
WIC and converts it to RGB565. A change queues `MediaChanged`.

`MediaService` sends the media record, `MEDIA_STATE` and the cover
(`protocol.md`, "Now playing") on connect and on `GET_MEDIA`; then a new
record when the texts change, `MEDIA_STATE` when the status, controls,
cover or duration change, after a seek (more than 2 s off) and every
30 s while playing, and a new cover 8 parts per loop turn (about 2.7 s
in all), so other frames are not held up. `host.log` shows only the
first and the last part of a cover.

The controls (`ACTION` kind 2) go to the session (skip previous, toggle
play/pause, skip next); without a session, or when it refuses, to the
media keys of step 7.

`SEEK` (the Music tab's progress bar, #55) goes to the session
(`TryChangePlaybackPositionAsync`) when it can seek (`controls` bit 3,
`IsPlaybackPositionEnabled`); otherwise `ERROR`. For 3 s after a seek
`MediaService` counts from the new position, as the board does, because
players update their timeline late.

`dashboard-host media` prints what the reader sees (useful to check a
player), `dashboard-host media next` etc. sends a control.

## Install

`installer/` builds a setup (Inno Setup) that installs both programs for
the current user, starts the tray program with Windows and starts it right
away; see `installer/README.md`. CI builds it as the artifact
`stm32-desk-dashboard-setup` (workflow "Host installer").

## Tray program (`dashboard-tray.exe`, #74)

The same service as `run` without a console window (Windows GUI
subsystem). The service loop (`HostService`) runs on a worker thread; the
main thread owns a hidden window, the tray icon, its menu (right click)
and the settings window (left click):

- the connection state ("Kumanda bağlı" / "Kumanda bekleniyor"), also
  in the icon's tooltip;
- "Ayarlar…": the settings window (below), also a left click on the icon;
- "Günlüğü aç": `host.log` in the settings folder: the same lines as `run`
  prints, with the local time; above 1 MB at start it becomes
  `host.log.1`;
- "Çıkış": stops the service (preferences saved) and removes the icon.

Start with Windows (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`,
value `STM32 Desk Dashboard`, current user only), the menu import, the
picture, the food lists, the menu's next-day hour, the Claude Code hook
snippet and the settings folder are in the settings window (#66).

The menu and tooltip text follow the language in the board's settings
record (Turkish or English). Logging off or shutting down Windows stops
the service the same way. The icon comes back when Explorer restarts
(`TaskbarCreated`).

### Settings window (ADR-011, #61)

A window with a WebView2 control showing `Platform/Windows/settings.html`
(a resource of `dashboard-tray`; no network, no files): General
(language, start with Windows, startup screen, screen timeout, upside
down), Appearance (theme, accent, Audio tab layout, grid, startup tab),
Audio (the outputs' names on the board, favourites and order; the
volume limit), Dashboard (weather location with a search, menu images,
the Claude API key, the food lists), Pictures (ADR-012: add photos,
show, crop with zoom, delete, the shuffle), Claude Code (copy the
hook setting) and About (program and firmware versions, a warning when
their protocols differ, the settings folder and the log).

- The page sends JSON requests, the tray answers with the state
  (`App/SettingsModel.h`). Preference requests go to the service loop
  (`HostEvent::SettingsRequest`), which applies them, tells the board and
  saves `preferences.txt`; its snapshot keeps the open window up to date,
  also when a setting is changed on the board.
- The weather, the food lists and the picture library (`pictures.json`)
  are files the tray writes, then pushes `SettingsChanged`; the API key
  goes to `claude.txt`. Which picture is shown is the service's
  (`picture-current.txt`); "Göster" is a request to it.
- Pictures (ADR-012): an added photo is copied unchanged into
  `pictures\<id>.<ext>` with an upright JPEG preview in
  `pictures\previews` (the window cannot show HEIC); the window reads
  the folder as `https://pictures.desk-dashboard/`. The crop is kept as
  fractions of the upright photo; the board's 480 x 272 is cut from the
  original. The shuffle runs in the service; a new picture reaches the
  board in about 20 s and is shown when complete. A `picture.txt` from
  before #72 becomes the first picture.
- It needs the Microsoft Edge WebView2 Runtime (part of Windows 11);
  without it the tray says where to get it. Its cache is in
  `%LOCALAPPDATA%\stm32-desk-dashboard\WebView2`. CMake fetches the
  WebView2 SDK (NuGet, checked by SHA-256) into the build folder; only
  MSVC builds the window.

One program serves the controller at a time: `run` and the tray program
hold the named mutex `Local\stm32-desk-dashboard-host`; the second one
says so and exits. The other console commands (`list`, `alias`, …) still
work while the tray program runs.

### Preferences

Aliases, the device order, favorites and the board's settings record
(ADR-004) are kept in `%APPDATA%\stm32-desk-dashboard\preferences.txt`,
a UTF-8 text file (format in `App/PreferenceFile.h`).

- `run` loads the file at start (`Preferences: <path>`, `(new)` if it does
  not exist yet) and saves what the board or the settings window changes
  at most once per second and when it stops. It does not check the file
  by itself (#61): `alias`, `weather`, `picture add` and `menu import` set the
  named event `Local\stm32-desk-dashboard-settings` after writing, and
  the service then reads `preferences.txt` (`Preferences reloaded from
  …`, sent to the board), `weather.txt`, the menu files and
  `pictures.json` again. A change from the file wins over board changes
  not yet saved. After editing a file by hand, restart the tray program
  (or run one of those commands).
- The file is replaced atomically (`preferences.tmp`, then
  `MoveFileEx`). An unreadable file is kept as `preferences.bad` and the
  defaults are used.
- `watch` reads the file but never writes it.

Volume limit on switch (off by default; `volume_limit_on` and
`volume_limit` in the settings record): when the board selects an output
whose volume is above the limit, the host lowers it to the limit before
making it the default, and logs `Volume limit: "<name>" <from>% -> <to>%`.
Switches made in Windows are not limited, volumes are never raised and
mute is not touched (ADR-004).

`run` prints `Controller connected: <product> <device path>`, then
`Firmware: <version>, protocol <n>` from the board's USB configuration
string (ADR-011; a `WARN` when the protocol differs from the program's),
and
`Controller disconnected.`, then one line per frame: `<-` received from the
STM32, `->` sent to it (`[send failed]` if the write failed). Ctrl+C prints
counters (sent, send failures, rejected, ignored, errors received,
dropped).

`watch` needs no board: it starts as if the board had just been opened
(the full list), then prints
`DEVICE_LIST` / `DEVICE_CHANGED` / `VOLUME_CHANGED` / `MUTE_CHANGED` lines
as you change devices or volume in Windows.

Manual check of the audio layer on Windows (REQUIRES WINDOWS):

1. `list` shows the same devices and default as Settings → Sound.
2. `set-default <n>` switches the default; Settings → Sound follows.
3. `watch`, then change the default and the volume in Windows: one
   `DEVICE_CHANGED` / `VOLUME_CHANGED` line per change, no repeats.
4. Plug in or remove a device (e.g. headphones, HDMI): a new
   `DEVICE_LIST` with a new `gen=` and existing devices keeping their
   `index=`.

Manual check with the board (REQUIRES HARDWARE + WINDOWS; firmware from
#31, built without `APP_UI_DEMO`):

1. Start `run` before plugging in the board: `Controller connected`,
   then `-> DEVICE_LIST …` for every device, `-> DEVICE_CHANGED`,
   `-> VOLUME_CHANGED`, `-> MUTE_CHANGED` (a `<- GET_DEVICE_LIST` and a
   second list may follow if the board asked at the same moment). The
   LCD shows the same devices with the default marked.
2. Touch another device on the LCD: `<- SELECT_DEVICE index=…`,
   `-> DEVICE_CHANGED index=…`; Settings → Sound follows.
3. Change the default in Windows: one `-> DEVICE_CHANGED`; the LCD
   follows.
4. Unplug the board: `Controller disconnected.`; plug it back in: it
   reconnects within about 2 s and the list is sent again.
5. Start `run` with the board already plugged in: same as 1.
6. Ctrl+C: `send_failures=0 rejected=0 dropped=0` in a normal session.
7. Restart `run` with the board plugged in, after changing the default
   in Windows while it was closed: the LCD shows the current list and
   default right after `Controller connected`, without a tap.
8. Drag the volume slider on the LCD: a few `<- SET_VOLUME` lines (not
   one per pixel), the Windows volume follows and the knob does not jump
   back. Tap mute: `<- SET_MUTE`, `-> MUTE_CHANGED`, the icon changes.
   Change volume and mute in Windows: the LCD follows.

### Device registry

- Assigns each active render endpoint a session-local `device_index`
  (0–254). An endpoint keeps its index while it stays present. New
  endpoints get the next free index in round-robin order, so an index
  that was just released is not reused for another device at once.
- Keeps at most 32 devices (protocol limit) and always includes the
  default device.
- Truncates names to 48 bytes at a UTF-8 character boundary and drops
  NUL bytes.
- Changes `generation` only when the list content (order, indices, names,
  icons) changes.

### Host controller

- Device opened (`onConnected`) or `GET_DEVICE_LIST` → the full list,
  then `DEVICE_CHANGED`, `VOLUME_CHANGED`, `MUTE_CHANGED`.
- `SELECT_DEVICE` → sets the Windows default and confirms with
  `DEVICE_CHANGED`; `ERROR UNKNOWN_DEVICE` for an index not in the list
  or before the list has been sent since the last connect (its index may
  be from an earlier session), `ERROR OPERATION_FAILED` if Windows
  refuses.
- `SET_VOLUME` / `SET_MUTE` → applied to the default device, confirmed
  with `VOLUME_CHANGED` / `MUTE_CHANGED`.
- Windows notifications (`onDevicesChanged`, `onDefaultChanged`,
  `onVolumeChanged`) send only what changed since the STM32 was last told,
  and nothing while no list has been sent since the last connect.
- Malformed frames are answered with `ERROR`; an `ERROR` frame is never
  answered. Frames meant for the STM32 are dropped.
- Single-threaded by design: the Windows layer must queue Core Audio
  notifications (which arrive on COM threads) and call the controller from
  one thread.

## Build and test

Windows (Visual Studio 2022 or newer) or Linux. On Linux only the core
and its tests are built.

```text
cmake -S host -B build/host
cmake --build build/host --config Debug
ctest --test-dir build/host --build-config Debug --output-on-failure
```

CI runs this with MSVC on Windows and GCC on Linux
(`.github/workflows/host-ci.yml`). The controller is also run against
the firmware's protocol client in `tests/integration`.

Link libraries: `ole32`, `setupapi`, `hid`.

### Cross-build on Linux (MinGW-w64 + Wine)

A quick check of the Windows code without a Windows PC; Claude's sandbox
uses it before CI. Not the reference build (CI builds with MSVC).

```text
sudo apt install g++-mingw-w64-x86-64-posix wine64
cmake -S host -B build/host-mingw -DCMAKE_TOOLCHAIN_FILE=$PWD/host/cmake/mingw-w64.cmake
cmake --build build/host-mingw
```

The toolchain path must be absolute (CMake resolves it from the build
directory). To run the programs under Wine, copy the MinGW runtime DLLs
next to them (paths for GCC 13 on Ubuntu/Debian):

```text
cp /usr/lib/gcc/x86_64-w64-mingw32/13-posix/{libstdc++-6.dll,libgcc_s_seh-1.dll} \
   /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll build/host-mingw/
wine build/host-mingw/test_host.exe
```

What Wine shows: `test_host.exe` passes, `dashboard-host` starts,
`run` scans for the board and stops on Ctrl+C. What it cannot show: no
HID devices, and Core Audio is limited (`set-default` and `volume` fail
under Wine). None of this replaces the checks on Windows above.

## Still to do

- Hardware + Windows test of `run` with the board (list above).
- Windows test of the tray program and "start with Windows" (#74,
  `docs/hardware.md`).
