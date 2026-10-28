# Hardware

| Item | Value | Status |
|---|---|---|
| Board | STM32F746G-DISCO (32F746GDISCOVERY), MB1191 | VERIFIED |
| MCU | STM32F746NGH6, Cortex-M7, run at 200 MHz | VERIFIED; clock measured against the LSE (12 ppm) |
| LCD | 4.3" 480×272, ROCKTECH RK043FN48H-CT672B, LTDC RGB565 | VERIFIED |
| Touch | FT5336 capacitive, I²C3 | VERIFIED |
| SDRAM | 8 MB usable on FMC bank 1 (`0xC0000000`) | VERIFIED (self-test) |
| USB to PC | USB OTG FS micro-USB connector, vendor HID `1209:0002` | VERIFIED as `1209:0001` (audio controller firmware); as `1209:0002` REQUIRES HARDWARE |
| Debug | ST-LINK/V2-1 (flash, SWD, virtual COM port) | VERIFIED |

## Peripherals in use

| Peripheral | Use |
|---|---|
| LTDC | LCD, two RGB565 framebuffers in SDRAM, swapped on vertical blanking |
| FMC | SDRAM |
| I²C3 | FT5336, polled (no interrupt) |
| USB OTG FS | Vendor HID `1209:0002` |
| USART1 | Log on the ST-LINK virtual COM port, 115200 8N1 |
| TIM5 | Clock measurement against the 32.768 kHz LSE at boot |

Pins and their verification status: [pin-mapping.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/pin-mapping.md).
GPIO and peripheral assignments come from UM1907, the MB1191 schematic and
the STM32CubeF7 BSP, never from guesses.

## Bring-up lessons

- **The LTDC must stay off while the SDRAM is initialised.** Its
  framebuffer reads made the SDRAM ignore the LOAD MODE REGISTER command;
  writes then smeared into neighbouring columns. The firmware enables the
  LTDC only after SDRAM init and a valid framebuffer.
- Two USB connectors: flashing and the log use **ST-LINK**; the PC link
  uses **USB OTG FS**.

The full log with dates and register values:
[docs/hardware.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/hardware.md).
