# Hardware Test Checklist

All of these checks ran on the board and on Windows on 2026-10-14 (#48);
the results are in the bring-up log of
[docs/hardware.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/hardware.md).
Keep this list as the procedure for re-testing after firmware or host
changes. Work top to bottom; each step needs the previous one. Record
each result (date, firmware commit, PASS/FAIL, the log line) in
`docs/hardware.md` and in the issue.

How to build, flash and read the log: [Getting Started](Getting-Started).

## 1. Clock — #16

Default firmware, read the boot log.

- [ ] `CLOCK CONFIG OK sysclk=200000000 …`
- [ ] `CLOCK MEASURE PASS ref=LSE … ppm=<n>` with |n| of a few tens or less

## 2. Touch — #18

Firmware built with `-DAPP_TEST_SCREEN=ON`.

- [ ] `TOUCH INIT OK id=0x51`
- [ ] Touch each of the five targets (four corners, centre): the
      `TOUCH DOWN x= y=` values are within a few pixels of the
      `TOUCH TARGET n=… x= y=` lines
- [ ] The dot follows the finger; `TOUCH UP` on release

## 3. UI on the LCD — #24

Firmware built with `-DAPP_UI_DEMO=ON` (no PC needed).

- [ ] `UI INIT OK fb0=0xC0000000 fb1=0xC0040000`, `UI DEMO START`
- [ ] The screens look like the simulator screenshots; Turkish letters
      and icons are right
- [ ] Tapping a device: pending mark, then active (`UI DEMO ACTIVE`)
- [ ] Page arrows work; no tearing or flicker
- [ ] Hold the flip button (top right) for 1 s: image and touch turn by
      180° and stay upright; hold again to turn back; `UI FLIP off
      copy_max_ms=<n>` with n of about 5 ms

## 4. USB HID — #30

Default firmware, board connected to the PC through **USB OTG FS**.

- [ ] `USB CONFIGURED` in the log
- [ ] Device Manager shows a HID device with VID `1209`, PID `0001`

## 5. Windows audio — #28

`dashboard-host` on Windows, no board needed.

- [ ] `list` matches Settings → Sound (devices and default)
- [ ] `set-default <n>` switches; Settings → Sound follows
- [ ] `volume` and `mute` work
- [ ] `watch`: one line per change of default or volume, no repeats; a
      new list when a device is plugged in or removed

## 6. End to end

Default firmware, `dashboard-host run`.

- [ ] Start `run`, then plug in the board: `Controller connected`, the
      list goes out, the LCD shows the same devices and default
- [ ] Tap another device: `<- SELECT_DEVICE`, `-> DEVICE_CHANGED`;
      Settings → Sound follows
- [ ] Change the default in Windows: the LCD follows
- [ ] Plug in / remove a device (headphones, HDMI): the LCD list updates,
      other devices keep their place
- [ ] Unplug the board: `Controller disconnected.`, waiting screen;
      plug it back in: the list returns within about 2 s
- [ ] Restart `run` with the board plugged in, after changing the default
      in Windows meanwhile: the LCD shows the current state without a tap
- [ ] Drag the volume slider: a few `<- SET_VOLUME` lines (not one per
      pixel), Windows follows, the knob does not jump back
- [ ] Tap mute: `-> MUTE_CHANGED`, the icon changes; change volume and
      mute in Windows: the LCD follows
- [ ] Ctrl+C: `send_failures=0 rejected=0 dropped=0`

## 7. Also open

- [ ] Boot after a power cycle (SDRAM from cold)
- [ ] LCD screenshots of the final UI into `docs/images/` (framebuffer
      dump + `tools/fb2png.py`)
