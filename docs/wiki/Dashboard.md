# Dashboard

The Dashboard is the first tab on the rail and, by default, the screen
the board starts on. Everything on it is written by the PC program: the
board only draws the texts, colours and icons it receives
([Architecture](Architecture)).

![The Dashboard](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png)

```text
┌────┬───────────────────────────────────────────────┐
│ ▦  │ 09:41  Çarşamba 28 Ekim              🔔4  ✳   │  clock strip, status icons
│ ♪  │ ┌───────────────────┐ ┌───────────────────┐   │
│ 🔊 │ │ weather           │ │ menu of the day   │   │  cards from the PC (slots 0, 1)
│    │ └───────────────────┘ └───────────────────┘   │
│    │ ┌───────────────────┐ ┌───────────────────┐   │
│ ⚙  │ │ now playing       │ │ picture           │   │  the board's own cards
│ ⇄  │ └───────────────────┘ └───────────────────┘   │
│    │  ⏮  ⏯  ⏭        🔊 ━━━━━━━●━━━━   62%        │  media strip
└────┴───────────────────────────────────────────────┘
```

## Clock strip and status icons

- **Time and date** of the PC, in Turkish or English day and month names
  (the board's language). The PC sends the time when it connects; the
  board counts on and redraws once a minute.
- **The bell** — Windows notifications. The number is how many toasts
  are in the Windows notification centre ("99+" above 99). It is
  **red** while there are toasts you have not seen in the list, normal
  once you opened it. A tap opens the [notifications list](Notifications).
- **The Claude Code symbol** — permission requests of Claude Code on this
  PC. **Red** with a number while requests wait, **yellow** after a
  request timed out, **green** after one was allowed, normal once you
  opened its list. A tap opens the [Claude Code list](Claude-Code-Approvals#the-list).

<table>
<tr><th width="50%">Calm</th><th width="50%">Something waiting</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png" alt="Bell with four toasts, Claude symbol normal" width="100%"><br>Four toasts not seen yet (red bell); nothing from Claude Code</td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-status.png" alt="Rain, a disliked dish, a Claude Code request waiting" width="100%"><br>Toasts seen (grey bell); one Claude Code request waiting (red, &quot;1&quot;); rain; a dish from the <em>sevmiyorum</em> list (yellow menu card)</td></tr>
</table>

## Weather card

One fixed place, set in the [settings window](Settings-Window#dashboard)
(or `dashboard-host weather <city>`). Data from the Open-Meteo forecast
service, no key; fetched every 30 minutes, again after 5 minutes when a
fetch failed.

- A 40 px weather icon, the place, the temperature in large type, the
  day's low / high.
- The background follows the weather:

  | Weather | Colour | | Weather | Colour |
  |---|---|---|---|---|
  | Clear, day | amber `#E59B2E` | | Fog | grey `#7D838C` |
  | Clear, night | night blue `#2B3A67` | | Drizzle, rain | blue `#3A6EA5` |
  | Partly cloudy, day | sky blue `#3D86C6` | | Heavy rain | deep blue `#2D4B7A` |
  | Partly cloudy, night | `#3A4A70` | | Snow | ice blue `#5F8FBF` |
  | Overcast | slate `#66717F` | | Thunderstorm | violet `#4A3B6B` |

- When a fetch fails the card keeps the last data and says when it was
  updated ("Güncellendi 09:00"); without any data, "Hava durumu
  alınamadı".

A tap opens the **weather detail**: a band with now (icon, temperature,
condition, low / high), tiles for feels like, humidity and wind, and the
next 10 days as tiles you swipe sideways (day, icon, high, low, chance
of rain).

<table>
<tr><th width="33%">Now</th><th width="33%">10 days</th><th width="33%">Swiped</th></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-weather.png" alt="Weather detail" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-weather-days.png" alt="Days" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-weather-days-scrolled.png" alt="Days, swiped" width="100%"></td></tr>
</table>

## Menu card

The canteen's lunch: the main dish, then the side dish, the soup and
the dessert. Green by default; the dishes on your food lists colour it
orange, yellow or add a gold frame, and an 08:00 warning tells you about
them. Everything about the menu — import, food lists, colours, the
warning, the next-day hour — is on its own page:
**[Menu of the Day](Menu-of-the-Day)**.

<table>
<tr><th width="25%">Green</th><th width="25%">Liked main dish</th><th width="25%">Disliked</th><th width="25%">Never</th></tr>
<tr><td width="25%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png" alt="Green" width="100%"></td><td width="25%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-liked.png" alt="Gold frame" width="100%"></td><td width="25%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-status.png" alt="Yellow" width="100%"></td><td width="25%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-orange.png" alt="Orange" width="100%"></td></tr>
</table>

## Now-playing card

What plays in Windows' media session (the one the volume flyout shows):
the app, a small cover, the title and the artist; "Şu an çalan bir şey
yok" when nothing plays. A tap opens the [Music tab](Music-Tab).

## Picture card

A picture from the library on the PC ([settings window → Pictures](Settings-Window#pictures)),
cut to the screen's 480 × 272 and scaled down on the card. With the
shuffle on, the PC sends the next picture every few minutes; the board
keeps showing the old one until the new one has fully arrived (about
20 seconds over USB). A tap shows the picture **full screen**; another
tap closes it. A Claude Code request or a banner still appears over it.

![The picture full screen](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-picture.png)

Without a picture the card says "Tepside: Resim seç…" and a tap does
nothing.

## Media strip

| Control | Does |
|---|---|
| ⏮ | Previous track |
| ▶ / ⏸ | Play or pause: the button shows pause while something plays |
| ⏭ | Next track |
| 🔊 | Mute the Windows default output (accent colour while muted) |
| slider, % | Volume of the Windows default output; the same value as on the Audio and Music tabs |

The controls go to the player through the media session; without a
session, or when the player refuses, the PC sends media keys instead, so
they work with another window in front.

## Detail views

A tap on the weather or menu card, the bell or the Claude Code symbol
opens a **detail** inside the tab: a header with the back arrow (‹) at the
top left, a title and a subtitle, then rows of label and value, section
titles, and up to three buttons at the bottom. Some headers have
previous / next (‹ ›, the menu's working days) or a bin at the top right
(clears the notifications or the Claude Code decisions).

- While it loads the detail shows the card's title and "Yükleniyor…".
- If the PC cannot send it, the board returns to the cards with the notice
  "Ayrıntı alınamadı".
- The notifications and Claude Code lists follow the PC while they are
  open: a new toast or a new decision changes them in place.
- The back arrow and ‹ › are 48 px wide and also take a touch slightly
  below them (the panel reports touches a few pixels low).

## Before the PC has sent anything

Right after the board connects, the top cards are empty frames until the
PC sends them (a second or two), and the bottom cards show their
placeholders:

![Empty dashboard](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-empty.png)

Without the PC program the board shows the waiting screen
([User Guide](User-Guide#messages-on-the-board)).

## Light theme and English

<table>
<tr><th width="50%">Light theme</th><th width="50%">English</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-light.png" alt="Light theme" width="100%"></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-dash.png" alt="English" width="100%"></td></tr>
</table>

The cards keep their colours in both themes; the board picks white or
dark text by the brightness of each card. Language and theme are
[board settings](Board-Settings).
