# Hardware

## Target Board

| Item | Value | Status |
|---|---|---|
| Board | STM32F746G-DISCO (ST order code: 32F746GDISCOVERY) | VERIFIED (physical board) |
| Board reference | MB1191 | VERIFIED (physical board) |
| MCU | STM32F746NGH6 | VERIFIED (physical board) |
| Core | Arm Cortex-M7, up to 216 MHz | From ST documentation |
| Internal memory | 1 Mbyte flash, 340 Kbytes RAM | From ST documentation |

> Note: earlier revisions of this repository referred to the MCU as
> STM32F756NGH6. That was incorrect. The verified MCU is **STM32F746NGH6**.

## Display

| Item | Value | Status |
|---|---|---|
| Size | 4.3" color TFT LCD | VERIFIED (physical board) |
| Resolution | 480 × 272 | VERIFIED (physical board) |
| Panel | ROCKTECH RK043FN48H-CT672B | VERIFIED (physical board) |
| MCU interface | LTDC, parallel RGB (LTDC_R0–R7, G0–G7, B0–B7 per UM1907) | From ST documentation |
| BSP panel driver | `rk043fn48h` (STM32CubeF7 BSP) | From ST BSP |

## Touch

| Item | Value | Status |
|---|---|---|
| Type | Capacitive | VERIFIED (physical board) |
| Controller | FT5336 | VERIFIED (physical board) |
| Bus | I²C | From ST BSP |
| Interrupt signal | `LCD_INT` | From ST documentation |
| BSP driver | `ft5336` component (STM32CubeF7 BSP) | From ST BSP |

## External Memory

- SDRAM: 128-Mbit device on FMC; only the lower 16 data bits are used,
  so 64 Mbit is accessible (UM1907).
  - Part MT48LC4M32B2B5-6A (`.ioc` pin labels), FMC SDRAM bank 1,
    `0xC0000000`–`0xC07FFFFF` (8 MB): 12 row bits, 8 column bits,
    4 internal banks, 16-bit bus.
  - CubeMX configures only the FMC controller; the JEDEC init sequence
    and refresh rate are in `App/Src/sdram.c` (CL3, SDCLK 100 MHz,
    refresh count 1542).
  - The LTDC must not read SDRAM while the init sequence runs (see the
    2026-10-13 LCD bring-up entry below).
- Quad-SPI NOR flash: 128 Mbit (UM1907).

## Debug

- On-board ST-LINK/V2-1 (UM1907).

## GPIO / peripheral mapping

See [`pin-mapping.md`](pin-mapping.md) for the pins, peripherals and
clocks the firmware configures, with the verification status of each.

Exact GPIO and peripheral assignments must be taken from the official
STM32F746G-DISCO sources listed below (UM1907 I/O assignment table,
MB1191 schematic, STM32CubeF7 BSP) and must not be guessed.

## Bring-up checklist

- [x] Confirm exact board model printed on PCB (STM32F746G-DISCO, MB1191)
- [x] Confirm MCU marking (STM32F746NGH6)
- [x] Identify LCD panel (ROCKTECH RK043FN48H-CT672B)
- [x] Identify touch controller (FT5336)
- [x] Identify LCD resolution (480 × 272)
- [x] Confirm touch I²C bus/address against BSP on hardware — I2C3,
  address `0x38` (7-bit), chip ID `0x51` read back; VERIFIED 2026-10-14
- [x] Identify USB connector used for device communication — USB OTG FS
  micro-USB connector (not the ST-LINK one); VERIFIED 2026-10-13
- [x] Confirm ST-LINK connection — flash + debug via STM32Cube for VS Code;
  VERIFIED 2026-10-13
- [x] Confirm external SDRAM operation — 8 MB self-test PASS after a
  software reset (2026-10-13) and after a power cycle (2026-10-14);
  VERIFIED
- [x] Confirm clock configuration — `CLOCK MEASURE PASS`, TIM5 clock
  within 12 ppm of 100 MHz against the LSE; VERIFIED 2026-10-14
- [x] Confirm ST-LINK virtual COM port output on hardware — USART1,
  TX `PA9`, RX `PB7`, 115200 baud, `COM4` on the developer host;
  VERIFIED 2026-10-13
- [ ] Capture board photos in `docs/images/`

## Bring-up log

### 2026-10-13 — first boot (developer, physical board)

Firmware: branch `feature/1-firmware-skeleton` after ADR-001 (trimmed
peripherals + MPU fix). Built and flashed with the Windows ST toolchain
from STM32Cube for VS Code.

| Check | Result |
|---|---|
| Board flashes and halts at `main` | VERIFIED |
| `MX_FMC_Init()` no longer faults (MPU regions 1–2) | VERIFIED — reaches `osKernelStart()` |
| FreeRTOS scheduler runs the default task | VERIFIED — implied by USB enumeration, which happens in `StartDefaultTask` |
| USB OTG FS enumerates on Windows | VERIFIED — "HID-compliant mouse" (CubeMX default HID class; Custom HID is M3 work) |
| ST-LINK VCP (USART1) output | NOT TESTED — no logging code yet |
| LCD / touch | NOT TESTED — SDRAM init sequence and display code not written yet |

### 2026-10-13 — SDRAM and LCD bring-up (M1, issue #7)

Firmware: branch `hardware/7-lcd-bringup`. Built with the command-line
Windows ST toolchain (CMake 4.4.0+st.1), flashed with
`STM32_Programmer_CLI`, log read from `COM4`. Commands: `docs/toolchain.md`.

| Check | Result |
|---|---|
| VCP log on `COM4` (USART1, 115200 8N1) | VERIFIED — `BOOT OK sysclk=200000000` |
| SDRAM JEDEC init | VERIFIED — `SDRAM INIT OK`; FMC registers read over SWD: SDCR1 `0x19D4`, SDTR1 `0x01126361`, SDRTR `0x0C0C`, last SDCMR = LOAD MODE `0x230` |
| SDRAM self-test (data bus, address bus, full 8 MB, two patterns) | VERIFIED — `SELFTEST SDRAM PASS size=8388608 ms=1492`, 5 consecutive software resets |
| Test pattern in framebuffer | VERIFIED — framebuffer dumped over SWD, converted to PNG, pixel-exact match with the expected pattern; pixel sum `0x010493A2` matches the log |
| Pattern visible on the panel (colours, orientation, stability) | VERIFIED — developer visual check on the physical panel |
| Boot after a power cycle (SDRAM from cold) | NOT TESTED — all runs used a software reset via ST-LINK |
| Long-run SDRAM retention / temperature | NOT TESTED |

Failure found and fixed during bring-up: with the LTDC enabled (as
`MX_LTDC_Init()` leaves it), the self-test failed in the fill pass
(`SELFTEST SDRAM FAIL test=fill addr=0xC0000010 exp=0x00000004
got=0x00000000`). Single SWD writes to SDRAM smeared into neighbouring
columns, while the same writes to internal SRAM did not, i.e. the
SDRAM was still in its power-up burst mode. Cause: LTDC framebuffer
fetches were interleaved by the FMC with the init commands, and the
LOAD MODE REGISTER command was ignored by the device because a bank was
open. Fix: `App_Main` disables the LTDC until SDRAM is initialised and
the pattern is drawn. Disabling the LTDC only during the self-test did
not help; it must be off during the init sequence.

### 2026-10-14 — M1–M6 hardware and Windows checks (issue #48)

Firmware and host: `main` at `41bfed5`. Firmware built with the
command-line Windows ST toolchain (CMake 4.4.0+st.1, GCC 14.3.1+st.2,
Debug, 0 warnings) in three variants — `build/TestScreen`
(`-DAPP_TEST_SCREEN=ON`), `build/UiDemo` (`-DAPP_UI_DEMO=ON`) and
`build/Debug` (default) — and flashed with `STM32_Programmer_CLI`. Log
read from `COM4`. Host built with MSVC (Visual Studio 2022 17.11.5,
Debug). Touches, cable changes and the visual checks on the panel and in
Settings → Sound were done by the developer; log lines, HID data and
framebuffer dumps were read on the PC.

Test screen build:

| Check | Result |
|---|---|
| Clock | VERIFIED — `CLOCK CONFIG OK sysclk=200000000 hclk=200000000 pclk1=50000000 pclk2=100000000`; `CLOCK MEASURE PASS ref=LSE tim=100001244 exp=100000000 ppm=12`. Later boots: `ppm=0` and `ppm=12` |
| SDRAM self-test | VERIFIED — `SELFTEST SDRAM PASS size=8388608`; `ms=` varied between boots and builds (1176–1727) |
| Touch controller | VERIFIED — `TOUCH INIT OK id=0x51` |
| Test pattern in framebuffer | VERIFIED — `sum=0x010493A2` as on 2026-10-13. `fb2png.py --expected` reports 453 mismatches, all within ±10 px of the five touch-target crosshairs, which the script does not draw |
| Touch coordinates against the five targets | VERIFIED — axes and orientation correct (no swap or mirroring); see the table below |
| Dot follows the finger | VERIFIED — developer visual check |
| Boot after a power cycle (SDRAM from cold) | VERIFIED — ST-LINK cable unplugged for about 18 s: `SELFTEST SDRAM PASS`, `LCD PATTERN DRAWN … sum=0x010493A2`, pattern on the panel; framebuffer dump byte-identical to the one after a software reset. `BOOT OK` and `CLOCK CONFIG` were sent before Windows reopened `COM4` and were not captured |

| Target | `TOUCH TARGET` | `TOUCH DOWN` | Difference |
|---|---|---|---|
| 0 top left | 20, 20 | 17, 32 | −3, +12 |
| 1 top right | 459, 20 | 467, 25 | +8, +5 |
| 2 bottom left | 20, 251 | 22, 260 | +2, +9 |
| 3 bottom right | 459, 251 | 463, 264 | +4, +13 |
| 4 centre | 240, 136 | 240, 144 | 0, +8 |

Every touch landed 5–13 px below the target, x within ±8 px. The offset
has the same sign at all five targets, so it looks like the finger's
contact point rather than a scaling error; no calibration was changed.

Demo UI build:

| Check | Result |
|---|---|
| UI on the LCD | VERIFIED — `UI INIT OK fb0=0xC0000000 fb1=0xC0040000`, `UI DEMO DEVICES n=5 active=1`; framebuffer dump matches the design; developer visual check on the panel |
| Taps | VERIFIED — device cards on both pages (`UI SELECT id=<n>`, `UI DEMO ACTIVE` 0.41 s later), page buttons |
| Volume and mute | VERIFIED — `UI VOLUME value=<n>` on release, `UI MUTE on/off` |
| Tearing | VERIFIED — none seen while dragging the slider, upright and flipped (developer visual check) |
| Flip button (#42) | VERIFIED — short taps do nothing; a hold of 1 s or more arms it, it flips on release. Image upright in both orientations; touch maps correctly while flipped (cards, slider, mute, flip button) |
| `UI FLIP off copy_max_ms` | NOT AS EXPECTED — `copy_max_ms=25` (demo) and `23` (normal build), not "a few ms". The 180° copy of the whole frame from SDRAM to SDRAM takes most of a 33 ms frame period. The buffer swap still waits for vertical blanking, so there is no tearing, but the frame rate drops while flipped. Fixed in #51: 5 ms, see below |

Default build, USB and host:

| Check | Result |
|---|---|
| USB enumeration | VERIFIED — `USB\VID_1209&PID_0001`, HIDClass "HID-compliant vendor-defined device"; `HidP_GetCaps`: usage page `0xFF00`, usage `0x01`, input/output report 65 bytes (64 + report ID byte, no report ID), no feature report — as ADR-003. `USB CONFIGURED` in the log |
| Host unit tests (`ctest`) | VERIFIED — 1/1 passed, after building with C4127 suppressed locally (see below) |
| `stm32-audio-host list` | VERIFIED — the 9 active render endpoints, same names as Windows' endpoint list (`Get-PnpDevice -Class AudioEndpoint`) |
| `set-default`, `volume`, `mute` | VERIFIED — Settings → Sound showed the new default, volume and mute (developer check) |
| `watch` | VERIFIED — Windows-side changes of the default device, volume (each step) and mute arrive as `DEVICE_CHANGED` / `VOLUME_CHANGED` / `MUTE_CHANGED`; after a device change the volume and mute of the new default follow |
| `run` started before the board (step 1) | VERIFIED — `Controller connected`, 9 × `DEVICE_LIST`, `DEVICE_CHANGED`, `VOLUME_CHANGED`, `MUTE_CHANGED`; the LCD shows the 9 devices with the default marked |
| Select on the LCD (step 2) | VERIFIED — `<- SELECT_DEVICE index=n`, `-> DEVICE_CHANGED index=n`; Settings → Sound follows |
| Default changed in Windows (step 3) | VERIFIED — `LINK ACTIVE` without `UI SELECT`; the LCD follows |
| Unplug and replug (step 4) | VERIFIED — `Controller disconnected.`, then `Controller connected` and the list again; `LINK LIST OK` about 1.2 s after plugging in |
| `run` started with the board plugged in, default changed while it was closed (steps 5 and 7) | VERIFIED — the first list already marks the new default; `LINK ACTIVE` 80 ms after `USB CONFIGURED`, no tap; the LCD shows it |
| Ctrl+C (step 6) | VERIFIED — `Stopped. sent=192 send_failures=0 rejected=0 ignored=0 errors_received=0 dropped=0` |
| Volume and mute both ways (step 8, #40) | VERIFIED — slider: several `SET_VOLUME` per drag, not one per pixel, Windows follows, 0 and 100 reached; mute tap: `SET_MUTE`, `MUTE_CHANGED`; volume and mute changed in Windows reach the LCD (`VOLUME_CHANGED` without `SET_VOLUME`, `LINK MUTE` without `UI MUTE`) |
| LCD screenshots | Done — `docs/images/ui-main-screen.png`, `ui-muted.png`, `ui-waiting-for-host.png` (framebuffer dumps of the default build) |

Observations, not failures:

- **`USB CONFIGURED` with the cable unplugged.** Unplugging the USB OTG
  FS cable logs `USB DISCONNECTED` and, about 100 ms later,
  `USB CONFIGURED` (seen twice). The LCD stays on the waiting screen and
  plugging in again works, so only the log line (and the link state
  behind it) is wrong. Closing the host program also logs
  `USB DISCONNECTED` and reopening it `USB CONFIGURED`, which fits Windows
  suspending the idle HID device. Plugging in logs one or two
  DISCONNECTED/CONFIGURED pairs before the list (bus resets during
  enumeration).
  Fixed by #52, see the next entry.
- **Long device names are cut** with "…" on the cards (two lines), and
  the protocol cuts "Kulaklık İşitme Bölümü (G435 Wireless Gaming
  Headset)" to "… (G435 Wireless Gami".
- **Host test build with Visual Studio 17.11.5:** `test_host.cpp:745`
  `CHECK(kReportBytes == 65)` raises C4127 (conditional expression is
  constant), which `/WX` turns into an error. `stm32-audio-host.exe`
  itself builds without warnings, and Host CI (newer MSVC) is green.

### 2026-10-14 — USB state after unplugging the OTG FS cable (issue #52)

Firmware `0bc52bf` (branch `fix/52-usb-configured-after-unplug`),
Windows ST build (0 warnings), host `stm32-audio-host run` from main. The
developer unplugged and replugged the OTG FS cable; log on COM4.

Cause of the #48 observation: the OTG FS core runs without VBUS
sensing, because PA9 is USART1_TX on this board. So the core never
reports a disconnect, and an unplug is only a USB suspend; the stack
keeps CONFIGURED as the state to resume to. About 100 ms later a wake-up
interrupt from the floating lines restored CONFIGURED with no host
attached. The fix watches VBUS on PJ12 (`OTG_FS_VBUS`, read high with
the cable in and low without it) and resets the device stack when it
drops. It also counts the device as configured only while SOFs arrive,
because the spurious resume comes before VBUS has fallen.

| Check | Result |
|---|---|
| Intermediate build `ec980b4` (VBUS check only), two unplugs | NOT ENOUGH — each unplug logged `USB DISCONNECTED`, `USB RESUMED`, `USB VBUS OFF`, `USB DISCONNECTED`: the spurious resume still arrives before VBUS falls (end state correct, no `USB CONFIGURED`) |
| Unplug, `0bc52bf`, twice | VERIFIED — only `USB DISCONNECTED` then `USB VBUS OFF`; no `USB RESUMED` or `USB CONFIGURED` with the cable out |
| Replug, `0bc52bf`, twice | VERIFIED — `USB VBUS ON`, `USB CONFIGURED`, `LINK LIST OK n=9 gen=1`, `LINK ACTIVE id=0`; the list is back on the LCD |
| Boot with the cable plugged in | VERIFIED — `USB VBUS ON`, `USB CONFIGURED`, `LINK LIST OK n=9` |
| Host program closed, then started again | VERIFIED — closing logs `USB DISCONNECTED` then `USB RESUMED` (Windows suspends the idle device and wakes it again, with SOFs); the restarted host gets the list (`Controller connected`, `LINK LIST OK n=9`) with no new `USB CONFIGURED`, because the device stayed configured |
| Replug enumeration: DISCONNECTED/CONFIGURED pairs seen in #48 | Not seen in these two replugs (one `USB CONFIGURED` each) |

### 2026-10-14 — protocol version 2 on the board (issue #58)

Firmware and host from branch `feature/58-protocol-v2` (`26e6c5e`; the
host binary was built just before a one-line `static_cast` in
`HostController.cpp` that is part of that commit, with no change in
behaviour). Firmware
built with the Windows ST toolchain (Debug, 0 warnings), host with MSVC
(Visual Studio 2022 17.11.5). The previous v1 `stm32-audio-host run` was
stopped first: a v1 host and a v2 board reject each other's frames.
Taps, cable changes and the visual checks were done by the developer.

| Check | Result |
|---|---|
| Connect: list, default, volume, mute, then `SETTINGS` | VERIFIED — host sends 9 × `DEVICE_LIST` and `SETTINGS`; `LINK LIST OK n=9 gen=1` |
| Select with the list generation | VERIFIED — `SELECT_DEVICE gen=1 index=1` → `DEVICE_CHANGED index=1`, Windows switched |
| Mute from the LCD | VERIFIED — `SET_MUTE` → `MUTE_CHANGED` |
| Flip through the settings record | VERIFIED — `SET_SETTINGS flipped=1` → `SETTINGS flipped=1` → `LINK SETTINGS … flipped=1`, `UI FLIP on`; back and forth three times, `copy_max_ms=5` |
| Unplug and replug while flipped | VERIFIED — the host sends `SETTINGS flipped=1` after the list; the display stays flipped (no `UI FLIP off`; developer visual check) |
| Errors | None on either side; no `LINK EDIT FAIL`, no timeouts |
| Alias, favorite, order from the board | NOT TESTED on hardware — no UI for them yet (#60); covered by the link, host and integration tests |
| Settings after a host restart | NOT TESTED — the host keeps preferences in memory only until #59 |

### 2026-10-15 — M7 UI phase 1: tabs, themes, accents, grids (issue #60)

Firmware from branch `feature/60-ui-foundation` (Debug, 0 warnings,
flash 624 KB), host from #59 (`run` with the preference file). Settings
and favorites were set by editing the preference file while `run` was
running; the developer tapped the tabs and checked the panel.

| Check | Result |
|---|---|
| Favorites from the file | VERIFIED — new list `gen=3` with two favorites; stars on the cards |
| Theme light, accent blue, grid 3x2 from the file | VERIFIED — after the fix below: board rebuilt in the light theme with blue accent and 3x2 cards (framebuffer dump and developer check: readable, nothing overlaps) |
| Startup tab "last used" | VERIFIED — after a board reset the favorites tab opened (`tab=0/1`) |
| Tabs | VERIFIED — "Tümü" shows all 9 devices over 2 pages, "Favoriler" the 2 favorites; every tap sent `SET_SETTINGS` with `last_tab`, answered by `SETTINGS` |
| A damaged line in the file | VERIFIED — a test script wrote `setting theme 1,accent 1,grid 1`; the host kept the default for that field |
| Bug found: frames lost after a long UI loop | `send_failures=4`, USB `tx_frames=2` read over SWD; `UsbLink_IsConfigured()` sampled SOFs only once per UI loop (#64). Fixed (PR #65, also on this branch); afterwards `send_failures=0` |
| Collision: a settings change in the file and a tab tap at the same time | The whole record from the board overwrote the file's change once; expected with whole-record settings (ADR-004), noted |

## Firmware peripherals in use

Pins and verification status: [`pin-mapping.md`](pin-mapping.md);
selection: `decisions/ADR-001-firmware-peripheral-set.md`.

| Peripheral | Use |
|---|---|
| LTDC | LCD, RGB565, scans out one of the SDRAM framebuffers |
| FMC | SDRAM bank 1 (8 MB): four framebuffers — FB0/FB1 (`0xC0000000`, `0xC0040000`) where LVGL draws, FB2/FB3 (`0xC0080000`, `0xC00C0000`) for the 180° copy while the display is flipped (#42) |
| I²C3 | FT5336 touch controller, polled |
| USB OTG FS | Vendor HID device `1209:0001` (ADR-003) |
| USART1 | Log on the ST-LINK virtual COM port |
| TIM5 | Clock measurement against the LSE at boot, released afterwards |

## Pending hardware and Windows tests

The checks for the M1–M6 code ran on 2026-10-14 on firmware and host
`41bfed5` (issue #48); details and observations are in the bring-up log
above. Open: the M7 settings page, screen timeout, edit mode, layouts B and C and the larger LVGL heap, the memory watermarks, the tray program and the volume limit on switch (last rows). New checks go here first,
and their results into the bring-up log, with the date and the firmware
commit.

| Check | Where | Status |
|---|---|---|
| `CLOCK MEASURE PASS`, error of a few tens of ppm or less | #16, `docs/firmware-log.md` | VERIFIED 2026-10-14 (`ppm=12`) |
| `TOUCH INIT OK id=0x51`; the five targets log their coordinates; the dot follows the finger | #18 | VERIFIED 2026-10-14 (touches 5–13 px below the targets, see log) |
| UI on the LCD as in the simulator; taps work; no tearing | #24 | VERIFIED 2026-10-14 (framebuffer dump and panel checked against the design; not compared pixel by pixel with the simulator) |
| Board enumerates as `1209:0001`, vendor HID, 64-byte reports | #30, ADR-003 | VERIFIED 2026-10-14 |
| `stm32-audio-host list`, `set-default`, `volume`, `mute`, `watch` match Settings → Sound | #28, `host/README.md` | VERIFIED 2026-10-14 |
| End to end with `stm32-audio-host run` (list, select on the LCD, Windows changes, unplug/replug, host restart) | `host/README.md`, "Manual check with the board" | VERIFIED 2026-10-14 (steps 1–8) |
| Volume and mute: drag the slider and tap mute on the LCD → Windows follows; change them in Windows → the LCD follows | #40, `host/README.md` | VERIFIED 2026-10-14 |
| Flip button (hold 1 s, top right): image and touch upright in both orientations, no tearing; `UI FLIP off copy_max_ms=<n>` small (a few ms expected) after dragging the slider while flipped | #42 | VERIFIED 2026-10-14 — image, touch, no tearing; `copy_max_ms=5` after the #51 fix (was 23–25 ms) |
| Boot after a power cycle (SDRAM from cold) | bring-up log above | VERIFIED 2026-10-14 |
| LCD screenshots of the final UI in `docs/images/` | `docs/images/README.md` | Done 2026-10-14 (main, muted, waiting screens) |
| Settings page (#60): the cog opens it; each control changes the setting at once (theme, colour, grid, language rebuild the screens and the page stays open); the list scrolls by finger and scrolling over a control changes nothing; "Ekranı döndür" turns the display and touch follows; `LINK SETTINGS` (or `UI SETTINGS` in the demo build) shows each change; with `stm32-audio-host run` the preference file has the new record and a host restart restores it | `firmware/ui/README.md`, `docs/firmware-log.md` | REQUIRES HARDWARE + WINDOWS |
| Screen timeout (#60): with "Ekran kapanma" at 1 dk, the backlight goes off about a minute after the last touch (`UI BACKLIGHT off`); a touch lights it (`UI BACKLIGHT on`) and does not select anything; the next tap works; "Kapalı" keeps it lit | `firmware/ui/README.md`, `docs/firmware-log.md` | REQUIRES HARDWARE |
| Edit mode (#60): the pencil opens it; ★ marks a favorite, the arrows move a device, the keyboard sets an alias with Turkish letters (✓ / "Kaydet" saves, × does not); each change comes back as a new list (`LINK LIST OK`), the preference file holds it, and Windows' own names are unchanged | `firmware/ui/README.md`, `docs/firmware-log.md` | REQUIRES HARDWARE + WINDOWS |
| Layouts B and C (#60): "Görünüm" switches between Kart, Liste and Karo; in B the rail switches tabs and the vertical slider sets the Windows volume; in C the panel follows the Windows default device (also when changed in Windows) and its slider and mute work; taps select in both; no tearing while dragging | `firmware/ui/README.md` | REQUIRES HARDWARE + WINDOWS |
| Mute and volume (#60): in each layout (Kart, Liste, Karo), set the volume to about 50 %, tap mute, then tap it again also on its edge next to the slider: the volume stays the same (no `UI VOLUME` line, Windows unchanged); while muted, drag the slider: `UI MUTE off` once, Windows unmutes and takes the new volume | `firmware/ui/README.md`, `docs/firmware-log.md` | Layout A VERIFIED 2026-10-16 (`a94d36b`): after a drag, mute on/off keeps the percentage; dragging while muted gives `UI MUTE off` once; no `UI VOLUME` on mute taps. Layouts B and C REQUIRES HARDWARE + WINDOWS |
| LVGL heap 128 KB (#60): boot log as before (`UI INIT OK`), no LVGL out-of-memory assert after opening every screen in every layout | `firmware/ui/lv_conf.h` | REQUIRES HARDWARE |
| Memory watermarks (#60): a `MEM ...` line follows `UI INIT OK` and repeats every 60 s; after opening every screen in every layout (the row above) and dragging the sliders, copy the last line's numbers into the "Measured on the board" table of `docs/memory-review.md`; no `RTOS STACK OVERFLOW` or `RTOS MALLOC FAILED` line | `docs/memory-review.md`, `docs/firmware-log.md` | REQUIRES HARDWARE |
| Tray program (#74): `stm32-audio-tray.exe` starts without a console window and shows the icon; the tooltip and the menu's first line say "Kumanda bekleniyor", then "Kumanda bağlı" when the board is plugged in, and the board works as with `run`; "Windows ile başlat" adds the Run value (check mark; `stm32-audio-host startup status` says on) and the tray program starts after logging off and on; "Günlüğü aç" shows the frames; "Çıkış" removes the icon and the preference file keeps the last change; a second tray program or `run` says another program serves the controller; English menu after choosing English on the board | `host/README.md`, "Tray program" | REQUIRES WINDOWS + HARDWARE |
| Dashboard v1 beta (`v0.9.0-beta.1`, #22): install and flash; Audio tab as before; clock; media keys; weather card and detail (stale note without network); menu import with the October images, card and day detail, ‹ ›, holidays; English texts; `MEM` lines after opening every detail | issue #22 (checklist), `host/README.md` | REQUIRES HARDWARE + WINDOWS |
| Now playing (#31, #32, ADR-008): Spotify, a YouTube tab and the Media Player in turn: the Music tab (second on the rail) shows title, artist, album, app and the cover within about 3 s of a new track; the progress counts on and matches the player; pause / play / skip on the PC change the board within a second; ⏮ ▶/⏸ ⏭ on the Music tab and on the Dashboard strip control the player; the Dashboard's middle button shows pause while playing; the strip's volume slider and speaker button change the Windows volume and mute and follow the Audio tab; "Açılış ekranı" Müzik opens the Music tab after a reset; `host.log` and the VCP log (`DASH ART shown`, no endless `GET_MEDIA`); `MEM` lines healthy | `host/README.md` "Now playing", `firmware/ui/README.md` | VERIFIED 2026-10-22 with Deezer (firmware and host from `main` `977dd20`): the Dashboard's middle button shows pause while playing and play otherwise; the Music tab shows the cover, texts and progress; the controls on the Music tab and the strip control Deezer; the strip's volume follows Windows and the Audio tab. VCP log: `DASH MEDIA`, `DASH ART shown`, healthy `MEM`. Spotify, a YouTube tab, the Media Player, "Açılış ekranı" Müzik after a reset and cover retries (`GET_MEDIA`) while the UI redraws: REQUIRES HARDWARE + WINDOWS |
| Claude Code approval (#26, ADR-007; in `v0.9.0-beta.2`): with the copied snippet in `settings.json`, a command in a session on this PC shows the prompt on the board (also over the Audio tab and on a dark screen, which lights up); Onayla runs it, Reddet refuses it, Terminalde sor and 60 s without an answer give the terminal question; two requests show "1/2"; unplugging the board or quitting the tray gives the terminal question; a long Write shows only Reddet / Terminalde sor; the Claude Code card counts and lists decisions | `host/README.md`, ADR-007 | REQUIRES WINDOWS + HARDWARE |
| Notifications (#34, ADR-009): `dashboard-host notify focus`, then turn Do not disturb on and off: the first value changes away from 5 `ACCEPTS_NOTIFICATIONS` while it is on (if not: which value changes, for the ADR's fallback). `dashboard-host notify` prints the card, the list and the banner of the newest toast. With the tray and the board: the fourth card counts the toasts of the notification centre and shows the newest three as `app: title`; a new WhatsApp / Teams / Outlook / Chrome toast shows a banner at the top for 5 s over the Dashboard, the Audio and the Music tab and the settings page; a tap opens the notifications list (on the settings page it only closes); dismissing a toast on the PC removes it from the card and the open list within a second; no banner while a Claude Code prompt is shown, with Do not disturb on, or on a dark screen (which stays dark); a burst shows one banner, then the newest after 2 s; `host.log` has app names but no texts; `MEM` lines healthy | ADR-009, `host/README.md` | REQUIRES WINDOWS + HARDWARE |
| Dashboard redesign (#47, ADR-010): weather card with a large icon, place, temperature, low / high, background by the weather; menu card without a title (main, soup, dessert), green; "Yemek listelerini düzenle…" opens `menu-lists.txt`; a dish of `[sevmiyorum]` makes it yellow, of `[istemiyorum]` orange, and the detail marks it; on such a working day after 08:00 the warning appears once (also on a dark screen), "Tamam" closes it and it does not come back that day (also after a tray restart); bell and Claude Code icon at the top right (red until the list is opened; Claude: red while waiting, yellow after a timeout, green after an allow); the now-playing card (cover, title; a tap opens the Music tab); "Resim seç…" shows the picture within ~4 s, cropped not stretched (try a tall phone photo and a wide panorama); `dashboard-host picture` says OK; light theme; `MEM` lines healthy | ADR-010, `host/README.md` | REQUIRES WINDOWS + HARDWARE |
| Full-screen picture (#53, protocol v7): host and board updated together; "Resim seç…" with a portrait phone photo shows it upright (#51) and cropped on the card after about 20 s (note the time); a tap on the card shows it full screen, sharp and not stretched, a tap closes it; a Claude Code prompt or a banner appears over it; removing the picture or unplugging the host closes it; the Dashboard stays responsive while the picture arrives; `MEM` lines healthy | ADR-010, `protocol.md` "Picture" | REQUIRES WINDOWS + HARDWARE |
| Dashboard feedback (#49): the menu card's dessert line without "Meyve /"; the Claude Code list (robot icon) shows the description of a command, never the command; the back key at the top left of a detail and ‹ › are larger and react also to a touch slightly below them | issue #49 | REQUIRES HARDWARE + WINDOWS |
| Volume limit on switch (#59, ADR-004): with "Geçişte ses sınırı" on at 25 %, set another output above 25 % in Windows, then tap it on the board: the host logs `Volume limit: "<name>" <from>% -> 25%` and the board shows 25 %; an output at or below 25 % keeps its volume; a switch made in Windows, a host restart and plugging in the board change no volume | `host/README.md`, ADR-004 | VERIFIED 2026-10-16 (v0.7.0-beta.2, limit 40 %): each output above 40 % was lowered on selection and logged (78, 68, 61, 64 and 84 % → 40 %), an output at 40 % kept its volume, the host start changed no volume. A switch made in Windows and plugging in the board: REQUIRES WINDOWS + HARDWARE |

## Reference sources

- ST product page: 32F746GDISCOVERY —
  https://www.st.com/en/evaluation-tools/32f746gdiscovery.html
- UM1907 — Discovery kit for STM32F7 Series with STM32F746NG MCU
- MB1191 board schematic (available from the ST product page)
- STM32CubeF7 BSP for STM32746G-Discovery —
  https://github.com/STMicroelectronics/32f746gdiscovery-bsp

## Hardware validation policy

"VERIFIED (physical board)" above means the value was read from the
physical board and its original documentation. It does **not** mean the
peripheral has been brought up or tested in firmware.

A hardware-dependent feature is considered verified only after it has been
tested on the physical board.

AI agents must not infer hardware success from successful compilation.
