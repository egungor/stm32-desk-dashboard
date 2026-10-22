# Architecture

```text
           STM32F746G-DISCO                               Windows PC
┌──────────────────────────────────┐         ┌──────────────────────────────────┐
│ firmware/ui (LVGL)               │         │ Platform/Windows/WinAudioSystem  │
│   screens, touch → select        │         │   Core Audio, IPolicyConfig      │
│            ▲ │                   │         │            ▲ │                   │
│            │ ▼                   │         │            │ ▼                   │
│ firmware/link (link_client.c)    │         │ App/HostController               │
│   protocol v1, STM32 side        │         │   protocol v1, DeviceRegistry    │
│            ▲ │                   │         │            ▲ │                   │
│            │ ▼                   │         │            │ ▼                   │
│ App/link_port.c, App/usb_link.c  │◄───────►│ Platform/Windows/WinHidLink      │
│   vendor HID 1209:0002           │ USB HID │   SetupDi, overlapped I/O        │
└──────────────────────────────────┘         └──────────────────────────────────┘
                     protocol/ (C codec, shared by both sides)
```

## Responsibilities

| | STM32 | Windows host |
|---|---|---|
| Knows | what to show, what the user tapped | the playback devices, the default, volume |
| Does | draws the list, sends the selection | switches the default, reports changes |
| Never | knows Windows device IDs or Windows APIs | depends on how the STM32 draws |

The STM32 shows **only** what the host reports. A tapped device is marked
"pending" and becomes active when the host confirms with
`DEVICE_CHANGED`.

## Rules that shape the code

- The [Protocol](Protocol) is the only contract; changing it needs
  documentation and tests on both sides.
- Hardware-dependent code is kept apart from logic that can be tested on a
  PC: `firmware/ui` and `firmware/link` build on Linux, and
  `host/Audio`, `host/App` and `host/Hid` do not include Windows headers.
- Firmware: fixed-size memory, no dynamic allocation in the USB path, short
  interrupt handlers. LVGL and the protocol client run in one FreeRTOS
  task; the USB interrupt only queues frames.
- Host: Core Audio callbacks and the HID reader thread only push events;
  one thread runs `HostController`.
- Generated STM32CubeMX code is changed only inside `USER CODE` sections.

## Decisions (ADRs)

| ADR | Decision |
|---|---|
| [ADR-001](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-001-firmware-peripheral-set.md) | Peripheral set enabled in CubeMX |
| [ADR-002](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-002-default-device-switching.md) | Switch the Windows default through the undocumented `IPolicyConfig` |
| [ADR-003](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-003-usb-hid-interface.md) | Vendor USB HID, `1209:0002`, 64-byte reports, registered from USER CODE |
| [ADR-008](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-008-media-session-and-music-tab.md) | Now playing from the Windows media session, a 120 × 120 cover, the Music tab; protocol v4 |
| [ADR-009](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-009-notifications.md) | Windows notifications: the fourth card, its list and a banner; protocol v5 |
| [ADR-010](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-010-dashboard-redesign.md) | Dashboard redesign: coloured cards, status icons, now-playing and picture cards; protocol v6 |
| [ADR-011](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-011-settings-window.md) | Settings window (WebView2), firmware version in the USB string, settings sent on change |
| [ADR-012](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-012-picture-library.md) | Picture library: photos in the settings folder, crop with zoom, shuffle |

More: [docs/architecture.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/architecture.md).
