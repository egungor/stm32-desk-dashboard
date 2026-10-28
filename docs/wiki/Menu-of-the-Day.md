# Menu of the Day

The second card of the [Dashboard](Dashboard) shows the canteen's lunch.
The month's menu is read once — from the canteen's images with the
Claude API, or from saved pages of the canteen portal without it — and
kept on the PC as a small JSON file per month. Your food lists then
colour the card and warn you in the morning.

## The card

![Menu card](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png)

- Four lines: the **main dish** in large type, then the side dish, the
  soup and the dessert.
- "Salata bar: 6 çeşit" on a day with only the salad bar, "Resmi tatil"
  on a holiday, "Menü yüklenmedi" when the month has not been imported.
- **The next working day**: from 14:00 on a working day (lunch is over)
  and at the weekend the card shows the next working day, with a badge
  under the icon ("Pzt 2", "Sal 27"). The hour is set in the settings
  window: Dashboard → Menu → "Ertesi günün menüsü" (11:00 … 20:00, or
  off).

## Food lists and colours

Three lists of dishes, one dish per line, edited in the settings window
(Dashboard → Food lists) and kept in
`%APPDATA%\stm32-desk-dashboard\menu-lists.txt`:

```text
[istemiyorum]        never: dishes you will not eat
Etli bamya
Kabak tatlısı

[sevmiyorum]         don't like
Tarhana çorba
Yayla çorba

[seviyorum]          like
Kağıt kebabı
Fıstıklı baklava
```

**The main dish alone colours the card:**

<table>
<tr><th width="33%">Main dish</th><th width="33%">Card</th><th width="33%"></th></tr>
<tr><td valign="top">on <em>istemiyorum</em></td><td valign="top">orange</td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-orange.png" alt="Orange" width="100%"></td></tr>
<tr><td valign="top">on <em>sevmiyorum</em></td><td valign="top">yellow</td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-status.png" alt="Yellow" width="100%"></td></tr>
<tr><td valign="top">on <em>seviyorum</em></td><td valign="top">green with a gold-leaf frame and a gold icon</td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-liked.png" alt="Gold frame" width="100%"></td></tr>
<tr><td valign="top">on no list</td><td valign="top">green</td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png" alt="Green" width="100%"></td></tr>
</table>

A listed side dish, soup or dessert leaves the card green; the detail and
the warning still show it.

## The detail

A tap on the card opens the day: main dish, side dish, soup, dessert,
what comes with it ("Yanında") and, below, the salad bar. ‹ › at the top
right move to the previous or next **working day**.

<table>
<tr><th width="50%">A plain day</th><th width="50%">Dishes from your lists</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu.png" alt="Menu detail" width="100%"></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-marks.png" alt="Listed dishes" width="100%"></td></tr>
</table>

Listed dishes are drawn in their list's colour with an icon: ★ liked
(gold), ⚠ don't like (yellow), ✕ never (orange).

## The morning warning

On a working day with a listed dish, the board shows a warning from
**08:00**, once a day — also on a dark screen, which lights up. It stays
until you tap **Tamam**, and does not come back that day (also after a
restart of the tray program). Saving the food lists shows it again for the
card's day.

<table>
<tr><th width="33%">Main dish on <em>istemiyorum</em></th><th width="33%">Main dish on <em>seviyorum</em></th><th width="33%">Only another dish listed</th></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning.png" alt="Orange warning" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning-liked.png" alt="Gold-leaf warning" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning-other.png" alt="Title strip" width="100%"></td></tr>
</table>

- The panel takes the card's colour when the **main dish** is listed
  (gold leaf for a liked one).
- When only other dishes are on *istemiyorum* or *sevmiyorum*, the
  panel keeps the theme and only its title strip takes the colour.
- A liked side dish alone gives no warning.

## Importing a month

**From images** (needs your own Anthropic API key):

1. In the settings window, Dashboard → Menu: paste the key into "Claude
   API anahtarı" and save (it goes to `claude.txt`; the
   `ANTHROPIC_API_KEY` environment variable works too).
2. "Görüntü seç…" and choose the month's images: one or more PNG, JPEG,
   WebP or GIF files, at most 10 and 5 MB each. The meal and the salad
   bar can be separate images, imported together or one after the other.
3. The images go to the Claude API in one call; the answer is checked and
   merged by date into `menu-YYYY-MM.json`. A balloon sums it up, e.g.
   "Ekim 2026: 20 gün yemek, 17 gün salata bar, 2 tatil; salata bar
   eksik: 26, 27, 30 Ekim".

**From the canteen portal** (no key, nothing sent anywhere): open
"Aylık Yemek Menüsü" in the browser, choose the "Öğle" tab, save the page
(Ctrl+S); then the "Salatabar" tab into a second file. Load both `.html`
files the same way. Days are read straight from the page; "RESMİ TATİL"
becomes a holiday.

Console: `dashboard-host menu import <files>`; `dashboard-host menu`
prints the card and the day's detail as the board gets them.

The month file can be corrected in Notepad; the tray program reads it
again within a minute.

## Privacy

Menu images leave the PC only when you import them, and only to the
Claude API with your key. Pages from the portal are read on the PC.
