# System Architecture

## Goal

Provide a physical touchscreen controller for selecting the default Windows
audio playback endpoint.

## High-level architecture

```text
           STM32F746G-DISCO                               Windows PC
┌──────────────────────────────────┐         ┌──────────────────────────────────┐
│ firmware/ui (LVGL)               │         │ Platform/Windows/WinAudioSystem  │
│   screens, touch → select        │         │   Core Audio, IPolicyConfig      │
│            ▲ │                   │         │            ▲ │                   │
│            │ ▼                   │         │            │ ▼                   │
│ firmware/link (link_client.c)    │         │ App/HostController               │
│   protocol v3, STM32 side        │         │   protocol v3, DeviceRegistry    │
│            ▲ │                   │         │            ▲ │                   │
│            │ ▼                   │         │            │ ▼                   │
│ App/link_port.c, App/usb_link.c  │◄───────►│ Platform/Windows/WinHidLink      │
│   vendor HID 1209:0002           │ USB HID │   SetupDi, overlapped I/O        │
└──────────────────────────────────┘         └──────────────────────────────────┘
                     protocol/ (C codec, shared by both sides)
```

The protocol (`protocol/protocol.md`) is the only contract between the
two sides. Both link the same C codec (`protocol/src/protocol.c`).
`tests/integration` runs `link_client.c` against `HostController` over a
simulated USB link.

## Component boundaries

### Firmware

Responsible for:

- LCD/touch initialization
- rendering the UI
- handling touch events
- maintaining a display model
- sending user actions to the host
- receiving device information/state from the host

Not responsible for:

- enumerating Windows audio devices
- knowing Windows device IDs
- changing Windows audio policy directly

### Windows host

Responsible for:

- detecting the STM32 HID device
- enumerating active Windows render endpoints
- identifying the current default endpoint
- changing the default endpoint
- monitoring endpoint/default-device changes
- sending a device model to the STM32

### Protocol

Responsible for:

- framing
- message types
- payload formats
- protocol version
- request/response semantics
- error reporting

## Modules

### Firmware (`firmware/`)

| Module | Hardware dependent | Role |
|---|---|---|
| `ui/` | no (LVGL only) | Rail and tabs (`ui_shell.c`); Dashboard tab: clock, 2×2 cards, generic detail, media strip (`ui_dash.c`); Audio tab: device buttons, bottom strip (mute, volume slider, page arrows); waiting screen, settings, edit mode, notices |
| `link/` | no (C, protocol codec) | `link_client.c`: protocol v3 audio commands, state machine: request the list, assemble it, apply `DEVICE_CHANGED`, send `SELECT_DEVICE`, volume and mute (rate limited), timeouts. `dash_client.c`: dashboard records (cards, detail, prompt, `TIME`, actions; ADR-006, ADR-007) |
| `App/usb_link.c` | yes | Vendor HID class (ADR-003); the ISR only queues received frames |
| `App/link_port.c` | yes | Connects USB, `link/` and `ui/` in the UI task; decodes each frame once and gives it to the audio or the dashboard client |
| `App/ui_port.c` | yes | LVGL on the LTDC (two SDRAM framebuffers, swap on VBLANK) and the FT5336 |
| `App/` others | yes | SDRAM, clock check, touch driver, log, M1 test screen |
| `simulator/` | no | Headless Linux build of `ui/` that writes PNG screenshots |

Everything LVGL and protocol related runs in one FreeRTOS task (the
default task); the USB interrupt only copies frames into a static queue.

### Host (`host/`)

| Module | Windows dependent | Role |
|---|---|---|
| `Audio/DeviceRegistry` | no | Endpoints → session-local `device_index`, at most 32, names truncated to 48 bytes |
| `App/HostController` | no | Protocol v3 audio commands, host side |
| `App/DashController` | no | Protocol v3 dashboard records: card slots, details, actions, `TIME` (ADR-006) |
| `App/FrameRouter` | no | Decodes each frame once; `0x20`–`0x3F` to `DashController`, the rest to `HostController` |
| `App/MediaKeys` | no | Media strip `ACTION` (kind 2) → previous / play-pause / next behind `IMediaKeys` |
| `App/HttpClient.h`, `App/BackgroundFetch` | no | `IHttpClient` (GET, POST); one GET at a time on a worker thread, result polled by the loop |
| `Menu/MenuData`, `MenuImport` | no | Month files `menu-YYYY-MM.json`, merge by date; menu images → Claude API request and answer (nlohmann/json) |
| `Hook/ApprovalService` | no | Claude Code requests: queue, prompt record (cut rule), timeouts, `ACTION` kind 1, card slot 3 (ADR-007) |
| `Hook/ClaudeHook` | no | Claude Code `PermissionRequest` decision JSON; the step 10 probe's config and log lines (ADR-007) |
| `Menu/MenuService` | no | Menu card (slot 1) and day detail, from the month files |
| `Weather/WeatherReport`, `WeatherLocation` | no | Open-Meteo forecast and geocoding (nlohmann/json), WMO code → icon number and text, `weather.txt` |
| `Weather/WeatherCard`, `WeatherService` | no | Weather card (slot 0) and detail: when to fetch, the records, the location file |
| `App/SequencedSink` | no | Sets one sequence counter (and the CRC) on every frame the host sends |
| `App/EventQueue` | no | Serialises Core Audio and HID events into one consumer thread |
| `Hid/HidMatch.h` | no | Which HID interface is the controller |
| `Platform/Windows/WinAudioSystem` | yes | `IAudioSystem` on Core Audio; default switching through `IPolicyConfig` (ADR-002) |
| `Platform/Windows/WinMediaKeys` | yes | `IMediaKeys`: one key down and up through `SendInput` (`VK_MEDIA_PREV_TRACK`, `VK_MEDIA_PLAY_PAUSE`, `VK_MEDIA_NEXT_TRACK`) |
| `Platform/Windows/WinHookPipe` | yes | Named pipe server for `dashboard-host hook permission`, current user only; requests into `EventQueue` |
| `Platform/Windows/WinHttpClient` | yes | `IHttpClient` on WinHTTP (HTTPS, system proxy) |
| `Platform/Windows/WinMenuImport` | yes | Menu import from image files (console and tray); month files in `%APPDATA%` |
| `Platform/Windows/WinHidLink` | yes | Finds and opens the controller, reader thread, writes frames |
| `Platform/Windows/main.cpp` | yes | Console program `dashboard-host` |

Core Audio callbacks and the HID reader thread only push events; the
main thread pops them and passes frames to `FrameRouter`, Core Audio
changes to `HostController`, and ticks the `DashController` (every
500 ms) for the time.

## UI

The main screen has a 56 px rail on the left (Dashboard and Audio tabs,
settings, connection mark) and the shown tab beside it, 424 × 272
(ADR-005, `firmware/ui/ui_shell.c`). The Dashboard tab (ADR-006, `ui_dash.c`): a clock strip,
four cards whose text comes from the host, a detail view in the tab after
a tap, and the media strip `<< || >>`. The Audio tab, layout A:

Four large device buttons per page (2×2), each with an icon and the
device name; the active device is marked by colour and text, a device
waiting for the host's confirmation by a pending mark. Arrows switch pages
when there are more than four devices. A strip at the bottom holds the
mute button, the volume slider with its percentage and, on the right, the
page arrows. Without a host the UI shows a waiting screen. UI text is
Turkish.

Screenshots: run `tools/run-ui-simulator.sh`, or download the
`ui-screenshots` artifact of the UI Simulator CI job. Photos from the LCD
go to `docs/images/` once the UI has been checked on the board.

## Important design decisions

- STM32 ↔ Windows: vendor USB HID, 64-byte reports, VID:PID `1209:0002`
  (ADR-003); protocol v3 with 64-byte frames and CRC-16
  (`protocol/protocol.md`)
- Host language: C++17
- GUI: LVGL v9.5.0 (git submodule). The UI lives in `firmware/ui/`, depends
  only on LVGL and is previewed with the Linux simulator
  (`firmware/simulator/`); on the board it runs on two SDRAM framebuffers
  with the FT5336 as input (`App/Src/ui_port.c`)
- Audio control: Windows Core Audio/MMDevice; switching the default device
  uses the undocumented `IPolicyConfig` (ADR-002)
- Device information is discovered dynamically by Windows
- User-friendly display names are separated from Windows endpoint IDs

## Future extensions

- Favourites, custom ordering and names
- Start with Windows, tray icon
- Device groups
