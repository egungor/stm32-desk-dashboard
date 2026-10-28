# Firmware

STM32F746NGH6 at 200 MHz, FreeRTOS with one application task, LVGL 9.5 on
two SDRAM framebuffers, FT5336 touch polled over I²C, a vendor USB HID
interface. The firmware draws what the PC sends and reports taps; it
knows nothing about Windows ([Architecture](Architecture)).

## Layout

```text
firmware/
├── STM32AudioController/     STM32CubeMX project (CMake)
│   ├── App/                  hand-written code (never generated)
│   ├── Core/ USB_DEVICE/     generated; edited only inside USER CODE
│   └── Middlewares/…/lvgl    LVGL v9.5.0 (git submodule)
├── ui/                       LVGL screens, hardware independent
├── link/                     protocol client and dashboard client, with tests
└── simulator/                headless Linux build of ui/: test script and documentation screenshots
```

| File | Role |
|---|---|
| `App/Src/app.c` | `App_Main()`: clock check, SDRAM, touch, then the UI or the test screen |
| `App/Src/usb_link.c` | Vendor HID class; the ISR copies frames into a 40-frame static queue |
| `App/Src/link_port.c` | USB state → link and dash clients → UI, every UI loop; SDRAM buffers for the cover and the picture |
| `App/Src/ui_port.c` | LVGL display on the SDRAM framebuffers, touch input, the 180° flip, the backlight |
| `App/Src/rtos_diag.c` | The `MEM` log line, stack overflow and malloc hooks |
| `App/Src/ui_demo.c` | Fixed device list for `APP_UI_DEMO` |
| `App/Src/touch.c` | FT5336 driver (I²C3, polling) |
| `App/Src/clock_check.c` | Measures the TIM5 clock against the LSE crystal |
| `App/Src/sdram.c`, `lcd_test.c` | SDRAM init and self-test, M1 test pattern |

Generated code calls the application from two USER CODE hooks:
`App_Main()` in `StartDefaultTask` and `UsbLink_DeviceInit()` in
`USB_DEVICE_Init_PreTreatment` (ADR-003).

## UI (`firmware/ui`)

| Module | Screens and components |
|---|---|
| `ui_shell.c` | the rail (Dashboard, Music, Audio, settings, connection mark) and one 424 × 272 container per tab |
| `ui_dash.c` | the Dashboard: clock strip, status icons, cards, detail view, media strip, picture card and the full-screen picture; the prompt and the banner over every screen |
| `ui_music.c` | the Music tab: cover, texts, progress with seek, controls, volume |
| `ui.c` | the Audio tab (cards layout), waiting screen, notices, volume and mute |
| `ui_layouts.c` | the Audio tab's list and large-tile layouts |
| `ui_edit.c` | the edit mode and the name keyboard |
| `ui_settings.c` | the settings page |
| `ui_theme.c`, `ui_text.c` | dark and light themes, six accents; Turkish and English texts (`tools/gen-ui-text.py`) |
| `fonts/` | Montserrat with Turkish letters at 14, 16 and 22 px, Font Awesome icons at 40 px, the Claude symbol (`tools/gen-ui-fonts.sh`) |

The UI depends only on LVGL: no HAL, no USB, no protocol framing. Every
screen is in the [Screen Gallery](Screen-Gallery).

## Protocol clients (`firmware/link`)

- **`link_client.c`** — the audio part: on USB connect it sends
  `GET_DEVICE_LIST` every second until a complete list has arrived; it
  shows a list only when all its entries are there; `SELECT_DEVICE` and
  edits without an answer in 2 s give "host not responding" and a new
  request; volume and mute rate-limited; the settings record both ways.
- **`dash_client.c`** — the dashboard part: assembles cards, details,
  prompts, banners and the media record from their 53-byte parts; the
  120 × 120 cover and the 480 × 272 picture into a second buffer, shown
  only when complete; `TIME`, status icons; sends `OPEN_DETAIL`,
  `ACTION`, `GET_CARDS`, `GET_MEDIA`, `SEEK`.
- Malformed frames are answered with `ERROR`, never an `ERROR` frame.

## Build options

| CMake option | Effect |
|---|---|
| (none) | The UI fed by the PC over USB |
| `APP_TEST_SCREEN=ON` | M1 test pattern, five touch targets, dot under the finger |
| `APP_UI_DEMO=ON` | The UI with demo devices, no PC |

The firmware version comes from `git describe` (or the `FW_VERSION`
environment variable in CI) and is put into the USB configuration string,
where the PC program reads it (ADR-011). A release ships the firmware as
`.elf`, `.hex` and `.bin`.

## Log lines

Selected lines (all of them:
[firmware-log.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/firmware-log.md)),
USART1 at 115200 8N1 on the ST-LINK virtual COM port:

| Line | Meaning |
|---|---|
| `BOOT OK sysclk=200000000` | Default task started |
| `CLOCK MEASURE PASS ref=LSE tim=… exp=… ppm=…` | Clock within ±500 ppm of expected |
| `SDRAM INIT OK`, `SELFTEST SDRAM PASS size=8388608 ms=…` | SDRAM usable |
| `TOUCH INIT OK id=0x51` | FT5336 found |
| `UI INIT OK fb0=0xC0000000 fb1=0xC0040000` | LVGL running |
| `MEM stack_free=… rtos_heap_min=… lvgl_used=… lvgl_peak=…` | After `UI INIT OK`, then every 60 s: the smallest free stack, FreeRTOS heap and the LVGL heap |
| `APP READY` | Main loop running |
| `USB CONFIGURED` / `USB DISCONNECTED` | PC link up / down |
| `LINK LIST OK n=… gen=…` | Device list received and shown |
| `LINK ACTIVE id=…`, `UI SELECT id=…` | New default from the host; the user tapped a device |
| `LINK SELECT FAIL reason=host_error\|timeout` | Selection refused or not answered |
| `LINK SETTINGS …` | Settings record from the host |
| `DASH MEDIA bytes=…`, `DASH ART shown\|none` | Now playing: a media record, a cover |
| `UI MEDIA action=1\|2\|3` | Previous / play-pause / next tapped |
| `UI BACKLIGHT on\|off`, `UI FLIP on\|off copy_max_ms=…` | Screen timeout; display turned by 180° |
| `RTOS STACK OVERFLOW task=…`, `RTOS MALLOC FAILED` | Must never appear |

## Memory

From the linker output of the M7 review
([memory-review.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/memory-review.md),
v0.7.0-beta.1): internal RAM 184,720 B of 320 KB (56 %), flash 388,588 B
of 1 MB (37 %). The LVGL heap is a 128 KB static array; in the simulator
its peak through every screen is about 93 KB (the test script fails above
90 %). Framebuffers (4 × 256 KB), the cover and the picture buffers are
in the 8 MB SDRAM. Run-time stack and heap use come from the `MEM` line
on the board (REQUIRES HARDWARE).
