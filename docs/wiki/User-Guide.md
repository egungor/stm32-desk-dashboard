# User Guide

How to use the controller day to day. The screens below are LCD
screenshots read from the board (firmware v0.7.0-beta.2), except where a
caption says *simulator*. The UI text is Turkish by default; English is
one tap away on the settings page ("Dil" → English).

To install the firmware and the PC program first, see
[Getting Started](Getting-Started#quick-start-from-a-release).

## Main screen

![Main screen, layout A](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-main-screen.png)

- **Tap an output** to make it the Windows default. The card shows
  "Geçiliyor..." until Windows confirms, then turns to the accent colour
  with "✓ Etkin çıkış" (active output).
- **Tabs**: "Tümü" (all outputs) and "Favoriler" (only the ones marked
  with a star). The board remembers the last tab (or a fixed one, see
  "Açılış sekmesi" below).
- **Pages**: when the outputs do not fit, the arrows at the bottom right
  turn the pages ("1/3").
- **Top bar**: the plug icon shows that the PC program is connected. The
  pencil opens the edit mode, the cog opens the settings, and the round
  arrow turns the display (hold it for 1 second, then let go).
- **Volume**: drag the slider. The percentage on the right is the
  volume of the active output in Windows. Changes made in Windows show
  up here too.
- **Mute**: tap the speaker button. While muted the button is in the
  accent colour and the slider is grey. Moving the slider while muted
  unmutes.

![Favorites tab](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-favorites.png)
![Muted](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-muted.png)

*Left: the "Favoriler" tab. Right: muted.*

## Three layouts

Choose one on the settings page ("Görünüm": Kart, Liste, Karo).

**Kart (cards)**: the screen above. The card grid can be 2×2, 3×2, 3×3
or a list ("Izgara").

**Liste (list)**: five rows with the alias and, under it, the Windows
name. The rail on the left switches between all outputs, favorites, the
edit mode and the settings. The volume slider is on the right.

![Layout B, list](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-layout-list.png)

**Karo (large tile)**: the active output in a large panel with its own
volume and mute, and the other outputs as six small tiles.

![Layout C, large tile](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-layout-tiles.png)

In Liste and Karo, turn the display with "Ekranı döndür" on the settings
page; only Kart has the round-arrow button on the main screen.

## Settings

The cog opens the settings; "‹ Ayarlar" at the top left goes back. Every
change applies at once and is stored on the PC (in the program's
preference file), so it survives a restart of the board or the PC. Scroll
the list with a finger; scrolling over a control does not change it.

![Settings page](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-settings.png)

| Row | Choices | What it does |
|---|---|---|
| Görünüm | Kart / Liste / Karo | Layout of the main screen |
| Tema | Koyu / Açık | Dark or light theme |
| Renk | six colours | Accent colour (orange, blue, green, purple, pink, yellow) |
| Izgara | 2×2 / 3×2 / 3×3 / Liste | Card grid; Kart layout only (greyed out otherwise) |
| Açılış sekmesi | Son / Tümü / Favori | Tab shown when the board starts: the last one used, or a fixed one |
| Ekran kapanma | Kapalı / 1 dk / 5 dk / 15 dk | Backlight off after that long without a touch |
| Geçişte ses sınırı | off / on + percentage | When you tap an output whose volume is above the limit, it is turned down to the limit before it becomes active. Switches made in Windows are not limited |
| Ekranı döndür | off / on | Turns the image and touch by 180°, for a board mounted upside down |
| Dil | Türkçe / English | Language of the board and of the tray menu |

**Screen off**: when the backlight is off, the first touch only wakes
the screen; it does not select anything. Tap again to use it.

![Light theme, blue accent, 3×2 grid (simulator)](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/sim-pitch-a-light-3x2.png)

*Light theme, blue accent, 3×2 grid (simulator, demo names).*

## Favorites, order and names

The pencil opens "Sırala ve düzenle". Every output gets a card with four
buttons:

| Button | Does |
|---|---|
| ‹ / › | Move the output one place earlier or later. The order is the same on every screen |
| ★ | Mark or unmark it as a favorite (accent colour when set) |
| ✎ | Give it another name (alias) |

"Bitti" goes back to the main screen.

![Edit mode (simulator)](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/sim-pitch-edit.png)

*Edit mode (simulator, demo names).*

The alias keyboard has a Turkish Q layout (ç ğ ı İ ö ş ü). "ABC" / "abc"
switch case, "1#" shows digits and signs. An alias can be up to 32
bytes long (about 30 letters; Turkish letters count as two bytes). ✓ or
"Kaydet" saves it, an empty name removes the alias, and × closes without
saving. The Windows name of the device does not change; the alias is
only shown on the board (and in `dashboard-host list`).

![Alias keyboard (simulator)](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/sim-pitch-keyboard.png)

*Alias keyboard (simulator, demo names).*

Aliases can also be set on the PC:

```text
dashboard-host list                       numbered list of the outputs
dashboard-host alias 2 "Oyun kulaklığı"   set the alias of output 2
dashboard-host alias 2 --clear            remove it
```

## On the PC: the tray program

The installer starts `dashboard-tray` and, by default, starts it with
Windows. Its icon in the notification area shows the state in its
tooltip; right-click for the menu:

| Menu item | Does |
|---|---|
| Kumanda bağlı / Kumanda bekleniyor | State: board connected or not (greyed out) |
| Windows ile başlat | Start at logon (check mark when on), current user only |
| Ayarlar klasörünü aç | Opens `%APPDATA%\stm32-desk-dashboard` (preferences, log) |
| Günlüğü aç | Opens `host.log`, every message between the PC and the board |
| Çıkış | Closes the program; the board then shows the waiting screen |

The menu follows the language chosen on the board.

## Messages on the board

![Waiting for the computer](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/ui-waiting-for-host.png)

| Message | Meaning, what to do |
|---|---|
| Bilgisayar bekleniyor | The PC program is not connected. Check that the board's **USB OTG FS** cable is in the PC and that the tray program runs |
| Ses çıkışı değiştirilemedi | Windows refused the switch; try again or check the output in Windows |
| Bilgisayar yanıt vermiyor | The PC did not answer within 2 seconds; the board asks for the list again |
| Değişiklik kaydedilemedi | An edit (alias, favorite, order) was not saved; try again |
| Ses çıkışı bulunamadı / Favori çıkış yok | No outputs in Windows, or none marked as favorite |

Notices disappear by themselves after 3 seconds. More in
[Troubleshooting](Troubleshooting).

## Tips

- The board has two USB connectors: **ST-LINK** (power, firmware
  updates, log) and **USB OTG FS** (the link to the PC). Keep both
  connected: the board is powered through the ST-LINK connector.
- Only one program serves the board at a time: close the tray program
  before running `dashboard-host run` in a console.
- Turn on "Geçişte ses sınırı" if one of your outputs (headphones, for
  example) is often left loud: switching to it never starts above the
  limit.
