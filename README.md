# STM32 Desk Dashboard

A Windows desk dashboard on the 4.3" touchscreen of an STM32F746G-DISCO
board: a dashboard tab (clock, weather, menu of the day, media keys,
Claude Code approvals) next to an audio tab that switches the default
Windows playback device.

This repository continues
[stm32-audio-controller](https://github.com/egungor/stm32-audio-controller)
(its history is kept here); its audio features work as there, on the
Audio tab. Dashboard v1 is coded (steps 1–11) and in testing; v1.0.0
follows after further requests, see [`docs/roadmap.md`](docs/roadmap.md).

The project consists of:

- **STM32F7 firmware** — touchscreen UI and USB HID communication
- **Windows host application** — tray program and console tools: Windows
  audio endpoints and the default playback device, the weather, the menu
  of the day (read from images with the Claude API), media keys, Claude
  Code permission requests answered on the board
- **Shared protocol** — the contract between STM32 and Windows
- **Tests and CI** — automated validation for host/protocol code

## Download

Releases are on the
**[Releases page](https://github.com/egungor/stm32-desk-dashboard/releases)**.
The latest are test pre-releases of Dashboard v1 (`v0.9.0-beta.*`):
clock, weather, menu of the day, now playing, notifications, the picture,
Claude Code approvals and the settings window (`v0.9.0-beta.1` has its
checklist in issue #22). There is no v1.0.0 yet.

| File | What |
|---|---|
| `stm32-desk-dashboard-setup-<version>.exe` | Windows installer: tray program, `dashboard-host` console tools, start with Windows; current user, no administrator rights ([`installer/README.md`](installer/README.md)) |
| `stm32-desk-dashboard-firmware-<version>.elf` / `.hex` / `.bin` | Firmware for the STM32F746G-DISCO (Release build; the `.bin` starts at `0x08000000`) |

Flash the firmware through the board's ST-LINK USB connector with
STM32CubeProgrammer:

```text
STM32_Programmer_CLI -c port=SWD -w stm32-desk-dashboard-firmware-<version>.elf -v -rst
```

Then connect the board's USB OTG FS connector to the PC and run the
installed tray program. Until the setup is signed (see
[Code signing policy](#code-signing-policy)), Windows SmartScreen may warn
("More info" → "Run anyway"); a newly signed program may also warn for a
while, until it has a reputation. A `v*` tag builds both
files in CI and attaches them to the release
([`installer/README.md`](installer/README.md), "Where to get it").

The board uses USB VID:PID `1209:0002`; the audio controller uses
`1209:0001`, so the two products and their host programs do not take each
other's board.

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/),
certificate by [SignPath Foundation](https://signpath.org/).

What is signed: the Windows installer `stm32-desk-dashboard-setup-<version>.exe`
of a release, built in GitHub Actions from this repository
(`.github/workflows/host-installer.yml`). Every signing request is
approved by hand. The firmware files are not signed (the board does not
check signatures).

Team roles:

| Role | Members |
|---|---|
| Authors (commit without further review) | [egungor](https://github.com/egungor) |
| Reviewers (review changes from others) | [egungor](https://github.com/egungor) |
| Approvers (approve each signing request) | [egungor](https://github.com/egungor) |

Changes are also written with the help of Claude Code (an AI coding
assistant); each one goes through a pull request that the owner merges.

### Privacy

The programs send no information to other networked systems unless the
user asks for it:

- **Weather**: once you set a place (`dashboard-host weather <place>` or
  the settings window), the place name is sent to the Open-Meteo
  geocoding service and its coordinates to the Open-Meteo forecast
  service ([Open-Meteo terms and privacy](https://open-meteo.com/en/terms)).
  Without a place nothing is sent; deleting
  `%APPDATA%\stm32-desk-dashboard\weather.txt` stops the requests.
- **Menu import**: only when you import menu images with your own
  Anthropic API key, the images are sent to the Claude API
  ([Anthropic privacy policy](https://www.anthropic.com/legal/privacy)).
- Everything else stays on the PC and the board: audio devices, now
  playing, Windows notifications (their texts go to the board over USB
  and are not written to `host.log`), Claude Code requests (a local named
  pipe), pictures. There is no telemetry and no update check.
- The settings window uses the Microsoft Edge WebView2 Runtime, a part of
  Windows ([Microsoft privacy statement](https://privacy.microsoft.com/privacystatement)).

The installer shows this statement before it installs.

## License

[MIT](LICENSE), for the code of this project. Third-party components keep
their own licences:

| Component | Where | Licence |
|---|---|---|
| nlohmann/json | host (`host/third_party/nlohmann`) | MIT |
| Microsoft WebView2 SDK (headers, static loader) | host, settings window (fetched by CMake) | BSD-3-Clause |
| LVGL | firmware (`firmware/ui`, submodule) | MIT |
| FreeRTOS | firmware | MIT |
| STM32Cube HAL, BSP | firmware | BSD-3-Clause |
| CMSIS | firmware | Apache-2.0 |
| STM32 USB Device Library | firmware | ST SLA0044 |

The installer carries the notices of the host's components
([`installer/THIRD-PARTY-NOTICES.txt`](installer/THIRD-PARTY-NOTICES.txt)).

## Project status (inherited from the audio controller)

M0–M6 are done: firmware, host and protocol are tested in CI, and the
board and Windows checks ran on 2026-10-14 (bring-up log in
[`docs/hardware.md`](docs/hardware.md)). M7 (polish) is coded and in
board testing: aliases, ordering, favorites, three layouts with themes,
a settings page, screen timeout, volume limit on switch (protocol
version 2, ADR-004), tray program and installer. Milestones:
[`docs/roadmap.md`](docs/roadmap.md). Product presentation (Turkish):
[`docs/presentation/stm32-ses-kumandasi.pdf`](docs/presentation/stm32-ses-kumandasi.pdf)
(source and image list: [`docs/images/README.md`](docs/images/README.md)).

![Main screen on the LCD: layout A, dark theme](docs/images/ui-main-screen.png)

<p>
  <img src="docs/images/ui-layout-list.png" width="32%" alt="Layout B: list with side rail and vertical volume slider">
  <img src="docs/images/ui-layout-tiles.png" width="32%" alt="Layout C: large tile for the active device">
  <img src="docs/images/ui-settings.png" width="32%" alt="Settings page">
</p>

LCD screenshots from the board (framebuffer read over SWD), firmware and
host v0.7.0-beta.2: layout A, layouts B and C, the settings page. More:
[`docs/images/README.md`](docs/images/README.md).

| Part | State |
|---|---|
| Board bring-up: boot, ST-LINK VCP log, clock, SDRAM (also from a power cycle), LCD | VERIFIED on the board |
| FT5336 touch | VERIFIED on the board |
| LVGL UI (device buttons, pages, waiting screen, notices) | Simulator in CI; VERIFIED on the board |
| USB HID interface and STM32 protocol client | Unit tests in CI; VERIFIED on the board and Windows as `1209:0001`; as `1209:0002` REQUIRES HARDWARE |
| Protocol v2 (64-byte frames, CRC-16; preferences and settings, ADR-004) | Codec tests in CI (Linux, Windows); VERIFIED on the board and Windows (#58) |
| Protocol v3 (dashboard records, ADR-006; #8) | Codec tests in CI; used by the dashboard; on the board REQUIRES HARDWARE |
| Tabbed shell, Dashboard tab: clock, 2×2 cards, detail view, media strip (#10, #14, #16) | Simulator in CI; integration test in CI; on the board REQUIRES HARDWARE (#22) |
| Weather (Open-Meteo) and menu of the day (Claude API) (#18, #20) | Unit tests in CI; on Windows REQUIRES WINDOWS (#22) |
| Claude Code approvals: hook command, named pipe, prompt on the board (#26, ADR-007) | Unit and integration tests in CI; hook behaviour on Windows REQUIRES WINDOWS (spike #24); on the board REQUIRES HARDWARE |
| Windows host: Core Audio, default device switching, HID transport | Unit tests in CI; VERIFIED on Windows |
| Firmware ↔ host together | Loopback integration test in CI; end to end VERIFIED; adding or removing a Windows device while connected NOT TESTED |
| Volume and mute on the touchscreen (M6) | VERIFIED on the board and Windows |
| Display flip (180°) | VERIFIED on the board (#42, #51) |
| Polish (M7): layouts, themes, settings, edit mode, aliases (#57–#60) | Simulator in CI; layout A, themes, accent colours, favorites tab, mute/volume VERIFIED on the board; the rest REQUIRES HARDWARE ([`docs/hardware.md`](docs/hardware.md)) |
| Volume limit on switch (#59, #83) | Unit tests in CI; VERIFIED on the board and Windows |
| Tray program, start with Windows, installer (#74) | Built in CI; installed and used on Windows; the full tray check REQUIRES WINDOWS |

## Architecture

```text
┌───────────── STM32F746G-DISCO ─────────────┐
│ firmware/ui        LVGL screens            │
│      ▲ │                                   │
│      │ ▼                                   │
│ firmware/link      protocol client         │
│      ▲ │                                   │
│      │ ▼                                   │
│ App/usb_link.c     vendor HID 1209:0002    │
└──────────────────────┬─────────────────────┘
                       │ USB, 64-byte reports (protocol/protocol.md)
┌──────────────────────▼─────────────────────┐
│ WinHidLink         HID discovery and I/O   │
│      ▲ │                                   │
│      │ ▼                                   │
│ HostController     protocol, device list   │
│      ▲ │                                   │
│      │ ▼                                   │
│ WinAudioSystem     Core Audio              │
└─────────────── Windows host ───────────────┘
```

Details: [`docs/architecture.md`](docs/architecture.md).

## Features

Done in code (verification status above):

- tabs on a left rail: Dashboard and Audio, global settings in sections,
  the tab shown at start chosen in the settings;
- Dashboard (ADR-010): clock and date from the PC; coloured cards for the
  weather (coloured by the weather), the menu of the day (green, or
  yellow / orange when a dish of your food lists is on it, with a warning
  at 08:00), what plays now (with a small cover) and a picture you choose
  in the tray menu; the notifications bell and the Claude Code icon at the
  top right; a detail view per card and icon; media keys `<< || >>` for
  the player on the PC;
- weather for one location (Open-Meteo, no key): card and 10-day detail;
- menu of the day read from menu images with the Claude API, card and
  day detail with the salad bar;
- Windows notifications: the card counts the toasts in the notification
  centre, its list shows them, and a new one shows a short banner at the
  top of every screen (none while Do not disturb is on);
- Claude Code permission requests on this PC shown over every screen:
  Onayla / Reddet / Terminalde sor, with a countdown; nothing is allowed
  by default;

Audio tab (from the audio controller):

- device list from Windows on the touchscreen, in pages, with icons;
- select a device on the touchscreen → Windows default playback device;
- Windows changes (default, devices added or removed) shown on the board;
- USB unplug/replug and host program restart handled on both sides;
- volume slider and mute button on the touchscreen, following Windows;
  moving the slider while muted unmutes;
- friendly names (aliases), favourites and custom ordering, edited on the
  touchscreen (M7);
- three layouts (cards, list, large tile), dark and light themes, six
  accent colours, a settings page, screen timeout, display flip, Turkish
  and English (M7);
- optional volume limit: an output selected on the board is turned down
  to the limit first (M7);
- tray program with start with Windows, and a Windows installer (M7).

## Repository layout

```text
stm32-desk-dashboard/
├── firmware/
│   ├── STM32AudioController/   # STM32CubeMX project (CMake)
│   │   └── App/                # hand-written firmware code
│   ├── ui/                     # LVGL UI, hardware independent
│   ├── link/                   # STM32 protocol client + tests
│   └── simulator/              # headless Linux build of the UI
├── host/
│   ├── Audio/ App/ Hid/        # platform-independent core
│   ├── Platform/Windows/       # Core Audio, HID, console program
│   └── Tests/
├── installer/                  # Inno Setup script of the Windows installer
├── protocol/                   # protocol.md, C codec, tests
├── tests/integration/          # firmware ↔ host loopback test
├── docs/                       # architecture, hardware, roadmap, ADRs
├── tools/                      # build, simulator, font and log helpers
├── .github/                    # CI workflows, issue/PR templates
├── .claude/                    # Claude Code agents and commands
├── AGENTS.md
└── CLAUDE.md
```

## Build and test

| What | How |
|---|---|
| Firmware | [`docs/toolchain.md`](docs/toolchain.md) (Windows ST toolchain); `tools/build-firmware-linux.sh` on Linux |
| UI simulator | `tools/run-ui-simulator.sh` ([`firmware/simulator`](firmware/simulator/README.md)) |
| Host | [`host/README.md`](host/README.md) |
| Protocol codec | [`protocol/tests`](protocol/tests/README.md) |
| Integration test | [`tests/integration`](tests/integration/README.md) |

After cloning, run `git submodule update --init` (LVGL).

An overview for readers is in the
[wiki](https://github.com/egungor/stm32-desk-dashboard/wiki); its pages
are kept in [`docs/wiki/`](docs/wiki/README.md).

## Development model

This repository uses **Claude Code** as the AI development environment.

GitHub is used for:

- source control
- issues
- pull requests
- code review
- CI
- releases
- public documentation

The human developer remains responsible for architecture decisions, hardware validation, and final merges.

## Important rule

An AI agent must never claim hardware functionality was verified unless it was actually tested on the physical board.

See:

- [`CLAUDE.md`](CLAUDE.md)
- [`AGENTS.md`](AGENTS.md)
- [`docs/development-workflow.md`](docs/development-workflow.md)
- [`docs/architecture.md`](docs/architecture.md)
