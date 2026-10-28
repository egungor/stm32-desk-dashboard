# Protocol (v7)

Authoritative description: [protocol/protocol.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/protocol/protocol.md).
Both sides link the same C codec (`protocol/src/protocol.c`).

## Frame

Every message is one 64-byte frame, which is exactly one HID report.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | version `7` |
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
| `0x0D`/`0x0E` | `SETTINGS` / `SET_SETTINGS` | → STM32 / → host | 12-byte settings record (layout, theme, accent, grid, tabs, timeout, flip, language, volume limit, start screen: Dashboard, Audio, Music) |
| `0x20` | `TIME` | host → STM32 | local time, seconds since 1970 |
| `0x21`/`0x22` | `CARD_PART` / `CARD_CLEAR` | host → STM32 | a card record in parts (slot 0–7), or an empty slot |
| `0x23` | `DETAIL_PART` | host → STM32 | the detail record of a card, in parts |
| `0x24`/`0x25` | `PROMPT_PART` / `PROMPT_CLOSE` | host → STM32 | a prompt over every screen (Claude Code approvals), and its end |
| `0x26` | `MEDIA_PART` | host → STM32 | what plays: title, artist, album, app (v4) |
| `0x27` | `MEDIA_STATE` | host → STM32 | playing / paused / none, enabled controls, cover id, position, duration (v4) |
| `0x28` | `ART_PART` | host → STM32 | one of 534 parts of a 120 × 120 RGB565 cover (v4) |
| `0x29` | `BANNER_PART` | host → STM32 | a short banner over every screen: a new notification (v5) |
| `0x2A` | `INDICATORS` | host → STM32 | status icons at the top right: icon, colour, number, the detail a tap opens (v6) |
| `0x2B` | `PICTURE_PART` | host → STM32 | one of 4836 parts of the 480 × 272 RGB565 picture (v6; 200 × 92 until v7) |
| `0x2C` | `PICTURE` | host → STM32 | the picture to show, 0 = none (v6) |
| `0x30` | `GET_CARDS` | STM32 → host | time and all cards, please |
| `0x31` | `OPEN_DETAIL` | STM32 → host | slot, previous / next |
| `0x32` | `ACTION` | STM32 → host | a detail or prompt button, or a media control (previous, play/pause, next) |
| `0x33` | `GET_MEDIA` | STM32 → host | what plays, please (v4) |
| `0x34` | `SEEK` | STM32 → host | jump to a position in the track: the Music tab's progress bar (v7) |
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

Dashboard after connect
  STM32 → GET_CARDS, GET_MEDIA
  host  → TIME, CARD_PART × parts (slots 0, 1), INDICATORS,
          MEDIA_PART, MEDIA_STATE, ART_PART × 534, PICTURE, PICTURE_PART × 4836

User taps the weather card
  STM32 → OPEN_DETAIL slot 0
  host  → DETAIL_PART × parts          (or ERROR: "Ayrıntı alınamadı")

Claude Code asks for a permission
  host  → PROMPT_PART × parts          (over every screen)
  STM32 → ACTION kind 1 (prompt), ref = prompt id, action 1 / 2 / 3
  host  → PROMPT_CLOSE id

A new Windows notification
  host  → BANNER_PART × parts, INDICATORS (the bell's number)
```

## Records

Cards, details, prompts, banners and the media record are UTF-8 text:
lines end with `\n`, fields are separated by `0x1F` (␟). They are sent in
parts of 53 bytes behind a small header (record id, part, parts); the
board shows a record only when all its parts have arrived.

```text
card:    11␟␟␟3E9B5F␟          icon 11 (menu), no title, no badge, green, no flags
         Izgara köfte             four text lines
         Bulgur pilavı
         Mercimek çorba
         Sütlaç

prompt:  Claude Code izin istiyor␟desk-dashboard · 1/1␟60
         ␟Araç␟Bash                a row: icon ␟ label ␟ value
         ␟Komut␟git push origin …
         !1␟Onayla                  a button: !action ␟ label
         !2␟Reddet
         !3␟Terminalde sor
```

Icons are numbers (1–10 weather, 11 menu, 12 bell, 13 Claude Code,
14–16 the food lists); the board draws nothing for a number it does not
know, so new icons need no new version. The full grammar, with bands,
tiles and the sideways strip of the weather detail, is in
[protocol.md, "Record text"](https://github.com/egungor/stm32-desk-dashboard/blob/main/protocol/protocol.md#record-text).

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
Version 4 adds now playing (ADR-008), 5 the banner (ADR-009), 6 status
icons, card colours and the picture (ADR-010), 7 the picture at the
screen's size and `SEEK`. Fields added later without a new version (the
gold-leaf frame, row colours, icons 14–16) are ignored by an older board.

The settings window's About page warns when the board and the program
speak different versions.
