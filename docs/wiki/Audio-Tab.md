# Audio Tab

The third button on the rail (🔊): the Windows playback devices
("outputs") as cards. Tap one and Windows makes it the default output.
This is the
[stm32-audio-controller](https://github.com/egungor/stm32-audio-controller)
the project grew from, unchanged in what it does.

![Audio tab, cards 2×2](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-cards.png)

## Choosing an output

- **Tap an output** to make it the Windows default. The card shows
  "Geçiliyor…" until Windows confirms, then turns to the accent colour
  with "✓ Etkin çıkış" (active output). Tapping the active output does
  nothing.
- Windows changes show up by themselves: a default changed in Windows, a
  device plugged in or removed, a renamed device.
- The board only shows what the PC reports: if Windows refuses the switch
  the card goes back and the notice "Ses çıkışı değiştirilemedi" appears.

<table>
<tr><th width="50%">Waiting for Windows</th><th width="50%">Muted</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-pending.png" alt="Pending" width="100%"></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-muted.png" alt="Muted" width="100%"></td></tr>
</table>

## Top bar and bottom strip

| Control | Does |
|---|---|
| **Tümü / Favoriler** | all outputs, or only the ones with a star |
| ✎ (top right) | the edit mode: order, favourites, names (below) |
| 🔊 (bottom left) | mute the active output; in the accent colour while muted |
| slider, % | the active output's volume; moving it while muted unmutes |
| ‹ 1/2 › | pages, when the outputs do not fit |

Volume and mute follow Windows both ways and are the same values as on
the Dashboard strip and the Music tab.

![Favourites tab](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-favorites.png)

*The "Favoriler" tab.*

## Layouts and grids

[Board settings](Board-Settings) → SES → "Görünüm" chooses one of three
layouts; "Izgara" the grid of the cards layout.

<table>
<tr><th width="33%">Kart 2×2</th><th width="33%">Kart 3×2</th><th width="33%">Kart 3×3</th></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-cards.png" alt="2×2" width="100%"><br><strong>Kart, Liste grid</strong></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-grid-3x2.png" alt="3×2" width="100%"><br><strong>Liste layout</strong></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-grid-3x3.png" alt="3×3" width="100%"><br><strong>Karo layout</strong></td></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-grid-list.png" alt="Card list" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-layout-list.png" alt="Layout B" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-layout-tiles.png" alt="Layout C" width="100%"></td></tr>
</table>

- **Kart (cards)**: big cards with the icon and the name; grid 2×2,
  3×2, 3×3 or a one-line list.
- **Liste (list)**: four rows with your name for the output and, under
  it, Windows' name; a check mark on the active one; the volume as a
  vertical slider on the right.
- **Karo (large tile)**: the active output in a large panel with its own
  volume and mute, the others as six small tiles; the tabs as pills.

## Favourites, order and names

The ✎ button opens "Sırala ve düzenle": every output as a card with four
buttons.

![Edit mode](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-edit.png)

| Button | Does |
|---|---|
| ‹ / › | move the output one place earlier or later; the order is the same everywhere |
| ★ | mark or unmark it as a favourite |
| ✎ | give it a name of your own (an alias) |

"Bitti" goes back. The PC stores every change; each one comes back as a
new list from the PC, so what you see is what was saved.

**The keyboard** for names has a Turkish Q layout (ç ğ ı i ö ş ü).
"ABC" / "abc" switch the case, "1#" shows digits and signs. A name can be
up to 32 bytes (about 30 letters; Turkish letters count as two). ✓ or
"Kaydet" saves it, an empty name removes it, × closes without saving.
Windows' own name of the device does not change.

![Name keyboard](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-keyboard.png)

Names, favourites and the order can also be set in the
[settings window](Settings-Window#audio) or in a console:

```text
dashboard-host list                       numbered list of the outputs
dashboard-host alias 2 "Oyun kulaklığı"   set the name of output 2
dashboard-host alias 2 --clear            remove it
```

## Volume limit on switch

[Board settings](Board-Settings) → "Geçişte ses sınırı": when you tap an
output whose volume is above the limit, the PC turns it down to the
limit **before** it becomes the default — handy for headphones that were
left loud. Switches made in Windows are not limited; a volume is never
raised and mute is not touched. The log shows
`Volume limit: "<name>" <from>% -> <to>%`.

## Status

Switching, volume, mute, Windows changes, unplug and replug: VERIFIED on
the board and Windows (2026-10-14). Layout A, themes, accent colours,
the favourites tab, mute and volume and the volume limit: VERIFIED.
Layouts B and C, the edit mode and the keyboard on the board: REQUIRES
HARDWARE + WINDOWS. Switching uses Windows' undocumented `IPolicyConfig`
([ADR-002](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-002-default-device-switching.md)).
