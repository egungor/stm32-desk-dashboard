# STM32 ↔ Windows Protocol

## Status

**Protocol version 7.** Version 2 (audio: device list, selection,
volume, mute, preferences and settings; ADR-004) plus the dashboard of
version 3: time, cards, details, prompts and actions sent as chunked
text records, and one more byte in the settings record
(`startup_screen`); plus, in version 4, what plays now: a media record,
the playback state and a small cover (ADR-008); plus, in version 5, a
short banner over every screen (notifications, ADR-009); plus, in
version 6, coloured cards, status icons and a picture (ADR-010); and, in
version 7, the picture at the screen's size, so a tap can show it full
screen (ADR-010, #53), and `SEEK` from the Music tab's progress bar
(ADR-008, #55). Design:
`docs/decisions/ADR-006-cards-and-protocol-v3.md`,
`docs/decisions/ADR-005-tabbed-shell-and-global-settings.md`,
`docs/decisions/ADR-008-media-session-and-music-tab.md`,
`docs/decisions/ADR-009-notifications.md` and
`docs/decisions/ADR-010-dashboard-redesign.md`.

This document is the source of truth for communication between the STM32
firmware and the Windows host application. The reference implementation
is `include/protocol.h` + `src/protocol.c` (portable C99, used by both
sides); `tests/test_protocol.c` checks it against this document.

## Transport

USB HID, Full Speed (design: `docs/decisions/ADR-003-usb-hid-interface.md`).

| Item | Value |
|---|---|
| VID / PID | `0x1209` / `0x0002` (pid.codes test range, development only; `0x0001` is stm32-audio-controller) |
| Interface | HID class, no boot subclass; vendor usage page `0xFF00`, usage `0x01` |
| Device → host | 64-byte input report on interrupt IN endpoint `0x81`, 5 ms interval |
| Host → device | 64-byte output report on interrupt OUT endpoint `0x01`, 5 ms interval |
| Report ID | none — the report is exactly one frame |

On Windows, `ReadFile` / `WriteFile` on the HID handle use 65-byte
buffers: byte 0 is the report ID (always 0), bytes 1–64 are the frame.

Timing, applied by the STM32:

| Rule | Value |
|---|---|
| `GET_DEVICE_LIST` repeated until a complete list has arrived | every 1000 ms |
| No `DEVICE_CHANGED` or `ERROR` after `SELECT_DEVICE`; no new list or `ERROR` after `SET_ALIAS`, `SET_FAVORITE`, `MOVE_DEVICE`; no `SETTINGS` or `ERROR` after `SET_SETTINGS` | 2000 ms → host not responding: notice, waiting screen, request the list again |
| A new list stays incomplete (a `DEVICE_LIST` or `DEVICE_ALIAS` frame was lost) while an older list is shown | 1000 ms after its last frame → `GET_DEVICE_LIST` again; the older list stays on screen |
| Edits (`SET_ALIAS`, `SET_FAVORITE`, `MOVE_DEVICE`) | one at a time |
| `SET_VOLUME` while the user drags the slider | at most every 100 ms, the latest value; the last value is always sent |
| A card, detail or prompt record stays incomplete | 1000 ms after its last part → `GET_CARDS` (card) or `OPEN_DETAIL` again (detail); an incomplete prompt is dropped |
| No complete detail after `OPEN_DETAIL`, and no `ERROR` | 2000 ms → notice, the board stays on the dashboard |
| The media record or the cover of the last `MEDIA_STATE` stays incomplete | 1000 ms after its last part → `GET_MEDIA` |
| A banner record stays incomplete | 1000 ms after its last part → dropped (a banner is never requested again) |
| The picture of the last `PICTURE` stays incomplete | 1000 ms after its last part (or the `PICTURE`) → `GET_CARDS` |
| `VOLUME_CHANGED` during a local volume change | not shown until 300 ms after the last `SET_VOLUME`; then the host's value is shown |

The host does not time out; it answers requests and reports changes.
These values do not change the frame format, so they are not a protocol
version change.

The protocol does not depend on a specific HID API.

## Goals

- Simple
- Deterministic
- Versioned
- Easy to debug
- Robust against malformed packets
- Suitable for small control messages

## Frame

All frames are 64 bytes. Multi-byte fields are little endian.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 1 | magic | `0xA5` |
| 1 | 1 | version | `6` |
| 2 | 1 | command | see message set |
| 3 | 1 | sequence | sender's counter, incremented per frame sent, wraps 255 → 0 |
| 4 | 1 | length | payload bytes, 0–57 |
| 5 | length | payload | per command |
| 5 + length | … | padding | zero |
| 62 | 2 | crc | CRC-16 of bytes 0–61 |

CRC: CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value `0xFFFF`,
no input/output reflection, no final XOR. Check value: `"123456789"` →
`0x29B1`. Stored low byte first at offset 62.

Senders must zero the padding. Receivers do not check it beyond the CRC.

### Example

`SELECT_DEVICE` with sequence `0x2A`, `generation` = 5, `device_index` = 2:

```text
offset 0..6:   A5 07 03 2A 02 05 02
offset 7..61:  00 (55 bytes)
offset 62..63: 8B 2B            (CRC 0x2B8B)
```

## Message set

| Code | Command | Direction | Payload |
|---|---|---|---|
| `0x01` | `GET_DEVICE_LIST` | STM32 → Host | none |
| `0x02` | `DEVICE_LIST` | Host → STM32 | one device entry (below) |
| `0x03` | `SELECT_DEVICE` | STM32 → Host | `generation`, `device_index` (0–254) |
| `0x04` | `DEVICE_CHANGED` | Host → STM32 | `device_index` of the current default (0–254), or `0xFF` = none |
| `0x05` | `SET_VOLUME` | STM32 → Host | `volume` (0–100, percent) |
| `0x06` | `VOLUME_CHANGED` | Host → STM32 | `volume` (0–100) |
| `0x07` | `SET_MUTE` | STM32 → Host | `muted` (0 or 1) |
| `0x08` | `MUTE_CHANGED` | Host → STM32 | `muted` (0 or 1) |
| `0x09` | `DEVICE_ALIAS` | Host → STM32 | `generation`, `position`, `alias_len` (1–32), `alias` (part of a device list, below) |
| `0x0A` | `SET_ALIAS` | STM32 → Host | `generation`, `device_index`, `alias_len` (0–32; 0 removes the alias), `alias` |
| `0x0B` | `SET_FAVORITE` | STM32 → Host | `generation`, `device_index`, `favorite` (0 or 1) |
| `0x0C` | `MOVE_DEVICE` | STM32 → Host | `generation`, `device_index`, `new_position` (0 … `total` − 1, below 32) |
| `0x0D` | `SETTINGS` | Host → STM32 | settings record (below) |
| `0x0E` | `SET_SETTINGS` | STM32 → Host | settings record (below) |
| `0x20` | `TIME` | Host → STM32 | `local_time` (u32, little endian): seconds since 1970-01-01 00:00 in Windows' local time |
| `0x21` | `CARD_PART` | Host → STM32 | part header (`id` = card slot 0–7) + record text (below) |
| `0x22` | `CARD_CLEAR` | Host → STM32 | `slot` (0–7): the slot is empty |
| `0x23` | `DETAIL_PART` | Host → STM32 | part header (`id` = card slot 0–7) + record text |
| `0x24` | `PROMPT_PART` | Host → STM32 | part header (`id` = prompt id 0–255) + record text |
| `0x25` | `PROMPT_CLOSE` | Host → STM32 | `prompt_id`: hide this prompt |
| `0x26` | `MEDIA_PART` | Host → STM32 | part header (`id` = 0) + media record text (version 4, below) |
| `0x27` | `MEDIA_STATE` | Host → STM32 | playback state, 11 bytes (version 4, below) |
| `0x28` | `ART_PART` | Host → STM32 | `art_id`, `index` (u16) + cover data (version 4, below) |
| `0x29` | `BANNER_PART` | Host → STM32 | part header (`id` = banner id 0–255) + banner record text (version 5, below) |
| `0x2A` | `INDICATORS` | Host → STM32 | status icons (version 6, below) |
| `0x2B` | `PICTURE_PART` | Host → STM32 | `pic_id`, `index` (u16) + picture data (version 6, below) |
| `0x2C` | `PICTURE` | Host → STM32 | `pic_id`: the picture to show, 0 = none (version 6, below) |
| `0x30` | `GET_CARDS` | STM32 → Host | none |
| `0x31` | `OPEN_DETAIL` | STM32 → Host | `slot` (0–7), `arg` (int8: 0 default, −1 previous, +1 next) |
| `0x32` | `ACTION` | STM32 → Host | `kind`, `ref`, `action` (below) |
| `0x33` | `GET_MEDIA` | STM32 → Host | none (version 4) |
| `0x34` | `SEEK` | STM32 → Host | `position` u32: seconds from the start of the track (version 7, below) |
| `0x7F` | `ERROR` | Either | `code`, `ref_command`, `ref_sequence` (1 byte each) |

Volume and mute refer to the current default playback device. After
`DEVICE_CHANGED` with `0xFF` (no default device) the STM32 treats them as
unknown; when a default device appears again the host sends
`VOLUME_CHANGED` and `MUTE_CHANGED` even if the values did not change.

### Device list

A device list is sent as `total` `DEVICE_LIST` frames, one device each, in
display order (the user's order, see "Preferences"), followed by one
`DEVICE_ALIAS` frame for every entry with flag bit 2. `DEVICE_LIST`
payload (7 + `name_len` bytes):

| Byte | Field | Value |
|---|---|---|
| 0 | `generation` | same in every frame of one list; the host changes it for each new list |
| 1 | `total` | number of devices, 0–32 |
| 2 | `position` | 0 … `total` − 1 |
| 3 | `device_index` | 0–254, assigned by the host |
| 4 | `flags` | bit 0: current default device; bit 1: favorite; bit 2: a `DEVICE_ALIAS` frame follows; bits 3–7: 0 |
| 5 | `icon` | 0 generic, 1 speakers, 2 headphones, 3 headset, 4 display, 5 digital output |
| 6 | `name_len` | 0–48 |
| 7 … | `name` | `name_len` bytes of UTF-8, no NUL bytes, no terminator; the Windows name, also when an alias is set |

`DEVICE_ALIAS` payload (3 + `alias_len` bytes):

| Byte | Field | Value |
|---|---|---|
| 0 | `generation` | the list it belongs to |
| 1 | `position` | the entry's `position` (0–31) |
| 2 | `alias_len` | 1–32 |
| 3 … | `alias` | `alias_len` bytes of UTF-8, no NUL bytes |

- An empty list is one frame with `total` = 0, `position` = 0,
  `device_index` = `0xFF`, `flags` = 0, `icon` = 0, `name_len` = 0.
- The host truncates names to 48 bytes at a UTF-8 character boundary.
- The firmware assembles the list by `position` and replaces its list
  only when all `total` entries of one `generation` and all aliases they
  announce have arrived. A `DEVICE_LIST` frame with a different
  `generation` discards a partially received list; a `DEVICE_ALIAS` frame
  of another `generation` is ignored.
- The host truncates aliases to 32 bytes at a UTF-8 character boundary.
  The firmware shows the alias instead of the name when there is one.
- The host sends a list in reply to `GET_DEVICE_LIST` and whenever the set
  of playback devices or their names change.

## Device identity

Windows endpoint IDs are opaque strings and never leave the host.

The host assigns each device a session-local `device_index` (0–254) and
keeps the mapping to the Windows endpoint ID. An index is valid until the
next device list; the firmware only echoes indices it received.

Indices belong to the session that sent them. A session starts when the
host sends the list after it has (re)opened the device, on its own or in
answer to `GET_DEVICE_LIST`. Until then the host answers `SELECT_DEVICE`
with `ERROR UNKNOWN_DEVICE`, which makes the STM32 request the list: its
indices may come from an earlier session (for example, the host program
was restarted while the board stayed connected) and could name another
device now.

Every request about a device (`SELECT_DEVICE`, `SET_ALIAS`,
`SET_FAVORITE`, `MOVE_DEVICE`) carries the `generation` of the list the
STM32 shows. The host answers `ERROR UNKNOWN_DEVICE` unless it is the
list the host sent last in this session, so an index from an earlier
session or an older list is never applied to another device. (Version 1
lacked this; a selection sent while a new list was on its way could be
read against the new list.)

## Preferences

Aliases, the display order and favorites are stored on the host, keyed by
the Windows endpoint ID, and survive USB reconnects and host restarts
(ADR-004). A device that is not present keeps them; new devices are
appended to the order. The board stores nothing.

Settings record (12 bytes, payload of `SETTINGS` and `SET_SETTINGS`):

| Byte | Field | Values | Default |
|---|---|---|---|
| 0 | `layout` | 0 cards, 1 list, 2 large tile | 0 |
| 1 | `theme` | 0 dark, 1 light | 0 |
| 2 | `accent` | 0 orange, 1 blue, 2 green, 3 purple, 4 pink, 5 yellow | 0 |
| 3 | `grid` | 0 2×2, 1 2×3, 2 3×3, 3 list (cards layout) | 0 |
| 4 | `startup_tab` | 0 last used, 1 all, 2 favorites | 0 |
| 5 | `last_tab` | 0 all, 1 favorites | 0 |
| 6 | `screen_timeout` | minutes until the backlight turns off: 0 never, 1–60 | 0 |
| 7 | `flipped` | 0 upright, 1 turned by 180° | 0 |
| 8 | `language` | 0 Turkish, 1 English | 0 |
| 9 | `volume_limit_on` | 0 off, 1 on | 0 |
| 10 | `volume_limit` | 0–100 percent | 50 |
| 11 | `startup_screen` | 0 Dashboard, 1 Audio (ADR-005; version 3), 2 Music (ADR-008; version 4) | 0 |

The STM32 uses the defaults until the host's `SETTINGS` arrive. The host
stores the record as received and interprets only `volume_limit_on` and
`volume_limit` (volume limit on switch: a device the STM32 selects is
lowered to it first, ADR-004).

## Dashboard records

Cards, details and prompts (ADR-006) are UTF-8 text records sent in
parts. The board draws them with generic components; it does not know
what a record is about.

### Record text

Lines end with `\n` (`0x0A`); fields within a line are separated by
`0x1F`. Neither byte appears inside a field (the host replaces them by a
space), and no record holds a NUL byte. Empty fields are allowed;
missing trailing fields count as empty.

| Record | Max bytes | Lines |
|---|---|---|
| Card | 256 | 1: `icon` ␟ `title` ␟ `badge` ␟ `bg` ␟ `flags` (version 6) ␟ `frame` ␟ `icon colour` (2026-10-27); 2–5: text lines (5: 2026-10-27; an older board shows 2–4) |
| Detail | 2048 | 1: `title` ␟ `subtitle` ␟ `flags`; then rows, section titles and buttons. `flags`: letters, `n` = show previous / next (`OPEN_DETAIL` −1 / +1) in the header, `c` = a clear button at the right of the header (`ACTION` 255); others are ignored |
| Prompt | 1024 | 1: `title` ␟ `subtitle` ␟ `seconds` (countdown, decimal; empty = none) ␟ `bg` ␟ `frame` ␟ `flags` ␟ `icon` (2026-10-27); then as a detail |

Card fields of version 6 (ADR-010): `bg` is the card's background as six
hex digits `RRGGBB` (empty: the theme's card colour; the board picks white
or dark text by its brightness); `flags` are letters, `L` = the first text
line in the large font. `frame` (RRGGBB, empty: none) draws a 4 px
gold-leaf frame: a vertical gradient of the colour, lightened at the top
and darkened at the bottom, around the card; `icon colour` (RRGGBB, empty:
the text's) paints the card's icon. An empty `title` is not drawn; a card
without a title shows its `badge` below the icon, so that four lines fit. The board shows the
cards of slots 0 and 1 (the bottom row is its own: now playing and the
picture); the details of every slot stay available.

Prompt fields of 2026-10-27 (ADR-010 amendment): `bg` (RRGGBB) paints
the panel, its text white or dark by brightness; with the flag `s` only a
title strip (and the panel's border) is in `bg`; `frame` (RRGGBB) draws
the gold-leaf frame around the panel; `icon` is the title's icon number
(empty: the warning sign). Empty fields keep the theme's look.

Detail and prompt lines after the first:

- a row: `icon` ␟ `label` ␟ `value` ␟ `colour` (RRGGBB, empty: the
  theme's; paints the icon, the label and the value);
- a section title: a line starting with `#` (the rest is the title);
- a button: a line starting with `!`: `!action` ␟ `label`, `action`
  decimal 1–255; at most 3 buttons, drawn at the bottom in their order;
- a band (2026-10-27): `*icon` ␟ `big` ␟ `text` ␟ `side` ␟ `bg`: the
  icon at 40 px, the big text (22 px) with the text below it, the side text
  on the right, on `bg` (RRGGBB; empty: the card colour);
- tiles (2026-10-27): `=label` ␟ `value` ␟ `label` ␟ `value` …: up to
  four small tiles in a row, the label small above the value;
- a strip (2026-10-27): consecutive lines `>label` ␟ `icon` ␟ `big` ␟
  `small` ␟ `note` are tiles in one strip that scrolls sideways (label,
  icon, big text, small text, the note in the accent colour).

An older board draws band, tiles and strip lines as rows (their first
field taken as an icon it does not know): readable, not designed; the
weather detail uses them (ADR-010 amendment of 2026-10-27).

`icon` is a decimal number from the table below. The board draws nothing
for 0, an empty field, or a number it does not know, so numbers can be
added without a new protocol version. The board cuts text that does not
fit with "…" and never wraps a card line.

| Icon | Meaning | Glyph (Font Awesome 5) |
|---|---|---|
| 0 | none | |
| 1 | clear, day | sun |
| 2 | clear, night | moon |
| 3 | partly cloudy, day | cloud-sun |
| 4 | partly cloudy, night | cloud-moon |
| 5 | overcast | cloud |
| 6 | fog | smog |
| 7 | drizzle, rain, showers | cloud-rain |
| 8 | heavy or freezing rain, violent showers | cloud-showers-heavy |
| 9 | snow | snowflake |
| 10 | thunderstorm | bolt |
| 11 | menu of the day | utensils |
| 12 | notifications | bell |
| 13 | Claude Code | the Claude symbol (#59; a robot before) |
| 14 | a liked dish | star |
| 15 | a dish never wanted | times (cross) |
| 16 | a dish not liked | exclamation-triangle |

(Numbers are added here with the features that use them; 1–10: weather,
#18; 11: menu, #20; 12: notifications, #34; 13: Claude Code, #47; 14–16:
the food lists, ADR-010 amendment of 2026-10-27; 14–16 are drawn in detail
and prompt rows, not as a card's 40 px icon.) A detail row whose `icon` field is not empty keeps an icon column,
also for 0, so that labels of rows with and without an icon line up; a
row with an empty field has no column.

### Part header

`CARD_PART`, `DETAIL_PART`, `PROMPT_PART`, `MEDIA_PART` and
`BANNER_PART` start with:

| Byte | Field | Value |
|---|---|---|
| 0 | `id` | card slot 0–7 (card, detail), prompt or banner id 0–255, or 0 (media) |
| 1 | `generation` | changed by the host for every new version of the record |
| 2 | `part` | 0 … `parts` − 1 |
| 3 | `parts` | 1–5 (card), 1–39 (detail), 1–20 (prompt), 1–10 (media), 1–5 (banner) |
| 4 … | data | part `n` holds record bytes `53·n` … ; every part but the last has 53 bytes |

- The last part has 1–53 bytes; only an empty record (`parts` = 1) has
  an empty part. A part that would end beyond the record's maximum size
  is invalid (`BAD_PAYLOAD`).
- The receiver collects the parts of one `id` and `generation` (in any
  order) and uses the record only when all `parts` have arrived. A part
  of another `id` or `generation` discards a partial record; a repeated
  part is ignored.
- The host sends one record completely before it starts the next.

## Now playing (version 4)

What plays on the PC (ADR-008): the Music tab and the Dashboard's media
strip draw it. Three host messages: the media record (texts), the
playback state, and the cover.

### Media record

`MEDIA_PART` carries one text record (the rules of "Record text" and
"Part header"; `id` is always 0), at most 512 bytes, one line:

`title` ␟ `artist` ␟ `album` ␟ `app`

`app` is the name of the program that plays (e.g. "Spotify"). Empty
fields are allowed.

### `MEDIA_STATE`

| Byte | Field | Value |
|---|---|---|
| 0 | `status` | 0 no media session, 1 stopped, 2 playing, 3 paused |
| 1 | `controls` | bit 0 previous, bit 1 play/pause, bit 2 next, bit 3 seek (version 7): the session takes them; other bits 0 |
| 2 | `art_id` | 0 no cover; 1–255 the cover sent in `ART_PART`s with this id |
| 3–6 | `position` | u32, seconds from the start of the track |
| 7–10 | `duration` | u32, seconds; 0 = unknown. When not 0, `position` ≤ `duration` |

With `status` 0 the board shows that nothing plays and ignores the media
record. While `status` is 2 the board counts `position` on by itself;
the host sends a new `MEDIA_STATE` when the status, the controls, the
cover, the duration or the track change, when the position jumps, and
every 30 s while playing.

### `SEEK` (version 7)

Four bytes, `position` u32 little endian: jump to this many seconds from
the start of the playing track (the board sends it when the user taps or
drags the Music tab's progress bar, only while `controls` bit 3 is set).
No answer: the board shows the new position at once and counts on from
it; the host asks the player to jump (a position beyond `duration` is
cut to it) and for 3 s keeps counting from the new position, so a late
timeline does not pull the bar back; afterwards the usual `MEDIA_STATE`
rules correct the bar if the player did not follow. Without a media
session, or when it cannot seek, the host answers `ERROR
OPERATION_FAILED`; the board then shows a notice and sends `GET_MEDIA`.

### Cover

A cover is 120 × 120 pixels, RGB565 (bits 15–11 red, 10–5 green, 4–0
blue), each pixel little endian, row by row from the top left: 28 800
bytes, sent as 534 `ART_PART`s:

| Byte | Field | Value |
|---|---|---|
| 0 | `art_id` | 1–255 |
| 1–2 | `index` | u16, 0–533; part `n` holds cover bytes `54·n` … |
| 3 … | data | 54 bytes; 18 in part 533 |

- A part whose data length is not exactly 54 (18 for part 533) is
  invalid (`BAD_PAYLOAD`).
- The board collects the parts of the `art_id` of the last
  `MEDIA_STATE` (in any order; a repeated part is ignored) and shows the
  cover when all 534 have arrived. Parts of another `art_id` are
  dropped.
- Other frames may come between two `ART_PART`s; the host sends cover
  parts after anything else it has to send, so a cover takes about
  2.7 s at the 5 ms interval. A new `art_id` replaces a cover still
  being sent.

## Banner (version 5)

A short strip over the top of every screen (ADR-009): an icon, a title,
up to two text lines and a countdown. The host uses it for a new
notification; the board does not know that.

`BANNER_PART` carries one text record (the rules of "Record text" and
"Part header"), at most 256 bytes:

- line 1: `icon` ␟ `title` ␟ `seconds` ␟ `slot`;
- lines 2–3: text lines; further lines are ignored.

| Field | Value |
|---|---|
| `icon` | a number of the icon table; 0 or empty = none |
| `seconds` | how long it stays, decimal 1–30; empty, 0 or above 30 = 5 |
| `slot` | card slot 0–7 whose detail a tap opens (as a tap on that card); empty or another value = a tap only closes the banner |

- A newer banner (another `id` or `generation`) replaces the one shown
  and starts its countdown again.
- A banner never covers a prompt: while a prompt is shown, the board
  drops banners; a prompt that appears closes a shown banner.
- A banner does not wake a dark screen (screen timeout, ADR-004) and is
  not shown on it.
- There is no answer and no `ACTION`; a tap closes it and, on the
  board's main screen, opens the detail with `OPEN_DETAIL slot, 0` as a
  card does (on the settings or edit screens a tap only closes it).

## Status icons and picture (versions 6 and 7)

### `INDICATORS`

Status icons at the right of the clock strip (ADR-010), in their order:

| Byte | Field | Value |
|---|---|---|
| 0 | `count` | 0–4 |
| 1 + 4·i | `icon` | icon number ("Icon") |
| 2 + 4·i | `colour` | 0 normal, 1 red, 2 yellow, 3 green |
| 3 + 4·i | `number` | shown next to the icon, 0 = none (above 99: "99+") |
| 4 + 4·i | `slot` | the detail a tap opens (`OPEN_DETAIL slot, 0`), 0–7, or `0xFF` = none |

The payload is exactly `1 + 4·count` bytes. The host sends it after the
cards on `GET_CARDS` (and when it opens the device) and whenever it
changes; each one replaces the last.

### Picture

The picture has the screen's size, 480 × 272 pixels (version 7; 200 × 92
in version 6), RGB565 as the cover: 261 120 bytes in 4 836
`PICTURE_PART`s with the layout of `ART_PART` (`pic_id` 1–255, `index`
u16 0–4835, 54 data bytes; 30 in part 4835; a part of another length is
`BAD_PAYLOAD`). The picture card shows it scaled down and cropped at the
edges; a tap on the card shows it full screen. At one report per 4 ms
(bInterval 5 ms, ADR-003) a picture takes about 20 s. `PICTURE` (one byte, `pic_id`) names
the picture to show; 0 = none (the card shows a placeholder). The board
collects the parts of the last `PICTURE`'s id (in any order; repeats
ignored; parts of another id dropped) and shows it when all have arrived;
a picture it shows already is not collected again. The host sends
`PICTURE` after `INDICATORS` on `GET_CARDS` and when the picture changes,
then its parts after anything else (as cover parts).

### `ACTION`

| `kind` | `ref` | `action` |
|---|---|---|
| 0 card detail | card slot 0–7 | the button's `action` (1–255); 255 also: the header's clear button (flag `c`) |
| 1 prompt | prompt id | the button's `action` (1–255) |
| 2 media strip, Music tab | 0 | 1 previous track, 2 play/pause, 3 next track |

`ACTION` has no direct answer; its effect comes back as cards, a detail
or `PROMPT_CLOSE`. The host answers an `ACTION` it cannot carry out with
`ERROR OPERATION_FAILED`.

## Exchanges

| Trigger | Frames |
|---|---|
| Firmware starts or USB (re)connects | STM32 → `GET_DEVICE_LIST`; Host → `DEVICE_LIST` × total (with its `DEVICE_ALIAS` frames), then `DEVICE_CHANGED`, `VOLUME_CHANGED`, `MUTE_CHANGED`, `SETTINGS` |
| Host opens the device (host program starts, or USB (re)connects) | Host → `DEVICE_LIST` × total, then `DEVICE_CHANGED`, `VOLUME_CHANGED`, `MUTE_CHANGED`, `SETTINGS`, without a request |
| User selects a device | STM32 → `SELECT_DEVICE`; Host → `DEVICE_CHANGED` after Windows switched, or `ERROR` |
| Default device, volume or mute changed in Windows | Host → `DEVICE_CHANGED` / `VOLUME_CHANGED` / `MUTE_CHANGED` |
| Devices added, removed or renamed | Host → new `DEVICE_LIST` (new `generation`), then `DEVICE_CHANGED` |
| User changes volume or mute | STM32 → `SET_VOLUME` / `SET_MUTE`; Host → `VOLUME_CHANGED` / `MUTE_CHANGED`, or `ERROR OPERATION_FAILED` |
| User edits an alias, a favorite or the order | STM32 → `SET_ALIAS` / `SET_FAVORITE` / `MOVE_DEVICE`; Host stores it → new list (new `generation`, also when nothing changed), then `DEVICE_CHANGED`; or `ERROR UNKNOWN_DEVICE` (stale `generation` or index), `BAD_PAYLOAD` (`new_position` ≥ `total`), `OPERATION_FAILED` (not stored) |
| User changes a setting (or the tab) | STM32 → `SET_SETTINGS` (whole record); Host stores it → `SETTINGS` |
| Preferences changed on the host (e.g. `dashboard-host alias`) | Host → new list if it changed, then `SETTINGS` |
| Firmware starts or USB (re)connects (dashboard) | STM32 → `GET_CARDS`; Host → `TIME`, then for every slot 0–7 its `CARD_PART`s or `CARD_CLEAR`, then `INDICATORS`, `PICTURE` and the picture's `PICTURE_PART`s |
| A status icon changes (a new toast, a Claude Code request, a list opened) | Host → `INDICATORS` |
| The user picks another picture on the PC | Host → `PICTURE` (new `pic_id`), then its `PICTURE_PART`s |
| Every 10 minutes, and when Windows' time or time zone changes | Host → `TIME` |
| Firmware starts or USB (re)connects (now playing) | STM32 → `GET_MEDIA`; Host → `MEDIA_PART`s, `MEDIA_STATE`, then the cover's `ART_PART`s if `art_id` is not 0 |
| What plays changes on the PC | Host → `MEDIA_PART`s if the texts changed, `MEDIA_STATE`, then the `ART_PART`s of a new cover |
| User taps or drags the Music tab's progress bar | STM32 → `SEEK`; Host → nothing, or `ERROR OPERATION_FAILED` (STM32 → `GET_MEDIA`) |
| A card changes | Host → its `CARD_PART`s with a new `generation`, or `CARD_CLEAR` |
| User taps a card | STM32 → `OPEN_DETAIL slot, 0`; Host → `DETAIL_PART`s, or `ERROR OPERATION_FAILED` (no detail for that slot) |
| User taps ‹ / › in a detail | STM32 → `OPEN_DETAIL slot, −1 / +1`; Host → the new detail |
| The open detail changes on the host | Host → its `DETAIL_PART`s, unasked |
| User taps a detail button or the media strip | STM32 → `ACTION` |
| Host needs a decision (ADR-007) | Host → `PROMPT_PART`s; the board shows the prompt over every screen and wakes a dark screen |
| User answers the prompt | STM32 → `ACTION kind 1`; Host → `PROMPT_CLOSE` |
| Something new for a short notice (a notification, ADR-009) | Host → `BANNER_PART`s; the board shows the banner unless a prompt is shown or the screen is dark |
| User taps a banner with a `slot` | STM32 → `OPEN_DETAIL slot, 0` (as a card tap) |
| The prompt is answered elsewhere or times out | Host → `PROMPT_CLOSE` |

The board shows one prompt at a time; the host keeps further prompts
queued until the shown one is closed.

The firmware shows the state reported by the host. It does not mark a
device as active after `SELECT_DEVICE` until `DEVICE_CHANGED` arrives,
and shows an edit only with the new list. Settings are the exception: the
STM32 applies a changed record at once and sends it; a record changed
while no host was connected is sent when the host's `SETTINGS` arrive,
instead of applying the host's.
It accepts a `DEVICE_LIST` at any time, requested or not. When the board
is plugged in, its request and the host's own list can cross; the second
complete list simply replaces the first.

Retries and timeouts: see "Transport".

## Errors and validation

A receiver checks, in this order, and stops at the first failure:

1. magic is `0xA5`;
2. version is `6`;
3. CRC matches;
4. length ≤ 57;
5. command is known;
6. payload length and every field value match the rules above.

A rejected frame is dropped. The receiver may answer with `ERROR`:

| Code | Name | Meaning |
|---|---|---|
| `0x01` | `BAD_FRAME` | magic, CRC or length wrong |
| `0x02` | `UNSUPPORTED_VERSION` | version is not 7 |
| `0x03` | `UNKNOWN_COMMAND` | command code not in the message set |
| `0x04` | `BAD_PAYLOAD` | payload length or a field value invalid |
| `0x05` | `UNKNOWN_DEVICE` | `device_index` not in the current list |
| `0x06` | `OPERATION_FAILED` | valid request, but it could not be carried out (e.g. Windows refused) |
| `0x07` | `BUSY` | request cannot be handled now |

`ref_command` and `ref_sequence` name the frame being answered (0 when
none). A receiver never answers an `ERROR` frame with another `ERROR`.

## Compatibility

Any incompatible protocol change requires:

1. version update;
2. protocol documentation update;
3. protocol tests;
4. firmware and host implementation updates.

A receiver rejects frames of any other version (`UNSUPPORTED_VERSION`).

## History

| Version | Change |
|---|---|
| 0 | Draft: message list and a header proposal (magic, version, command, 16-bit length). |
| 1 | Fixed 64-byte frame with sequence byte, 8-bit length and CRC-16; payloads, device list format, error codes and validation rules defined. |
| 2 | ADR-004: `DEVICE_ALIAS`, `SET_ALIAS`, `SET_FAVORITE`, `MOVE_DEVICE`, `SETTINGS`, `SET_SETTINGS`; favorite and alias flags in `DEVICE_LIST`; list in the user's order; `generation` in `SELECT_DEVICE`. |
| 3 | ADR-005, ADR-006: `TIME`, `CARD_PART`, `CARD_CLEAR`, `DETAIL_PART`, `PROMPT_PART`, `PROMPT_CLOSE`, `GET_CARDS`, `OPEN_DETAIL`, `ACTION`; chunked text records; settings record 12 bytes (`startup_screen`). stm32-desk-dashboard only; the audio controller stays on version 2. |
| 4 | ADR-008: `MEDIA_PART`, `MEDIA_STATE`, `ART_PART`, `GET_MEDIA` (now playing, 120 × 120 RGB565 cover); `startup_screen` 2 = Music. |
| 5 | ADR-009: `BANNER_PART` (a short banner over every screen, used for new notifications); icon 12 = bell. |
| 6 | ADR-010: `INDICATORS`, `PICTURE`, `PICTURE_PART` (200 × 92 RGB565); card record fields `bg` and `flags`; icon 13 = robot. |
| 7 | ADR-010 amendment: the picture at 480 × 272 (#53). ADR-008 amendment: `SEEK`, `MEDIA_STATE` `controls` bit 3 (#55). Later, without a new version: detail flag `c` (the header's clear button, `ACTION` 255; an older board ignores the flag); ADR-010 amendment of 2026-10-27: card fields `frame` and `icon colour`, row field `colour`, prompt fields `bg`, `frame`, `flags`, `icon`, icons 14–16 (an older board ignores the fields and draws no unknown icon). |
