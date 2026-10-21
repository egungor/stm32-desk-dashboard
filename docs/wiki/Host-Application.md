# Host Application

Two Windows programs (C++17), installed together by the setup from the
[Releases page](https://github.com/egungor/stm32-desk-dashboard/releases). Both find the board over USB HID, read Windows'
playback devices with Core Audio, and keep both in step:

- **`dashboard-tray`**: the everyday program, without a console window.
  A tray icon shows the connection state ("Kumanda bağlı" / "Kumanda
  bekleniyor"); its menu has "Windows ile başlat" (start at logon, current
  user only), "Menü görüntülerini yükle…" (menu of the day), "Claude
  Code hook ayarını kopyala", "Ayarlar klasörünü aç", "Günlüğü aç"
  (`host.log`, the same lines as `run`) and "Çıkış". Menu text follows
  the board's language setting. Besides the audio devices it serves the
  Dashboard tab: the time, the weather, the menu of the day, media keys
  and Claude Code approvals.
- **`dashboard-host`**: console tools, below.

Only one of `run` and the tray program serves the board at a time; the
other console commands work while the tray program runs. Preferences
(aliases, favorites, order, the board's settings) are kept in
`%APPDATA%\stm32-desk-dashboard\preferences.txt`.

## Commands

| Command | What it does |
|---|---|
| `run` | Serve the board over USB HID; log every frame (`<-` from the board, `->` to it). Ctrl+C prints counters |
| `list` | Playback devices (`*` = default), volume, mute |
| `set-default <n>` | Make device `<n>` from `list` the default |
| `volume <0-100>` | Volume of the default device |
| `mute on\|off` | Mute of the default device |
| `watch` | Run the protocol controller without a board and print what it would send |
| `alias <n> <name>` / `alias <n> --clear` | Show device `<n>` under another name on the board (at most 32 bytes of UTF-8) |
| `startup on\|off\|status` | Start the tray program at Windows logon, or not |
| `weather` | Print the weather card and 10-day detail for the saved location |
| `weather <city>` / `weather <lat> <lon> [name]` | Find a city (Open-Meteo geocoding) or give the place exactly; saved in `weather.txt` |
| `menu` | Print the menu card and the day's detail |
| `menu import <image> ...` | Read menu images with the Claude API into `menu-YYYY-MM.json` (key in `claude.txt` or `ANTHROPIC_API_KEY`) |
| `hook permission` | Claude Code `PermissionRequest` hook: hands the request to the tray program and the board (ADR-007) |
| `hook probe` | Observing hook for the approval spike (`docs/hook-spike.md`) |

**Volume limit on switch** (settings page on the board, off by default):
when the board selects an output louder than the limit, the host turns it
down to the limit before making it the default. Switches made in Windows
and the host's start are not limited.

## How it works

- **Audio** (`WinAudioSystem`): active render endpoints, names and form
  factors through the documented MMDevice API. Switching the default uses
  the undocumented `IPolicyConfig` interface for the console and
  multimedia roles; see
  [ADR-002](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-002-default-device-switching.md) for
  the risk.
- **USB HID** (`WinHidLink`): once a second while not connected, look
  for VID:PID `1209:0002`, usage page `0xFF00`, 65-byte reports. A reader
  thread queues received frames; writes time out after 1 s.
- **Protocol** (`HostController`, `DeviceRegistry`):
  - gives each device a session-local index (0–254) that it keeps while
    the device stays present; at most 32 devices, names cut to 48 bytes
    at a UTF-8 boundary;
  - sends the full list when the board is opened or asks for it, then
    only what changed;
  - refuses a selection that arrives before this session's list was sent
    (its index may be from an earlier session).
- **Threads**: Core Audio callbacks and the HID reader only push events;
  the main thread runs the controller.

## Code layout

```text
host/
├── Audio/              IAudioSystem, DeviceRegistry, AudioMapping (portable)
├── App/                HostController, EventQueue, FrameText (portable)
├── Hid/                IFrameSink, HidMatch (portable)
├── Platform/Windows/   WinAudioSystem, WinHidLink, HostService, main.cpp, tray.cpp
└── Tests/              test_host.cpp (Linux and Windows)
```

The portable part builds and is tested on Linux too. Details and manual
checks: [host/README.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/host/README.md).
