# STM32 Desk Dashboard

A Windows desk dashboard on the 4.3" touchscreen of an
**STM32F746G-DISCO** board: the time, the weather, today's lunch menu,
what is playing, notifications and Claude Code approvals on one screen,
and the Windows audio output in one tap. It grows out of
[stm32-audio-controller](https://github.com/egungor/stm32-audio-controller),
whose screens live on as the **Audio** tab.

![The Music tab, the Dashboard and the Audio tab](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/hero.png)

**Download:** the Windows installer and the firmware are on the
[Releases page](https://github.com/egungor/stm32-desk-dashboard/releases).
The latest are test pre-releases of Dashboard v1 (`v0.9.0-beta.*`);
there is no v1.0.0 yet. Install and flash in three steps:
[Getting Started](Getting-Started#quick-start-from-a-release).

## A quick tour

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png" alt="Dashboard" width="100%"><br><strong><a href="Dashboard">Dashboard</a></strong> — clock, weather, menu, now playing, picture, status icons, media strip</td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-playing.png" alt="Music tab" width="100%"><br><strong><a href="Music-Tab">Music tab</a></strong> — what plays on the PC, with seek and volume</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-cards.png" alt="Audio tab" width="100%"><br><strong><a href="Audio-Tab">Audio tab</a></strong> — the default playback device in one tap</td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-top.png" alt="Settings on the board" width="100%"><br><strong><a href="Board-Settings">Board settings</a></strong> — theme, colour, language, screen</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-prompt.png" alt="Claude Code request" width="100%"><br><strong><a href="Claude-Code-Approvals">Claude Code approvals</a></strong> — answer permission requests on the board</td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-banner.png" alt="Notification banner" width="100%"><br><strong><a href="Notifications">Notifications</a></strong> — a banner for each new toast, a list behind the bell</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning.png" alt="Menu warning" width="100%"><br><strong><a href="Menu-of-the-Day">Menu of the day</a></strong> — read from the canteen's images, coloured by your food lists</td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-dashboard-light.png" alt="Settings window" width="100%"><br><strong><a href="Settings-Window">Settings window</a></strong> — every setting on the PC</td></tr>
</table>

Every screen, with what each control does:
[Screen Gallery](Screen-Gallery).

## How it fits together

```text
 STM32F746G-DISCO (touchscreen)                 Windows PC (dashboard-tray)
 ┌───────────────────────────┐                  ┌─────────────────────────────────┐
 │ LVGL UI: rail, Dashboard, │                  │ Core Audio · media session      │
 │ Music, Audio, settings,   │   USB HID        │ notifications · Open-Meteo      │
 │ prompt and banner         │◄────────────────►│ menu files · picture library    │
 │ link + dash client        │ 64-byte frames,  │ Claude Code hook (named pipe)   │
 │ (draws what it is sent)   │ protocol v7      │ host service → cards, details   │
 └───────────────────────────┘                  └─────────────────────────────────┘
```

The board draws a few generic components (card, detail list, prompt,
banner, media strip); every text, colour and icon comes from the PC.
More: [Architecture](Architecture).

## Where the project stands

| Part | State |
|---|---|
| Board bring-up: clock, SDRAM, LCD, touch, USB HID, display flip | VERIFIED on the board |
| Audio tab: switching, volume, mute, Windows changes, unplug and replug | VERIFIED on the board and Windows |
| Audio tab polish (M7): layouts, themes, favourites, aliases, settings page | Layout A, themes, accent colours, favourites tab, mute and volume VERIFIED on the board; the rest REQUIRES HARDWARE |
| Volume limit on switch | VERIFIED on the board and Windows (2026-10-16) |
| Music tab and the Dashboard's media strip | VERIFIED with Deezer (2026-10-22); other players REQUIRES WINDOWS + HARDWARE |
| Dashboard: clock, weather, menu and food lists, now-playing card, picture card and library | Unit tests and simulator in CI; Windows and board checks open |
| Notifications | Unit tests and simulator in CI; Windows and board checks open |
| Claude Code approvals | Unit and integration tests in CI; hook behaviour REQUIRES WINDOWS (spike #24); on the board REQUIRES HARDWARE |
| Settings window | Built in CI (MSVC); REQUIRES WINDOWS |
| Tray program, start with Windows, installer | Built in CI; installed and used on Windows; the full check REQUIRES WINDOWS |
| Protocol v7, firmware ↔ host together | Codec and loopback integration tests in CI (Linux, Windows) |

The checks still to run are in the
[Hardware Test Checklist](Hardware-Test-Checklist) and, row by row, in
`docs/hardware.md`.

## Pages

**Using it**

- [Getting Started](Getting-Started) — install from a release, or clone, build, flash, run
- [User Guide](User-Guide) — the rail, the tabs, messages and tips
- [Dashboard](Dashboard) · [Menu of the Day](Menu-of-the-Day) · [Notifications](Notifications) · [Claude Code Approvals](Claude-Code-Approvals)
- [Music Tab](Music-Tab) · [Audio Tab](Audio-Tab) · [Board Settings](Board-Settings)
- [Settings Window](Settings-Window) — the tray program and its window on the PC
- [Screen Gallery](Screen-Gallery) — every screen, and how the pictures are made
- [Troubleshooting](Troubleshooting)

**Inside**

- [Architecture](Architecture) — the three parts and how they talk
- [Hardware](Hardware) — board, peripherals, pins
- [Firmware](Firmware) — STM32 code, UI modules, build options, log lines
- [Host Application](Host-Application) — tray program, `dashboard-host` and its commands, the files it keeps
- [Protocol](Protocol) — the 64-byte frame and the message exchanges
- [Testing](Testing) — CI, local tests, the loopback integration test, the simulator
- [Hardware Test Checklist](Hardware-Test-Checklist) — what to try on the board and on Windows
- [Development Workflow](Development-Workflow) — branches, commits, PRs, Claude Code

## Source of truth

This wiki is an overview. The repository documents are authoritative and
are updated with the code:
[README](https://github.com/egungor/stm32-desk-dashboard/blob/main/README.md) ·
[roadmap](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/roadmap.md) ·
[architecture](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/architecture.md) ·
[hardware](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/hardware.md) ·
[protocol](https://github.com/egungor/stm32-desk-dashboard/blob/main/protocol/protocol.md) ·
[host](https://github.com/egungor/stm32-desk-dashboard/blob/main/host/README.md) ·
[toolchain](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/toolchain.md).
