# Host Application

Two Windows programs (C++17), installed together by the setup from the
[Releases page](https://github.com/egungor/stm32-desk-dashboard/releases).
They find the board over USB HID and feed it everything it shows.

- **`dashboard-tray`** — the everyday program, without a console window:
  the tray icon (left click: the [settings window](Settings-Window), right
  click: the menu), the service that talks to the board, and the
  settings window itself (WebView2).
- **`dashboard-host`** — console tools for checking and scripting each
  part, and the Claude Code hook commands.

Only one of `dashboard-tray` and `dashboard-host run` serves the board at
a time (a named mutex); the other console commands work while the tray
program runs, and `alias`, `weather`, `picture add` and `menu import`
reach the running tray at once.

## What the service does

| Service | Source | To the board |
|---|---|---|
| Audio (`HostController`, `WinAudioSystem`) | Core Audio: active render endpoints, the default, volume, mute; switching through `IPolicyConfig` | the device list, the default, volume, mute; the Audio tab |
| Preferences (`PreferenceStore`) | `preferences.txt` | names, favourites, order, the settings record |
| Time (`DashController`) | the PC clock | `TIME` on connect |
| Weather (`WeatherService`) | Open-Meteo forecast, every 30 min (5 min after a failure), on a worker thread | card slot 0 and its detail |
| Menu (`MenuService`, `MenuLists`) | `menu-YYYY-MM.json`, `menu-lists.txt`, `menu-settings.txt` | card slot 1, its detail, the morning warning (prompt 255) |
| Notifications (`NotificationService`) | `UserNotificationListener`, every 500 ms | the bell (status icon), the list (slot 2), banners |
| Claude Code (`ApprovalService`, `WinHookPipe`) | the hook commands over `\\.\pipe\stm32-desk-dashboard-hook` | prompts, the Claude Code symbol, its list (slot 3) |
| Now playing (`MediaService`, `WinMediaSession`) | `Windows.Media.Control`, every 500 ms; the cover through WIC | media record, state, 120 × 120 cover |
| Picture (`PictureService`, `WinPicture`) | the picture library (`pictures.json`) | the 480 × 272 picture, the shuffle |
| Media keys (`MediaKeys`) | — | `SendInput` when no media session takes a control |

Each source runs behind a small interface; the Windows parts are in
`Platform/Windows`, the rest builds and is unit tested on Linux too.
Core Audio callbacks, the HID reader and the pipe server only queue
events; one service loop handles them.

## Commands

| Command | What it does |
|---|---|
| `run` | Serve the board over USB HID; log every frame (`<-` from the board, `->` to it). Ctrl+C prints counters |
| `version` | The program's version and protocol and, with a board connected, its firmware version and protocol (also while the tray serves it) |
| `list` | Playback devices (`*` = default), volume, mute, names and favourite marks |
| `set-default <n>` | Make device `<n>` from `list` the default |
| `volume <0-100>` | Volume of the default device |
| `mute on\|off` | Mute of the default device |
| `watch` | Run the protocol controller without a board and print what it would send |
| `alias <n> <name>` / `alias <n> --clear` | Show device `<n>` under another name on the board (at most 32 bytes of UTF-8) |
| `startup on\|off\|status` | Start the tray program at Windows logon, or not |
| `weather` | Print the weather card and detail for the saved place |
| `weather <city>` / `weather <lat> <lon> [name]` | Find a city (Open-Meteo geocoding) or give the place exactly; saved in `weather.txt` |
| `menu` | Print the menu card and the day's detail |
| `menu import <file> ...` | Read menu images with the Claude API, or saved portal pages (`.html`) without it, into `menu-YYYY-MM.json` |
| `media` | What plays in the Windows media session: app, texts, status, position, controls, cover |
| `media previous\|play-pause\|next` | Send that control to the session, then print |
| `notify` | The notifications card, its list and the banner of the newest toast, as the tray sends them |
| `notify watch` | Print new toasts as they arrive |
| `notify focus` | Whether Windows accepts pop-ups (Do not disturb), at every change |
| `notify probe [n]` | The notification spike's probe (`docs/notification-spike.md`) |
| `picture` / `picture add <file>` | List the picture library and check that the shown picture loads / add a photo |
| `hook permission` | Claude Code `PermissionRequest` hook: hands the request to the tray program and the board |
| `hook resolved` | Claude Code `PostToolUse`, `PostToolUseFailure`, `Stop`, `UserPromptSubmit` hook: closes requests answered elsewhere |
| `hook probe` | Observing hook for the approval spike (`docs/hook-spike.md`) |

Arguments are read as UTF-8, so names may contain Turkish letters.

## Files

Everything lives in `%APPDATA%\stm32-desk-dashboard\`; the table is on
the [Settings Window](Settings-Window#files-in-the-settings-folder) page.
Files are replaced atomically; an unreadable `preferences.txt` is kept as
`preferences.bad` and the defaults are used. After editing a file by
hand, restart the tray program (or run one of the commands above that
writes it): the service does not poll the files.

## The log

"Günlüğü aç" in the tray menu, or `run` in a console:

```text
Controller connected: <product> <device path>
Firmware: 0.9.0-beta.14, protocol 7
-> DEVICE_LIST seq=4 gen=3 1/6 index=0 default favorite alias icon=speakers "Hoparlörler (Realtek(R) Audio)"
<- SELECT_DEVICE seq=12 gen=3 index=1
-> DEVICE_CHANGED seq=19 index=1
Volume limit: "Oyun kulaklığı" 78% -> 40%
```

(an illustration of the format, not a captured log)

`<-` is a frame from the board, `->` one to it. Record parts (cards,
details, prompts, banners) are logged with their part numbers and
sizes, not their text; notifications appear by app name only.

## How the audio part works

- **Audio** (`WinAudioSystem`): active render endpoints, names and form
  factors through the documented MMDevice API. Switching the default uses
  the undocumented `IPolicyConfig` interface for the console and
  multimedia roles; see
  [ADR-002](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-002-default-device-switching.md)
  for the risk.
- **USB HID** (`WinHidLink`): once a second while not connected, look for
  VID:PID `1209:0002`, usage page `0xFF00`, 65-byte reports. A reader
  thread queues received frames; writes time out after 1 s.
- **Protocol** (`HostController`, `DeviceRegistry`): each device gets a
  session-local index (0–254) kept while it stays present; at most 32
  devices, names cut to 48 bytes at a UTF-8 boundary. The full list goes
  out when the board is opened or asks for it, then only what changed. A
  selection that arrives before this session's list was sent is refused.

## Code layout

```text
host/
├── App/                HostController, DashController, FrameRouter, preferences,
│                       SettingsModel (the window's JSON), BackgroundFetch
├── Audio/              IAudioSystem, DeviceRegistry, AudioMapping, VolumeLimit
├── Hid/                IFrameSink, HidMatch
├── Weather/            WeatherService, WeatherCard, WeatherReport, WeatherLocation
├── Menu/               MenuService, MenuData, MenuImport (Claude API), MenuHtmlImport, MenuLists
├── Media/              MediaService, MediaCover
├── Notify/             NotificationService, ToastText
├── Picture/            PictureService, PictureLibrary
├── Hook/               ApprovalService, ClaudeHook
├── Platform/Windows/   WinAudioSystem, WinHidLink, WinMediaSession, WinNotificationListener,
│                       WinPicture, WinHookPipe, WinSettingsWindow, HostService,
│                       main.cpp (dashboard-host), tray.cpp (dashboard-tray), settings.html
└── Tests/              test_host.cpp (Linux and Windows)
```

Details, manual checks and the cross-build on Linux:
[host/README.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/host/README.md).
