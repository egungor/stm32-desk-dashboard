# ADR-010: Dashboard redesign — coloured cards, status icons, music and picture cards; protocol version 6

## Status

Accepted (issue #47). The owner's request of 2026-10-22 and his answers:
the morning menu warning stays until "Tamam"; the Claude Code icon's
yellow and green turn grey once its list was opened; the picture is
chosen from the tray menu ("Resim seç…").

Amended by issue #53 (2026-10-22): protocol version 7 sends the picture
at the screen's size and a tap on the picture card shows it full screen
("Amendment: full-screen picture" below). ADR-012 (#72) replaces the
single picture with a library, a shuffle and a crop.

Amended on 2026-10-27 ("Amendment: liked dishes" below): a third food
list, the card coloured by the main dish only, a gold-leaf frame, coloured
detail rows and warnings in the card's colours; additive record fields,
no new protocol version.

## Context

The Dashboard (ADR-006) shows four text cards on the theme's card colour:
weather, menu, notifications (ADR-009) and Claude Code (ADR-007). The
owner wants it more colourful and simpler:

1. every card on a coloured background, all card icons larger and the
   same size;
2. weather: a large icon, a small place name, the temperature now and
   today's low / high; the background follows the weather;
3. menu: no title; the main dish, soup and dessert only; the background
   from his food lists (orange: a dish he never wants; yellow: one he
   eats but does not like; green otherwise) and a one-time warning at
   about 08:00 on such a day;
4. notifications and 5. Claude Code: no cards; icons at the top right
   (a count, a warning colour until the list has been opened);
6. bottom left: now playing (music icon, small cover, title);
7. bottom right: a picture chosen in Windows, scaled without distortion,
   cropped at the edges.

## Decision

### Layout (board)

```text
┌───────────────────────────────────────────┐
│ 14:32  Perşembe 22 Ekim      (bell) 3 (robot)│  clock strip, status icons right
│ ┌ card 0: weather ┐ ┌ card 1: menu ────┐   │
│ └─────────────────┘ └──────────────────┘   │  200 × 92 each
│ ┌ now playing ────┐ ┌ picture ─────────┐   │
│ └─────────────────┘ └──────────────────┘   │
│ [|<] [ > ] [>|]      (vol) ━━━━●──── 45%   │  media strip (unchanged)
└───────────────────────────────────────────┘
```

- The top row shows card slots 0 and 1 (host records). The bottom row is
  drawn by the board: the now-playing card from the media record and
  cover it already has (ADR-008; no protocol change), and the picture
  card. Slots 2–7 keep their details (`OPEN_DETAIL`) but have no card on
  the screen; the host clears them.
- Every card icon is 40 px (the 40 px icon font gains the weather icons,
  utensils and music) in a column on the left; the text is on the right.

### Card record (text change, `protocol.md` "Record text")

First line: `icon ␟ title ␟ badge ␟ bg ␟ flags`

- `bg`: the background as six hex digits `RRGGBB`; empty = the theme's
  card colour. The board picks white or dark text by the background's
  brightness. The colours are the host's choice (thin firmware).
- `flags`: letters; `L` = the first text line in the large font (22 px).
- An empty `title` is not drawn.

Colours the host uses:

| Card | State | `bg` |
|---|---|---|
| Weather | clear, day / night | `E59B2E` / `2B3A67` |
| | partly cloudy, day / night | `3D86C6` / `3A4A70` |
| | overcast / fog | `66717F` / `7D838C` |
| | drizzle, rain / heavy rain | `3A6EA5` / `2D4B7A` |
| | snow / thunderstorm | `5F8FBF` / `4A3B6B` |
| Menu | a dish of the "never" list / of the "don't like" list / otherwise | `E0702F` / `D9A520` / `3E9B5F` |
| Menu, weather | no data (no menu, holiday, no forecast yet) | empty (theme) |
| Now playing (board) | always | `5B4B8A` |

Weather card: `icon ␟ Ankara ␟ ␟ bg ␟ L`, then `18°`, then `11° / 21°`
(or "Güncellendi hh:mm" when stale). Menu card: `11 ␟ ␟ ␟ bg ␟`, then the
main dish, the soup, the dessert (empty ones left out).

### Status icons: `INDICATORS` (0x2A, host → board)

| Byte | Field | Value |
|---|---|---|
| 0 | `count` | indicators, 0–4 |
| 1 + 4·i | `icon` | icon number (protocol.md "Icon"; adds 13 = robot) |
| 2 + 4·i | `colour` | 0 normal, 1 red, 2 yellow, 3 green |
| 3 + 4·i | `number` | shown next to the icon; 0 = none; above 99 shown as "99+" |
| 4 + 4·i | `slot` | the detail a tap opens (`OPEN_DETAIL slot, 0`), or `0xFF` = none |

Drawn at the right of the clock strip in their order. The host sends it
after the cards on `GET_CARDS` and whenever it changes.

- Notifications: icon 12 (bell), number = toasts in the notification
  centre, colour red while there are toasts the list has not shown yet
  (a new toast arrived since `OPEN_DETAIL 2`), else normal; slot 2.
- Claude Code: icon 13 (robot), number = waiting requests; colour red
  while one waits, else yellow if one timed out (no answer) since the list
  was last opened, else green if one was allowed since then, else normal;
  slot 3.

### Picture: `PICTURE` (0x2C) and `PICTURE_PART` (0x2B), host → board

As the cover (ADR-008), at the card's size: 200 × 92 pixels, RGB565 little
endian, 36 800 bytes in 682 `PICTURE_PART`s (`pic_id`, `index` u16, 54
data bytes; 26 in the last). `PICTURE` carries `pic_id` (0 = no picture:
the card shows a placeholder). The board collects the parts of the last
`PICTURE` id and shows the picture when complete; one already shown is not
collected again; a picture still incomplete 1 s after its last part (or
after its `PICTURE`) makes the board send `GET_CARDS`. The host sends
`PICTURE` after the cards and `INDICATORS` on `GET_CARDS` and when the
picture changes, then the parts after anything else (as cover parts).

On the host the tray menu's "Resim seç…" opens a file dialog; the path is
kept in `picture.txt` in the settings folder. The host reads the file
with WIC, crops the largest centred 200:92 rectangle (no distortion; the
edges that do not fit are cut) and scales it to 200 × 92.

### Food lists and the morning warning (host)

`menu-lists.txt` in the settings folder (the owner's, not in the
repository): sections `[istemiyorum]` (never) and `[sevmiyorum]` (eats,
does not like), one name per line, `#` comments. A dish matches a name
when the name occurs in it, ignoring case (Turkish rules: İ/i, I/ı). The
five dishes of the day (main, side, soup, dessert, "yanında") are checked,
not the salad bar. The detail marks a matching dish ("istemiyorum" /
"sevmiyorum"). The tray menu's "Yemek listelerini düzenle…" opens the file
(created with a short explanation if missing).

On a working day with a matching dish, from 08:00 the host shows a prompt
(ADR-007's component) once: "Bugünkü menüde …", one button "Tamam"; it
wakes a dark screen and stays until answered. Prompt id 255 is reserved
for it (Claude Code requests use 0–254); a waiting Claude Code request
goes first and the warning waits for it. Shown once per day:
`menu-warned.txt` keeps the date. If the board is not connected at 08:00,
it comes when the board connects later that day.

### Protocol version 6

`INDICATORS`, `PICTURE` and `PICTURE_PART` are new commands; the card record
gains fields. Firmware and host are released together (ADR-008), so both
move to 6.

### Board memory

The picture: one 36 800-byte buffer being collected and one shown, both in
SDRAM next to the cover's (`UI_PORT_PIC_RX_ADDR`, `UI_PORT_PIC_SHOW_ADDR`).
The status: 4 × 4 bytes in `g_ui`. Larger 40 px icon font in flash.

### Amendment: full-screen picture (protocol version 7, #53)

The owner asked for a tap on the picture card to open the photo full
screen. Of two ways (one 480 × 272 picture for both, or the small
picture plus a full-size one fetched on a tap) the owner chose the
first: no new command, no wait on a tap.

- `PROTO_PICTURE_W` × `PROTO_PICTURE_H` become 480 × 272: 261 120 bytes
  in 4 836 `PICTURE_PART`s (30 data bytes in the last). The frames do
  not change; the version goes to 7 because a version 6 peer would
  reject the part indices and lengths.
- The host crops the largest centred 480:272 rectangle and scales it to
  480 × 272 (as before, at the new size; WIC also turns the photo
  upright by its EXIF orientation, #51).
- The board keeps the picture being received and the one shown in
  SDRAM (256 KB each, `ui_port.h`). The card shows the picture scaled
  down to cover the card (`LV_IMAGE_ALIGN_COVER`, the edges are cut).
  A tap on the card shows it 1:1 on `lv_layer_top()`, below a prompt
  or a banner; a tap closes it, as do no picture, the waiting screen
  and a rebuild (theme change).
- A picture takes about 7 times as many frames: about 20 s at one report
  per 4 ms (bInterval 5 ms, ADR-003), after the cards (it does not hold
  them up). It is sent again on every connect.

## Amendment: liked dishes (2026-10-27)

The owner's request: a list of liked dishes; a liked dish keeps the card
green with a gold frame and a gold utensils icon; the warning also for
liked dishes, at 08:00 and when the lists change, in the card's colours.
His choices from simulator screenshots of six frames and three warning
designs: the **gold-leaf** frame (a light-to-dark gold gradient) and the
warning **panel in the card's colour**. His rules:

- **The main dish decides the card** (the side dish on a day without
  one): "never" orange, "don't like" yellow, liked green with the frame
  and a gold icon, otherwise green. Other dishes change nothing on the
  Dashboard (before: the worst of the five dishes coloured the card).
- **The detail** shows every listed dish in its list's colour with its
  icon (14 star gold `D4AF37`, 15 cross orange `E0702F`, 16 warning
  yellow `D9A520`) and its name ("· seviyorum", "· istemiyorum",
  "· sevmiyorum"); the other rows keep an empty icon column.
- **The warning**: the main dish on a list → the panel in the card's
  colour (gold leaf for a liked one; liked rows `F2C94C` on green);
  otherwise a "never" / "don't like" dish → the dark panel with a title
  strip in the card's green, rows in their lists' colours; a liked side
  dish alone → no warning. From 08:00 once a day for today, as before,
  and again when `menu-lists.txt` changes, for the card's day (title
  "Menüde" for another day than today); a shown warning is replaced, or
  closed when the new lists say nothing. Its "Tamam" marks the morning
  warning as done only when it was about today.

`menu-lists.txt` gains `[seviyorum]` (`[like]`); a dish on two lists
takes the first of never, don't like, liked.

Record fields (protocol.md, "Record text"), all optional and ignored by
an older board (it splits a line into a fixed number of fields), so the
protocol version stays 7:

| Record | New fields |
|---|---|
| Card line 1 | 6 `frame` RRGGBB: the gold-leaf frame (4 px, the colour lightened 50 % at the top, darkened 30 % at the bottom); 7 `icon colour` RRGGBB |
| Detail / prompt row | 4 `colour` RRGGBB: icon, label and value |
| Prompt line 1 | 4 `bg` RRGGBB: the panel (its text white or dark by brightness); 5 `frame` RRGGBB; 6 `flags`: `s` = only the title strip in `bg`, the panel's border too; 7 `icon`: the title's icon number (default the warning sign) |

Icons 14 (star), 15 (cross) and 16 (warning) are glyphs the firmware's
16 px font already has. Memory: the framed warning peaks at 89.8 KB of
the 128 KB LVGL heap in the simulator (before: 89.7 KB).

## Alternatives

- **Colour names instead of `RRGGBB`** (a palette in the firmware): every
  new colour would need firmware. Rejected (thin firmware).
- **The picture as a card record of a new kind with an image**: the card
  record is text; mixing a 36 kB bitmap into it would need a new record
  format anyway. The cover's transfer is proven; reused.
- **The now-playing card from the host**: the board has the media record
  and the cover already (ADR-008); sending them again as a card would cost
  frames for nothing.
- **The morning warning as a banner (ADR-009)**: the owner chose a
  pop-up that stays until "Tamam".
- **The Claude logo for the Claude Code icon**: a trademark; a generic
  robot icon (Font Awesome) is used instead. Changed by #59 at the
  owner's request (2026-10-22): the Claude symbol from Wikimedia Commons
  (CC0 there; a trademark of Anthropic) replaces the robot at the same
  code point (`tools/fonts/claude-symbol.svg`), in Claude orange unless a
  state colours it. It is shown on the owner's own device; the
  repository is public, so whoever reuses it should mind the trademark.

## Consequences

- Protocol version 6: `protocol.md`, codec and tests; host and firmware.
- Host: weather and menu card records, `MenuLists` and the morning
  warning, the status of notifications and Claude Code (their cards
  cleared), `PictureService` and the tray's "Resim seç…" and "Yemek
  listelerini düzenle…".
- Board: card rendering (colour, 40 px icon, `L`), status icons, the
  now-playing and picture cards, `dash_client.c` (`INDICATORS`, picture).
- REQUIRES WINDOWS: the WIC picture import, the tray menu items.
  REQUIRES HARDWARE: the new Dashboard on the LCD.
- Version 7 (#53): host and firmware must be updated together; the
  full-screen picture and the transfer time REQUIRE HARDWARE.
