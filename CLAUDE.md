# STM32 Desk Dashboard — Claude Code Instructions

## Mission

Build a Windows desk dashboard on the touchscreen of an STM32F7
Discovery board. The LCD has tabs: **Dashboard** (the main tab: clock,
weather, menu of the day, media keys, Claude Code approvals) and **Audio**
(selecting the default Windows playback device, volume and mute — the
inherited stm32-audio-controller), plus settings. More tabs come later.

This repository continues egungor/stm32-audio-controller; its history,
milestones M0–M7 and ADR-001…004 are kept. The audio features must keep
working as they do there.

The system has three major parts:

1. STM32 firmware
2. Windows C++ host application (tray program + console tools)
3. Shared STM32 ↔ Windows protocol

Design principle: **thin firmware, content from the host.** The board
knows a few generic components (card, detail list, approval prompt, media
strip); weather, menu, notifications and other features are written on the
host. Avoid over-engineering: no plugin systems, no generic frameworks
beyond what the current step needs.

## Current milestone

Dashboard v1, step 7 (#16): media keys on the host (`App/MediaKeys`,
`Platform/Windows/WinMediaKeys`: the media strip's `ACTION` presses
previous / play-pause / next through `SendInput`). Done: steps 1–6
(rename and USB PID `1209:0002`; ADR-005/006/007; protocol v3; tabbed
shell; host `FrameRouter` and `DashController`; board dashboard
components). Features that fill the cards (weather, menu, Claude Code)
come in steps 8–11. The v1 steps and their status are in
`docs/roadmap.md`, "Dashboard v1". Do not start a later step unless the
current task asks for it.

Inherited open work: the M7 board and Windows checks listed in
`docs/hardware.md`, "Pending hardware and Windows tests".

## Target hardware

- Board: STM32F746G-DISCO (32F746GDISCOVERY), board reference MB1191
- MCU: STM32F746NGH6
- LCD: 4.3" 480x272 capacitive touchscreen, ROCKTECH RK043FN48H-CT672B
- Touch controller: FT5336
- GUI: LVGL v9.5.0 (`firmware/ui`, git submodule)
- USB: vendor USB HID, VID:PID `1209:0002` (ADR-003; `1209:0001` is the audio controller)

The board identity above has been verified on the physical board. See
`docs/hardware.md` for details. Do not invent GPIO or peripheral mappings;
take them from the official STM32F746G-DISCO documentation (UM1907, MB1191
schematic) and the STM32CubeF7 BSP.

## Host

- Platform: Windows
- Language: C++17
- Audio: Windows Core Audio / MMDevice
- USB transport: HID
- Host code must isolate Windows-specific APIs behind small interfaces.

## Architecture rules

- The protocol is the contract between firmware and host.
- Firmware must not contain Windows-specific concepts.
- Host code must not depend on STM32 GUI implementation details.
- Keep hardware-dependent firmware code isolated from application logic.
- Prefer deterministic memory use in firmware.
- Avoid unnecessary dynamic allocation in embedded real-time paths.
- Keep interrupt handlers short.
- Do not block in interrupt context.
- Do not introduce unnecessary third-party dependencies.
- Do not change the protocol silently.
- Protocol changes require documentation and tests.
- Do not modify generated STM32Cube/CubeMX code unnecessarily.
- Keep generated code separate from application code whenever practical.

## Development workflow

For every non-trivial task:

1. Inspect the repository and relevant documentation.
2. Identify affected components.
3. Explain the implementation plan before making broad changes.
4. Make the smallest coherent change.
5. Build the affected target.
6. Run relevant tests.
7. Review the change for architecture, safety, and regressions.
8. Clearly state what was tested and what remains to be tested manually.

## Truthfulness rule

Never claim:

- a hardware test passed when it was not performed;
- a build passed when it was not run;
- a test passed when it was not run;
- a Windows audio operation was verified when it was only reasoned about.

Use explicit labels:

- `VERIFIED`
- `NOT VERIFIED`
- `REQUIRES HARDWARE`
- `REQUIRES WINDOWS`
- `NOT TESTED`

## Git rules

Prefer one logical change per commit.

Commit style:

- `feat(...)`
- `fix(...)`
- `test(...)`
- `refactor(...)`
- `docs(...)`
- `build(...)`
- `ci(...)`
- `chore(...)`

Do not mix unrelated changes.

## Definition of done

A task is not complete until:

- implementation is present;
- relevant code builds, where a build exists;
- relevant tests pass, where tests exist;
- documentation is updated if an interface or architecture changed;
- untestable hardware behavior is explicitly identified;
- the final response summarizes files changed and verification performed.

## Repository map

- `firmware/` — STM32 code
- `host/` — Windows application
- `protocol/` — shared communication contract
- `tests/integration/` — firmware ↔ host loopback test (no hardware)
- `docs/` — architecture and project knowledge
- `.claude/agents/` — Claude Code specialist agents
- `.github/` — GitHub workflows/templates

## Branch Naming Convention

All development branches must follow:

<type>/<issue-number>-<short-description>

Allowed types:

- feature/
- fix/
- refactor/
- test/
- docs/
- chore/
- hardware/

Rules:

- Use lowercase.
- Use kebab-case.
- Do not use spaces.
- Do not use Turkish characters.
- Keep branch names concise.
- Include the GitHub issue number whenever the work is associated with an issue.

Examples:

- feature/12-usb-hid
- feature/15-audio-enumeration
- fix/23-hid-reconnect
- refactor/31-protocol-parser
- test/18-protocol-tests
- docs/8-hardware-setup
- chore/5-github-actions
- hardware/20-display-bringup
