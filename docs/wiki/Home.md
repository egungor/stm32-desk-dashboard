# STM32 Desk Dashboard

A Windows desk dashboard on the 4.3" touchscreen of an **STM32F746G-DISCO**
board. It grows out of
[stm32-audio-controller](https://github.com/egungor/stm32-audio-controller):
today the board still is that audio controller (tap a device on the LCD and
Windows switches to it); the dashboard tab (clock, weather, menu of the day,
media keys, Claude Code approvals) is being built, see the
[roadmap](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/roadmap.md).

![Main screen on the LCD](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-main-screen.png)

```text
 STM32F746G-DISCO (touchscreen)  ──USB HID──►  Windows PC (dashboard-tray)
   LVGL UI, protocol client      ◄──────────   Core Audio, default device
```

**Download:** the Windows installer and the firmware (`.elf`, `.hex`,
`.bin`) will be on the [Releases page](https://github.com/egungor/stm32-desk-dashboard/releases);
there is no release of the dashboard yet. How to install and
flash: [Getting Started](Getting-Started#quick-start-from-a-release).

## Where the project stands

| Part | State |
|---|---|
| Board bring-up: boot, log, clock, SDRAM, LCD | VERIFIED on the board |
| Touch, UI on the LCD, USB HID, display flip | VERIFIED on the board |
| Windows host: audio, default switching, HID | VERIFIED on Windows |
| Firmware ↔ host together | Simulated in CI; end to end VERIFIED |
| Volume/mute on the touchscreen (M6) | VERIFIED on the board and Windows |
| Polish (M7): layouts, themes, settings, edit mode, aliases | Coded; layout A, themes, accent colours, favorites tab, mute/volume VERIFIED on the board; the rest REQUIRES HARDWARE |
| Volume limit on switch | VERIFIED on the board and Windows (2026-10-16) |
| Tray program, start with Windows, installer | Built in CI; installed and used on Windows; full check REQUIRES WINDOWS |

The M1–M6 board and Windows checks ran on 2026-10-14, the M7 checks are
in progress; see
[Hardware Test Checklist](Hardware-Test-Checklist) and the bring-up log
in `docs/hardware.md`.

## Pages

- [User Guide](User-Guide) — screens, settings, favorites and aliases, the tray program
- [Getting Started](Getting-Started) — install from a release, or clone, build, flash, run
- [Architecture](Architecture) — the three parts and how they talk
- [Hardware](Hardware) — board, peripherals, pins
- [Firmware](Firmware) — STM32 code, UI, build options, log lines
- [Host Application](Host-Application) — tray program, `dashboard-host` and its commands
- [Protocol](Protocol) — the 64-byte frame and the message exchanges
- [Testing](Testing) — CI, local tests, the loopback integration test
- [Hardware Test Checklist](Hardware-Test-Checklist) — what to try on the board and on Windows
- [Development Workflow](Development-Workflow) — branches, commits, PRs, Claude Code
- [Troubleshooting](Troubleshooting)

## Source of truth

This wiki is an overview. The repository documents are authoritative and
are updated with the code:
[README](https://github.com/egungor/stm32-desk-dashboard/blob/main/README.md) ·
[roadmap](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/roadmap.md) ·
[architecture](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/architecture.md) ·
[hardware](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/hardware.md) ·
[protocol](https://github.com/egungor/stm32-desk-dashboard/blob/main/protocol/protocol.md) ·
[toolchain](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/toolchain.md).
