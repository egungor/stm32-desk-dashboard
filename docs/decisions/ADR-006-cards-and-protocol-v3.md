# ADR-006: Cards, details and prompts; protocol version 3

## Status

Accepted (issue #5, PR #7). Protocol, codec and tests: step 3 (#8).
Host routing and `DashController`: step 5 (#12). The board components:
step 6 (`docs/roadmap.md`).

Notes from step 5 (#12): `FrameRouter` decodes once and calls
`HostController::onMessage` / `DashController::onMessage`;
`HostController::onFrame` stays for code that runs it alone (the
`watch` command). The single sequence counter is a `SequencedSink` in
front of the transport. `TIME` also goes out when the local clock jumps
by more than 2 s against the last value sent (time zone, daylight
saving), without a Windows message hook.

Notes from step 6 (#14): the detail's first line got a third field,
`flags` (`n` = show previous / next), so the board shows ‹ › only where
the host can answer them (the menu, not the weather). Missing trailing
fields count as empty, so this needs no protocol version change. The
board's two clients share one sequence counter by numbering from the
audio client's counter (`Dash_SetSequenceCounter`), not by renumbering
frames in the port: the audio client matches ERROR answers by their
`ref_sequence`. The firmware keeps the four shown cards and the open
detail in the UI (about 3 KB) and one card and one detail being
assembled in `dash_client.c` (about 2.3 KB).

Notes from step 8 (#18, weather): nlohmann/json 3.11.3 is vendored as
`host/third_party/nlohmann/json.hpp` (with its MIT licence) and included
as a system header of `host_core` only. `host_core` and `test_host` are
compiled with `/utf-8` on MSVC, as the Windows programs already were, so
the Turkish texts of the host can be written as UTF-8. The board got the
icon numbers 1–10 (weather, `protocol.md` "Icon"); a detail row with an
icon field keeps an icon column. HTTP runs on a worker thread
(`BackgroundFetch`); the service loop only polls its result.

## Context

The Dashboard tab (ADR-005) shows a clock, four cards on a fixed 2×2
grid and a media strip. A tap on a card opens a detail screen; a Claude
Code permission request opens a prompt over any screen (ADR-007). v1
content: weather (card + 10 days), menu of the day (card + day detail
with the salad bar), Claude Code approvals; notifications take the
fourth card after v1 (decisions of 2026-10-21).

The project rule is **thin firmware, content from the host**: features
change often, firmware updates are slow (flash over ST-LINK), and the
host already knows the data. The board should only know how to *draw* a
few generic things, never what a weather report or a menu is.

Protocol v2 (ADR-004) has 64-byte frames with at most 57 payload bytes,
one sequence counter per sender, CRC-16, and rejects unknown commands
and other versions. Texts longer than one frame exist only as the device
list, which is sent in numbered parts under a `generation`.

## Decision

### Three generic components on the board

| Component | Shown | Content |
|---|---|---|
| **Card** | Dashboard grid, slots 0–3 (protocol: 0–7, v1 shows 0–3) | icon, title, badge, up to 3 lines |
| **Detail** | full content area after a tap on a card | title, rows (icon, label, value), section titles, up to 3 buttons |
| **Prompt** | over any screen (`lv_layer_top()`) | title, subtitle, countdown, rows, up to 3 buttons |

Plus two fixed pieces the board draws itself: the **clock strip** (from
`TIME`) and the **media strip** (`<< || >>`, sends `ACTION`).

Which feature sits in which slot is decided by the host. v1 order:
0 weather, 1 menu, 2 notifications (empty until after v1), 3 Claude
Code. An empty slot is drawn as an empty frame.

### Text format of a record

One record is UTF-8 text. Lines end with `\n`, fields are separated by
`0x1F` (unit separator). Neither byte may appear inside a field; the
host replaces them by a space. No NUL bytes.

- **Card:** line 1 `icon ␟ title ␟ badge`; lines 2–4 text. At most
  256 bytes.
- **Detail:** line 1 `title ␟ subtitle`; then rows. A row is
  `icon ␟ label ␟ value`; a line starting with `#` is a section title;
  a line starting with `!` is a button `!action ␟ label` (action
  1–255, at most 3 buttons, drawn at the bottom). At most 2048 bytes.
- **Prompt:** line 1 `title ␟ subtitle ␟ seconds`; then as a detail.
  At most 1024 bytes.

`icon` is a decimal number from a table in `protocol.md` (0 = none);
the board maps it to a glyph of its icon font and draws nothing for a
number it does not know. Empty fields are allowed. The board cuts what
does not fit on screen with "…"; it never wraps a card line.

### Chunked records

A record is sent in parts. Every part frame starts with a 4-byte header
followed by up to 53 bytes of the record:

| Byte | Field | Value |
|---|---|---|
| 0 | `id` | card slot 0–7, or prompt id 0–255 |
| 1 | `generation` | changed by the host for every new version of this record |
| 2 | `part` | 0 … `parts` − 1 |
| 3 | `parts` | 1–39 (39 × 53 ≥ 2048) |

The board collects the parts of one `generation` in a staging buffer
and replaces the shown record only when all parts are there, as for the
device list. A part of another `generation` discards a partial record.
A record still incomplete 1000 ms after its last part is requested again
(`GET_CARDS` for cards, `OPEN_DETAIL` again for a detail; a prompt is
dropped and the host times it out). The host sends one record completely
before the next.

### Message set (protocol version 3)

Commands `0x01`–`0x0E` and `ERROR` are unchanged from version 2, except
that the settings record is 12 bytes (ADR-005: byte 11
`startup_screen`, 0 Dashboard, 1 Audio, default 0). New commands:

| Code | Command | Direction | Payload |
|---|---|---|---|
| `0x20` | `TIME` | Host → STM32 | `local_time` (u32, seconds since 1970-01-01 00:00 in local time) |
| `0x21` | `CARD_PART` | Host → STM32 | part header (`id` = slot) + text |
| `0x22` | `CARD_CLEAR` | Host → STM32 | `slot` |
| `0x23` | `DETAIL_PART` | Host → STM32 | part header (`id` = slot) + text |
| `0x24` | `PROMPT_PART` | Host → STM32 | part header (`id` = prompt id) + text |
| `0x25` | `PROMPT_CLOSE` | Host → STM32 | `prompt_id` |
| `0x30` | `GET_CARDS` | STM32 → Host | none |
| `0x31` | `OPEN_DETAIL` | STM32 → Host | `slot`, `arg` (int8; 0 = default, ±1 = previous / next, e.g. menu day) |
| `0x32` | `ACTION` | STM32 → Host | `kind` (0 card detail, 1 prompt, 2 media), `ref` (slot, prompt id, or 0), `action` (1–255) |

Exchanges:

| Trigger | Frames |
|---|---|
| Board connects | STM32 → `GET_DEVICE_LIST` and `GET_CARDS`; Host → device list as in v2, `SETTINGS`; `TIME`; `CARD_PART`… for every filled slot, `CARD_CLEAR` for every empty one |
| Every 10 minutes, and when Windows' clock or time zone changes | Host → `TIME` |
| A card's content changes | Host → its `CARD_PART`s (new `generation`) |
| User taps a card | STM32 → `OPEN_DETAIL slot 0`; Host → `DETAIL_PART`s, or `ERROR OPERATION_FAILED` (no detail). No answer in 2000 ms → notice, the board stays on the dashboard |
| User taps ‹ / › in a detail | STM32 → `OPEN_DETAIL slot ∓1`; Host → the new detail |
| A shown detail changes on the host | Host → its `DETAIL_PART`s, unasked |
| User taps a detail button | STM32 → `ACTION kind 0, ref slot, action n` |
| User taps `<<`, `||`, `>>` | STM32 → `ACTION kind 2, ref 0, action 1 / 2 / 3` |
| Host needs a decision | Host → `PROMPT_PART`s; the board shows the prompt over everything and wakes the screen |
| User answers the prompt | STM32 → `ACTION kind 1, ref prompt id, action n`; Host → `PROMPT_CLOSE` |
| Prompt answered elsewhere or timed out | Host → `PROMPT_CLOSE` |

The board shows one prompt at a time; the host queues the rest and puts
"1/2" in the subtitle itself. `ACTION` needs no direct answer: its
effect comes back as new cards, a new detail or `PROMPT_CLOSE`.

### Routing and sequence numbers

- **Host:** a small `FrameRouter` in `host/App` decodes each frame once
  and passes it on by command: `0x01`–`0x0E` → `HostController`
  (unchanged), `0x20`–`0x3F` → the new `DashController`. An `ERROR` goes
  by its `ref_command` the same way (`ref_command` 0 → `HostController`).
- **Firmware:** the same split in `App/link_port.c`: `link_client.c`
  (audio, unchanged) and the new `firmware/link/dash_client.c`.
- **One sequence counter per side.** Frames of both clients leave
  through one send point that sets `sequence` and the CRC, so the
  counter stays "per sender" as `protocol.md` defines it.

### Firmware memory

Static buffers, no dynamic allocation: 8 cards × 257 B, one detail of
2049 B, one prompt of 1025 B, and one staging buffer of each kind
(257 + 2049 + 1025 B): about 8.4 KB of the ~140 KB free internal RAM
(`docs/memory-review.md`). The board keeps only the detail that is open;
it asks for it again on every tap.

### JSON on the host

The host reads JSON in three places in v1: the Open-Meteo answer, the
Claude Code hook input (ADR-007) and the monthly menu file / Claude API
answer. It uses **nlohmann/json** (MIT, one header), vendored as
`host/third_party/nlohmann/json.hpp` at a pinned release, used only in
`host_core`, never in the firmware. This is the one new third-party
dependency of v1; the rule against unnecessary dependencies is met
because three independent formats would otherwise each need a
hand-written parser.

## Alternatives

- **Feature-specific messages** (`WEATHER_NOW`, `MENU_DAY`, …). Compact
  and typed, but every new feature or field means firmware and protocol
  changes. Rejected by the thin-firmware rule.
- **Images rendered on the host** (send pixels). Most flexible, but a
  424×272 RGB565 image is ~226 KB, about 3.5 s over the 64-byte, 5 ms
  HID link; touch areas would still need a description. Rejected.
- **A layout language** (positions, fonts, colours sent by the host).
  The generic components cover v1; a layout language is the kind of
  framework the project avoids until a real need shows.
- **Binary TLV records instead of text.** Slightly smaller, harder to
  read in logs and tests; the text format is debug-friendly, one of the
  protocol's goals.
- **JSON alternatives:** `Windows.Data.Json` (WinRT, MSVC only; the
  Linux CI and MinGW builds of `host_core` could not test the parsers),
  Open-Meteo's CSV output (covers only the weather), hand-written
  parsers (three of them, error-prone).

## Consequences

- Protocol version 3 is incompatible with version 2: firmware and host
  are updated together (the PID change of #3 already separates this
  product from the audio controller).
- New features (notifications, controls) are mostly host work: a class
  that fills a card and a detail. The board changes only for a new kind
  of component or a new icon.
- The board can show only what the components can express; a feature
  that needs a chart or an image needs a new component and an ADR.
- The firmware gains `dash_client.c` and the dashboard UI (step 6) with
  unit tests for record assembly; the host gains `FrameRouter`,
  `DashController` and the JSON header.
