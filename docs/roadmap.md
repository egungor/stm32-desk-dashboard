# Roadmap

Status legend: **DONE** — implemented and verified where hardware or
Windows is involved; **CODE DONE** — implemented and tested in CI, the
hardware/Windows check is open (`docs/hardware.md`, "Pending hardware and
Windows tests"); **IN PROGRESS**; **NOT STARTED**.

This repository continues stm32-audio-controller. Milestones M0–M7 below
are that project's (issue numbers refer to its repository); the dashboard
plan is "Dashboard v1" at the end.

The M1–M6 board and Windows checks ran on 2026-10-14 (issue #48; results
in the `docs/hardware.md` bring-up log).

## M0 — AI Development Environment

Status: **DONE**

- [x] Repository structure
- [x] Claude Code project instructions
- [x] Specialist agent definitions
- [x] Architecture document
- [x] Hardware document
- [x] Protocol document
- [x] GitHub issue/PR templates
- [x] CI skeleton
- [x] Verify exact STM32 board model (STM32F746G-DISCO / MB1191, STM32F746NGH6)

## M1 — Hardware Bring-up

Status: **DONE**

- [x] Create STM32CubeMX project (CMake, `firmware/STM32AudioController/`)
- [x] Review enabled peripherals — `docs/decisions/ADR-001-firmware-peripheral-set.md`
- [x] Verify clocks (SYSCLK configured at 200 MHz) — measurement against the LSE at boot (#16), `ppm=12` on the board
- [x] Initialize LCD — SDRAM framebuffer + LTDC, issue #7; SDRAM also from a power cycle
- [x] Initialize touch — FT5336 driver (#18)
- [x] Display a static test screen — M1 test pattern, verified on the panel 2026-10-13
- [x] Verify touch coordinates — five targets on the test screen (#18)
- [x] Document hardware pin/peripheral mapping — `docs/pin-mapping.md`, issue #14

## M2 — STM32 UI

Status: **DONE**

- [x] Add LVGL
  - [x] LVGL v9.5.0 submodule, `lv_conf.h`, fonts with Turkish letters (#22)
  - [x] Hardware-independent UI module and Linux simulator with screenshots (#22)
  - [x] Port to the board: LTDC display driver, FT5336 input, timer task (#24)
- [x] Create main screen — `firmware/ui` (#22)
- [x] Create 2×2 audio device buttons
- [x] Add active-device indication — colour and text, plus a pending mark until the host confirms
- [x] Add pagination — four devices per page, arrows
- [x] Create device model — device list from the host (`firmware/link`, #30), no model of its own on the STM32
- [x] Capture LCD screenshots of the UI into `docs/images/` (#48)
- [x] Add the screenshots to the project presentation (#67, see `docs/images/README.md`)

## M3 — USB HID

Status: **DONE**

- [x] Configure USB Device — vendor HID class, `1209:0001` (#30, ADR-003)
- [x] Define HID descriptor — ADR-003, `protocol.md` "Transport" (#30)
- [x] Implement HID transport — firmware side and protocol client (#30), Windows `WinHidLink` and `run` (#32)
- [x] Test host ↔ STM32 packets
- [x] Handle disconnect/reconnect — firmware: waiting screen and list re-request (#30), no false `CONFIGURED` after unplugging (#52); host: rescan every second, controller reset (#32)

## M4 — Windows Audio

Status: **DONE**

- [x] Create Windows C++17 host project — platform-independent core + tests (#26)
- [x] Enumerate render endpoints (#28)
- [x] Build audio device model — `DeviceRegistry`, protocol controller (#26)
- [x] Detect current default endpoint (#28)
- [x] Implement default endpoint switching (#28, ADR-002)
- [x] Monitor endpoint/default-device changes (#28)

## M5 — Integration

Status: **DONE** — except endpoint removal while connected, NOT TESTED on hardware

The firmware protocol client and the host controller also run against
each other in `tests/integration` (simulated USB link and Windows, #34).

- [x] Send device list to STM32
- [x] Select device from STM32
- [x] Update Windows default endpoint
- [x] Synchronize Windows changes back to STM32
- [ ] Handle endpoint removal — logic tested (#34); adding or removing a Windows device while the board is connected NOT TESTED on hardware
- [x] Handle USB disconnect

## M6 — Audio Controls

Status: **DONE**

- [x] Volume read/write — host (#26, #28), STM32 protocol client with rate limiting (#40)
- [x] Mute — host (#26, #28), STM32 (#40)
- [x] Volume UI — bottom strip slider and percentage (#40)
- [x] Mute UI — bottom strip button (#40)

## M7 — Polish

Status: **IN PROGRESS**

Order: protocol version 2 (ADR-004, #58) → host (#59) → firmware UI
(#60). Designs: #57.

- [ ] Friendly names — aliases, edited on the touchscreen and with `dashboard-host alias` (#58–#60); keyboard coded and tested in the simulator, on the board REQUIRES HARDWARE + WINDOWS
- [ ] Favorites — favorite flag and a favorites tab (#58–#60); tab verified on the board, star in the edit mode coded and tested in the simulator
- [ ] Custom ordering — edit mode on the touchscreen (#58–#60); coded and tested in the simulator, on the board REQUIRES HARDWARE + WINDOWS
- [ ] Layouts and themes — A cards (2×2, 2×3, 3×3, list), B list, C large tile; dark/light; six accent colours (#57, #60); themes, colours and A verified on the board, B and C coded and tested in the simulator, on the board REQUIRES HARDWARE
- [ ] Settings page — layout, theme, colour, grid, startup tab, screen timeout, flip, language, volume limit on switch (#58–#60); page coded and tested in the simulator, on the board REQUIRES HARDWARE; screen timeout: backlight off after the chosen minutes without a touch, coded and tested in the simulator, on the board REQUIRES HARDWARE
- [ ] Icons — form factor icons exist (speakers, headphones, headset, display, digital, generic; #22, #26)
- [ ] Windows startup option — tray program `dashboard-tray` with "start with Windows" (HKCU Run) and `dashboard-host startup` (#74); coded, built for Windows, unit tests for the Run value; REQUIRES WINDOWS
- [ ] Error UI — waiting screen, error screen and notices for a refused or unanswered selection exist (#22, #30)
- [ ] Performance/memory review — static review in `docs/memory-review.md` (RAM 56 %, flash 37 %); stack and heap watermarks on the board REQUIRES HARDWARE
- [x] Display flip (180°) — button and port (#42), frame copy 5 ms (#51), verified on the board

## Dashboard v1

Goal: the LCD has tabs; Dashboard is the main tab (clock, weather, menu of
the day, media strip `<< || >>`, Claude Code approvals on a 2×2 card grid),
Audio keeps everything above. Notifications are the fourth card, after v1.
Design draft (Turkish): https://claude.ai/artifact/8Qc5MiqaPuFWK9HB1eSvjM

Each step is one issue and one pull request. Steps 4 and 5 can run in
parallel after step 3.

- [x] 1. Repository setup — product name, program names, settings folder, USB PID `1209:0002`, `CLAUDE.md` (#3)
- [ ] 2. Decisions — ADR-005 tabbed shell and global settings, ADR-006 card/detail/prompt model and protocol v3 (incl. the JSON library), ADR-007 Claude Code hook bridge (#5)
- [ ] 3. Protocol v3 — commands from `0x20`, chunked records, 12-byte settings record; codec tests
- [ ] 4. Tabbed shell (firmware) — left rail, tab containers, audio screens in a 424 px container, global settings
- [ ] 5. Host router and `DashController` skeleton — `GET_CARDS`, chunked records, `TIME`; integration test
- [ ] 6. Board dashboard components — `dash_client.c`, clock strip, 2×2 cards, generic detail screen, media strip
- [ ] 7. Media keys — `SendInput` previous / play-pause / next
- [ ] 8. Weather — Open-Meteo, one fixed location, card and 10-day detail
- [ ] 9. Menu of the day — menu images → Claude API → monthly JSON; card and daily detail with the salad bar
- [ ] 10. Claude Code approval: spike — hook behaviour on Windows, timeouts
- [ ] 11. Claude Code approval — `PermissionRequest` command hook, named pipe, prompt on the board
- [ ] 12. Release v1.0.0

After v1: notifications (spike first), controls (microphone, lock,
shortcuts), the title of the playing track.
