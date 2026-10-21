# ADR-008: Now playing, cover art and the Music tab; protocol version 4

## Status

Accepted (issue #30). Protocol, codec and tests: #30. Host (Windows
media session): #31. Board (media strip, Music tab): #32.

Notes from #31 (host): the reader polls every 500 ms (150 ms after a
command) rather than subscribing to the session's events, which keeps
the WinRT code small; a change wakes the service loop
(`HostEvent::MediaChanged`). The cover goes out 8 parts per turn of the
service loop, which does not wait while parts are left. A thumbnail that
comes after the title is read again for up to 10 polls; a cover that
arrives late gets its own `art_id`. While a track changes (`Changing`)
the last status is kept, so the play / pause icon does not flicker.
`host.log` shows the first and last `ART_PART` of a cover only.

## Context

The owner's requests of 2026-10-21, before v1.0.0:

- the media strip of the Dashboard gets proper icons: previous on the
  left, next on the right, and in the middle **pause while something
  plays, play while nothing plays**;
- a new tab on the rail, right after the Dashboard, with a music icon:
  what is playing now (title, artist, album, the app, the progress, a
  small cover) and the same controls;
- (Windows notifications come later, after their own spike, #33 / #34.)

Today the host only sends media keys (`SendInput`, step 7, #16) and
knows nothing about what plays, so the board cannot know whether to
draw play or pause. Protocol v3 (ADR-006) has text records and
`ACTION kind 2` for the strip, but no live state, and records are text
only, so they cannot carry an image.

Windows has one place that knows what plays: the **system media
transport controls** (`Windows.Media.Control`,
`GlobalSystemMediaTransportControlsSessionManager`, Windows 10 1809 and
later). Spotify, browsers (YouTube), the Media Player and most players
publish their session there: title, artist, album, the app, playback
status, which controls are enabled, the timeline (position, end) and a
thumbnail. It can be used from a normal (unpackaged) desktop program
through C++/WinRT, which ships with the Windows SDK; it is not a
third-party dependency. The same session takes the commands (skip
previous, toggle play/pause, skip next).

## Decision

### On the host

- A small interface `IMediaSession` (current session: texts, status,
  controls, position, duration, cover; commands; a change callback) in
  `host/Media/`, and `WinMediaSession` in `host/Platform/Windows/` with
  C++/WinRT. The rest of the host does not see WinRT.
- The **current session** is the one Windows calls current
  (`GetCurrentSession`), as the Windows volume flyout shows it.
- The cover comes from the session thumbnail, decoded with WIC
  (Windows Imaging Component), cropped to a centred square and scaled to
  **120 × 120**, converted to RGB565.
- The media `ACTION`s go to the session; without a session they fall
  back to the media keys of step 7.
- A `MediaService` turns the session into frames (below). It sends
  `MEDIA_STATE` when the status, controls, cover, duration or track
  change, when the position jumps (seek) by more than 2 s against what
  the board counts, and every 30 s while playing.

### Thin firmware still holds

The board gets one more fixed component, the **now playing** view, next
to the clock strip and the media strip (ADR-006). It knows nothing about
Spotify or YouTube: it draws three texts, an app name, a cover, a
progress bar and three buttons from what the host sends. Cards and
details cannot do this: they have no live position, no play/pause state
for the strip and no image.

### Protocol version 4

New commands; everything of version 3 is unchanged except the version
byte and `startup_screen`, which gets value 2 (Music). A board on
version 3 would answer the new commands with `UNKNOWN_COMMAND` and
reject `startup_screen` 2, so this is an incompatible change: version
4. Firmware and host are released together (one tag builds both), so
both move to 4 at once.

| Code | Command | Direction | Payload |
|---|---|---|---|
| `0x26` | `MEDIA_PART` | Host → STM32 | part header (`id` = 0) + record text: `title ␟ artist ␟ album ␟ app`, at most 512 bytes (10 parts) |
| `0x27` | `MEDIA_STATE` | Host → STM32 | `status`, `controls`, `art_id`, `position` (u32 s), `duration` (u32 s) — 11 bytes |
| `0x28` | `ART_PART` | Host → STM32 | `art_id`, `index` (u16), data: 54 bytes, 18 in the last part (534 parts of a 28 800-byte cover) |
| `0x33` | `GET_MEDIA` | STM32 → Host | none |

- `status`: 0 no session (the board shows "nothing playing"), 1
  stopped, 2 playing, 3 paused. `controls`: bit 0 previous, bit 1
  play/pause, bit 2 next are enabled.
- `position` / `duration` in whole seconds; `duration` 0 = unknown (no
  progress bar). While `status` is 2 the board counts `position` on by
  itself, as the clock; the host corrects it as said above.
- `art_id` 0 = no cover; 1–255 names the cover the host sends in
  `ART_PART`s, a new id for every new cover.
- Cover format: 120 × 120 pixels, RGB565, little endian, row by row from
  the top left: 28 800 bytes, 534 parts of 54 bytes (the last 18).
- The media record uses the record rules of ADR-006 (part header,
  `generation`, assembled in any order, replaced only when complete).
- Exchanges: the board sends `GET_MEDIA` at start and after a (re)connect,
  as `GET_CARDS`; the host answers with the media record, `MEDIA_STATE`,
  then the cover. On a change the host sends a new record if the texts
  changed, then `MEDIA_STATE`, then the cover if `art_id` is new. The
  board collects `ART_PART`s only for the `art_id` of the last
  `MEDIA_STATE`; others are dropped. A media record or cover still
  incomplete 1000 ms after its last part → `GET_MEDIA` again.
- The controls stay `ACTION kind 2` (1 previous, 2 play/pause, 3 next).

### The cover over HID

A cover is 534 frames. At the 5 ms interval of the OUT endpoint
(ADR-003) that is about **2.7 s** per new cover; texts and status come
first, the cover follows. The host sends cover parts at low priority:
other frames (volume, device list, prompts, cards) may go out between
two cover parts, and a newer cover stops an older one. A record of
ADR-006 still goes out complete before the next record. The USB interval
is not changed for this (ADR-003 stays).

Smaller or compressed covers were weighed (below); 120 × 120 raw is
what the owner chose ("küçük kapak") and keeps the board's decoder at
zero code.

### Firmware memory

Two static cover buffers of 28 800 bytes (the shown cover and the one
being received) in SDRAM, outside the LVGL heap; the shown one is an
LVGL image (`LV_COLOR_FORMAT_RGB565`). A bitmap of 534 bits marks the
received parts. The media record: one 512-byte buffer being assembled
and the shown texts in `g_ui`.

### Tabs and settings

Rail order: Dashboard, **Music**, Audio, then settings. `startup_screen`
0 Dashboard, 1 Audio (unchanged values), 2 Music. The Music tab and the
Dashboard's media strip show the same state.

## Alternatives

- **Keep only media keys** and toggle the middle icon on each tap: the
  icon would lie as soon as the user pauses on the PC. Rejected.
- **Send the media info as a card or detail record**: no live position,
  no state for the strip, no image. Rejected.
- **No version change**, new commands in version 3: an older board
  would log `UNKNOWN_COMMAND` for every media frame and reject the
  settings with `startup_screen` 2. Rejected (protocol rule:
  incompatible change → new version).
- **A smaller cover** (64 × 64 or 96 × 96, 0.8–1.7 s) or a **compressed
  cover** (JPEG / PNG decoded on the board): smaller transfers, but a
  coarser image or a decoder in the firmware (LVGL's decoders need
  heap and code). Kept as an option if 2.7 s is too slow in practice.
- **A shorter USB interval** (1 ms): five times faster, but changes the
  USB descriptor of ADR-003 and the board's polling. Not for this step.

## Consequences

- Protocol version 4; the firmware and the host must be updated
  together (one release). `protocol.md`, the codec and its tests change
  in #30; host in #31; board in #32.
- The host needs C++/WinRT (Windows SDK) for `WinMediaSession`; only the
  Windows build of the host is affected. The Linux build and tests of
  `host_core` use a fake `IMediaSession`.
- Which players publish a session, how often their timeline updates and
  what their thumbnails look like: REQUIRES WINDOWS (#31).
- The cover transfer (2.7 s) and SDRAM buffers: REQUIRES HARDWARE (#32).
