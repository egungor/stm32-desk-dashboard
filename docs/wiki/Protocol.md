# Protocol (v3)

Authoritative description: [protocol/protocol.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/protocol/protocol.md).
Both sides link the same C codec (`protocol/src/protocol.c`).

## Frame

Every message is one 64-byte frame, which is exactly one HID report.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | version `2` |
| 2 | 1 | command |
| 3 | 1 | sequence (per sender, wraps) |
| 4 | 1 | payload length, 0–57 |
| 5 | n | payload, then zero padding |
| 62 | 2 | CRC-16/CCITT-FALSE of bytes 0–61, low byte first |

## Commands

| Code | Command | Direction | Payload |
|---|---|---|---|
| `0x01` | `GET_DEVICE_LIST` | STM32 → host | – |
| `0x02` | `DEVICE_LIST` | host → STM32 | one device: generation, total, position, index, flags (default, favorite, alias follows), icon, Windows name (≤ 48 B UTF-8) |
| `0x03` | `SELECT_DEVICE` | STM32 → host | generation, device index |
| `0x04` | `DEVICE_CHANGED` | host → STM32 | index of the default, `0xFF` = none |
| `0x05`/`0x06` | `SET_VOLUME` / `VOLUME_CHANGED` | → host / → STM32 | 0–100 |
| `0x07`/`0x08` | `SET_MUTE` / `MUTE_CHANGED` | → host / → STM32 | 0 or 1 |
| `0x09` | `DEVICE_ALIAS` | host → STM32 | generation, position, alias (≤ 32 B UTF-8); part of the list |
| `0x0A` | `SET_ALIAS` | STM32 → host | generation, index, alias (empty removes it) |
| `0x0B` | `SET_FAVORITE` | STM32 → host | generation, index, 0 or 1 |
| `0x0C` | `MOVE_DEVICE` | STM32 → host | generation, index, new position |
| `0x0D`/`0x0E` | `SETTINGS` / `SET_SETTINGS` | → STM32 / → host | 11-byte settings record (layout, theme, accent, grid, tabs, timeout, flip, language, volume limit) |
| `0x7F` | `ERROR` | either | code, referenced command, referenced sequence |

A list of *n* devices is *n* `DEVICE_LIST` frames with the same
`generation`, in the user's order, plus one `DEVICE_ALIAS` frame per
device with an alias; the STM32 replaces its list only when all have
arrived. Aliases, order, favorites and settings are stored on the host
(ADR-004).

## Typical exchanges

```text
Board plugged in / host opens the board
  STM32 → GET_DEVICE_LIST            (repeated every 1 s until a list arrives)
  host  → DEVICE_LIST × n, DEVICE_ALIAS × a, DEVICE_CHANGED, VOLUME_CHANGED,
          MUTE_CHANGED, SETTINGS

User taps a device
  STM32 → SELECT_DEVICE generation index
  host  → DEVICE_CHANGED index       (after Windows switched)
       or ERROR UNKNOWN_DEVICE / OPERATION_FAILED

Windows changes
  host  → DEVICE_CHANGED             (default changed)
  host  → DEVICE_LIST × n, DEVICE_CHANGED   (devices added/removed/renamed)

User renames, stars or moves a device
  STM32 → SET_ALIAS / SET_FAVORITE / MOVE_DEVICE
  host  → DEVICE_LIST × n (new generation), DEVICE_CHANGED

User changes a setting
  STM32 → SET_SETTINGS record
  host  → SETTINGS record
```

## USB transport

Vendor HID, VID:PID `1209:0002` (pid.codes test range, development only),
usage page `0xFF00`, one 64-byte input and one 64-byte output report on
interrupt endpoints (5 ms), no report ID. On Windows the read/write buffers
are 65 bytes: report ID 0, then the frame.

## Changing the protocol

Any incompatible change needs a version bump, `protocol.md`, codec tests,
and both implementations. Version 2 added the generation to
`SELECT_DEVICE`, so a selection can no longer refer to an older list.
Version 3 adds the dashboard: `TIME`, cards, details and prompts sent as
chunked text records, `ACTION` (detail buttons, prompt answers, media
keys), and `startup_screen` in the settings record (ADR-005, ADR-006).
