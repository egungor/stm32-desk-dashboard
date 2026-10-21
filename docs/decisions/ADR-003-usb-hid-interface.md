# ADR-003: USB HID interface of the controller

## Status

Proposed — implemented in the firmware (issue #30); the Windows side
follows. To be confirmed after the first end-to-end test.

## Context

Protocol v1 (`protocol/protocol.md`) moves fixed 64-byte frames in both
directions and left the HID descriptor, identifiers and timing to M3.
CubeMX generated the ST demo HID class (a mouse, VID `0x0483` /
PID `0x572B`), which has a 4-byte IN endpoint and no OUT endpoint.

## Decision

**Identifiers.**

| Item | Value |
|---|---|
| VID / PID | `0x1209` / `0x0001` (pid.codes test range) |
| Manufacturer string | `stm32-audio-controller` |
| Product string | `STM32 Audio Controller` |

pid.codes reserves `1209:0001`–`000F` for private testing. Before the
device is given to anyone else it needs its own PID (pid.codes grants
them free to open-source hardware); only the two defines change.

**Interface.** One HID interface, class 3, subclass 0 (no boot), protocol
0, with:

- interrupt IN endpoint `0x81`, 64 bytes, 5 ms interval;
- interrupt OUT endpoint `0x01`, 64 bytes, 5 ms interval;
- report descriptor: usage page `0xFF00` (vendor), usage `0x01`, one
  application collection with a 64-byte input report (usage `0x02`) and
  a 64-byte output report (usage `0x03`), **no report ID**.

One report carries exactly one protocol frame. On Windows a report is
read and written as 65 bytes: report ID 0, then the frame. The host finds
the device by VID/PID and usage page `0xFF00`, so it never opens another
HID interface by mistake.

**Firmware placement.** The class is application code
(`App/Src/usb_link.c`). CubeMX keeps generating the demo HID class; the
`USB_DEVICE_Init_PreTreatment` USER CODE section of `usb_device.c`
initialises the stack with our class and returns before the generated
registration runs. Identifiers are redefined in the `PRIVATE_DEFINES`
USER CODE section of `usbd_desc.c`. Both survive regeneration.

**Timing** (in `protocol.md`, "Transport"):

- the STM32 repeats `GET_DEVICE_LIST` every 1 s until a complete list
  has arrived (the host also sends the list when it opens the device,
  #36);
- a `SELECT_DEVICE` without `DEVICE_CHANGED` or `ERROR` within 2 s means
  the host is not responding: the UI says so and the STM32 goes back to
  requesting the list.

## Alternatives

- **Switch the `.ioc` to CubeMX "Custom HID".** Cleaner on paper, but it
  needs CubeMX (not available in CI or the cloud sandbox), CubeMX's custom
  HID template still has to be filled in by hand in USER CODE, and its
  report size defaults would need overriding. Can still be done later
  without touching the protocol.
- **USB CDC (virtual COM port).** Needs no descriptor work, but Windows
  shows a COM port that other programs may grab, and the project decided
  on HID (`CLAUDE.md`).
- **Keep the ST VID with a new PID.** ST's PIDs are not ours to assign.

## Consequences

- The generated mouse class stays linked but unused (a few hundred bytes
  of flash).
- Windows needs no driver (HID class driver) and no administrator rights.
- Changing a report size or adding a report ID is a protocol change
  (version, documentation, tests).

## Update (2026-10-21, stm32-desk-dashboard #3)

This repository continues stm32-audio-controller as a new product. Its
board uses the pid.codes test PID `1209:0002`; `1209:0001` stays with the
audio controller, so neither host program opens the other product's
board. Everything else in this decision is unchanged.
