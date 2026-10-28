# Architecture

Three parts: the **firmware** on the STM32F746G-DISCO, the **host**
programs on Windows, and the **protocol** between them — the only
contract, one C codec linked by both sides.

```text
           STM32F746G-DISCO                                     Windows PC
┌────────────────────────────────────┐          ┌────────────────────────────────────────┐
│ firmware/ui (LVGL 9.5)             │          │ sources behind small interfaces:       │
│   rail · Dashboard · Music · Audio │          │   WinAudioSystem   Core Audio          │
│   settings · edit · keyboard       │          │   WinMediaSession  media session       │
│   prompt · banner (over all)       │          │   WinNotificationListener              │
│             ▲ │                    │          │   WeatherService   Open-Meteo          │
│             │ ▼                    │          │   MenuService      menu files          │
│ firmware/link                      │          │   PictureService   picture library     │
│   link_client.c  audio, settings   │          │   WinHookPipe      Claude Code hook    │
│   dash_client.c  records, media,   │          │               ▲ │                      │
│                  picture           │          │               │ ▼                      │
│             ▲ │                    │          │ HostService loop: HostController,      │
│             │ ▼                    │          │   DashController, ApprovalService …    │
│ App/usb_link.c  vendor HID         │◄────────►│ Platform/Windows/WinHidLink            │
│   1209:0002, 64-byte reports       │ USB HID  │   SetupDi, overlapped I/O              │
└────────────────────────────────────┘          └────────────────────────────────────────┘
                         protocol/ (v7, C codec shared by both sides)
```

## Thin firmware, content from the host

The board knows a few **generic components** and nothing about what
they show:

| Component | Record | Used for |
|---|---|---|
| Card | icon, title, badge, background `RRGGBB`, flags, frame, icon colour, up to four lines | weather (slot 0), menu (slot 1) |
| Detail | header (title, subtitle, ‹ ›, bin), rows with icon / label / value / colour, section titles, a band, tiles, a sideways strip, up to three buttons | weather, menu, notifications (slot 2), Claude Code (slot 3) |
| Prompt | a detail over every screen with a countdown and buttons, answered once | Claude Code requests, the morning menu warning |
| Banner | icon, title, seconds, the slot a tap opens, two lines | new notifications |
| Status icons | icon, colour, number, the slot a tap opens | the bell, the Claude Code symbol |
| Media | texts, state, 120 × 120 cover | the Music tab and the now-playing card |
| Picture | 480 × 272 RGB565 | the picture card and the full-screen picture |

The weather, the menu, notifications and approvals are written entirely
on the host (`host/Weather`, `host/Menu`, `host/Notify`, `host/Hook`).
A new kind of card needs no firmware change. Records are UTF-8 text sent
in 53-byte parts ([Protocol](Protocol)).

## Responsibilities

| | STM32 | Windows host |
|---|---|---|
| Knows | what to show, what the user tapped | devices, the default, volume, the media session, notifications, weather, menu, pictures, Claude Code requests |
| Does | draws records, sends taps (`SELECT_DEVICE`, `OPEN_DETAIL`, `ACTION`, `SEEK`, settings) | switches the default, answers hooks, writes cards and details, stores preferences |
| Never | knows Windows device IDs or any Windows API | depends on how the STM32 draws |

The STM32 shows **only** what the host reports. A tapped device is
marked "pending" and becomes active when the host confirms with
`DEVICE_CHANGED`; an edit shows when the new list arrives.

## Rules that shape the code

- The [Protocol](Protocol) is the only contract; changing it needs
  documentation and tests on both sides, and a version bump when it is
  not compatible.
- Hardware-dependent code is kept apart from logic that can be tested on
  a PC: `firmware/ui` and `firmware/link` build on Linux; `host/App`,
  `Audio`, `Hid`, `Weather`, `Menu`, `Media`, `Notify`, `Picture` and
  `Hook` include no Windows headers.
- Firmware: fixed-size memory, no dynamic allocation in the USB path,
  short interrupt handlers. LVGL and the protocol clients run in one
  FreeRTOS task; the USB interrupt only queues frames. Framebuffers,
  the cover and the picture live in SDRAM.
- Host: Core Audio callbacks, the HID reader, the media and notification
  readers and the pipe server only push events; one service loop runs
  the controllers. Slow work (weather, menu import, pictures) runs on
  worker threads.
- Generated STM32CubeMX code is changed only inside `USER CODE`
  sections.

## Decisions (ADRs)

| ADR | Decision |
|---|---|
| [ADR-001](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-001-firmware-peripheral-set.md) | Peripheral set enabled in CubeMX |
| [ADR-002](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-002-default-device-switching.md) | Switch the Windows default through the undocumented `IPolicyConfig` |
| [ADR-003](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-003-usb-hid-interface.md) | Vendor USB HID, `1209:0002`, 64-byte reports, registered from USER CODE |
| [ADR-004](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-004-device-preferences-and-settings.md) | Aliases, favourites, order and the settings record, stored on the host; protocol v2 |
| [ADR-005](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-005-tabbed-shell-and-global-settings.md) | Tabbed shell with a rail; global settings in sections |
| [ADR-006](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-006-cards-and-protocol-v3.md) | Cards, details and prompts as chunked text records; protocol v3 |
| [ADR-007](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-007-claude-code-approval-bridge.md) | Claude Code approvals: `PermissionRequest` hook, named pipe, prompt on the board |
| [ADR-008](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-008-media-session-and-music-tab.md) | Now playing from the Windows media session, a 120 × 120 cover, the Music tab, seek; protocols v4 and v7 |
| [ADR-009](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-009-notifications.md) | Windows notifications: the list, the banner, clearing; protocol v5 |
| [ADR-010](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-010-dashboard-redesign.md) | Dashboard redesign: coloured cards, status icons, now-playing and picture cards, food lists; protocols v6 and v7 |
| [ADR-011](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-011-settings-window.md) | Settings window (WebView2), firmware version in the USB string, settings sent on change |
| [ADR-012](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-012-picture-library.md) | Picture library: photos in the settings folder, crop with zoom, shuffle |

More: [docs/architecture.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/architecture.md).
