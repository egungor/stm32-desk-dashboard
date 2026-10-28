# Screen Gallery

Every screen of the board and of the settings window on the PC, with one
consistent set of demo data: a Wednesday morning in Istanbul, six audio
outputs, a track playing, the canteen's menu, four notifications and a
Claude Code session. How the pictures are made is at the
[end of the page](#how-the-pictures-are-made).

All board screens are 480 × 272, the panel's own size.

## Start

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/waiting.png" alt="Waiting" width="100%"></td><td valign="top"><strong>Waiting for the computer.</strong> No PC program is talking to the board: check the USB OTG FS cable and that the tray program runs. The button at the top right turns the display (hold 1 s).</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-empty.png" alt="Empty dashboard" width="100%"></td><td valign="top"><strong>Just connected.</strong> The clock runs; the top cards are empty until the PC sends them, the bottom ones show their placeholders.</td></tr>
</table>

## Dashboard — [page](Dashboard)

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png" alt="Dashboard" width="100%"></td><td valign="top"><strong>The Dashboard.</strong> Clock and date; the bell (4 toasts, red: not seen) and the Claude Code symbol; sunny weather (amber card); a menu with nothing listed (green); the playing track with its cover; the picture; media keys and the volume.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-status.png" alt="Status" width="100%"></td><td valign="top"><strong>Something waiting.</strong> Rain (blue card); a dish of <em>sevmiyorum</em> (yellow); one Claude Code request waiting (red symbol, &quot;1&quot;); the toasts seen (grey bell).</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-orange.png" alt="Orange menu" width="100%"></td><td valign="top"><strong>A dish you never want</strong> makes the menu card orange. The green Claude Code symbol: a request was allowed. No toasts: the bell without a number.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-liked.png" alt="Gold frame" width="100%"></td><td valign="top"><strong>A dish you like</strong> as the main dish: green with a gold-leaf frame and a gold icon.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-weather.png" alt="Weather detail" width="100%"></td><td valign="top"><strong>Weather detail.</strong> Now (icon, temperature, condition, low / high), feels like, humidity, wind.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-weather-days.png" alt="10 days" width="100%"></td><td valign="top"><strong>10 days</strong>, scrolled down: day, icon, high, low, chance of rain.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-weather-days-scrolled.png" alt="10 days, swiped" width="100%"></td><td valign="top"><strong>Swiped sideways</strong> to the last days.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu.png" alt="Menu detail" width="100%"></td><td valign="top"><strong>Menu detail.</strong> The day's five lines and the salad bar; ‹ › go to the previous or next working day.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-marks.png" alt="Menu marks" width="100%"></td><td valign="top"><strong>Listed dishes</strong> in their list's colour: ★ like, ⚠ don't like, ✕ never.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-picture.png" alt="Picture" width="100%"></td><td valign="top"><strong>The picture full screen</strong> after a tap on its card; another tap closes it.</td></tr>
</table>

## Over every screen

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning.png" alt="Menu warning" width="100%"></td><td valign="top"><strong>Morning menu warning</strong>, main dish on <em>istemiyorum</em>: the panel in the card's orange. Stays until &quot;Tamam&quot;. <a href="Menu-of-the-Day#the-morning-warning">Menu of the Day</a></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning-liked.png" alt="Menu warning, liked" width="100%"></td><td valign="top"><strong>The warning with a liked main dish</strong>: green with the gold-leaf frame.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning-other.png" alt="Menu warning, other" width="100%"></td><td valign="top"><strong>Only another dish listed</strong>: the theme's panel with a title strip in the card's colour.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-banner.png" alt="Banner" width="100%"></td><td valign="top"><strong>A notification banner</strong>, 5 s (countdown at the right). A tap opens the list. <a href="Notifications">Notifications</a></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-banner.png" alt="Banner over Audio" width="100%"></td><td valign="top"><strong>The banner over the Audio tab.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-notifications.png" alt="Notifications list" width="100%"></td><td valign="top"><strong>The notifications list</strong> behind the bell: app and time, sender and text; the bin clears the notification centre.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-prompt.png" alt="Claude Code request" width="100%"></td><td valign="top"><strong>A Claude Code request</strong>: a command, its description, 60 s to answer. <a href="Claude-Code-Approvals">Claude Code Approvals</a></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-prompt-edit.png" alt="Claude Code edit" width="100%"></td><td valign="top"><strong>An edit request</strong>, the second of two (&quot;2/2&quot;), 41 s left.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-list.png" alt="Claude Code list" width="100%"></td><td valign="top"><strong>The Claude Code list</strong> behind the symbol: waiting requests and the last decisions.</td></tr>
</table>

## Music tab — [page](Music-Tab)

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-playing.png" alt="Playing" width="100%"></td><td valign="top"><strong>Playing</strong>: cover, title, artist, album, app; a progress bar with a knob (the player can seek); the volume on the right.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-paused.png" alt="Paused" width="100%"></td><td valign="top"><strong>Paused</strong>: &quot;· Duraklatıldı&quot;, the play button; this player cannot seek (no knob).</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-none.png" alt="Nothing playing" width="100%"></td><td valign="top"><strong>Nothing playing.</strong></td></tr>
</table>

## Audio tab — [page](Audio-Tab)

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-cards.png" alt="Cards" width="100%"></td><td valign="top"><strong>Cards, 2×2</strong>: the active output in the accent colour, stars on favourites, page 1 of 2.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-pending.png" alt="Pending" width="100%"></td><td valign="top"><strong>&quot;Geçiliyor…&quot;</strong>: tapped, waiting for Windows to confirm.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-muted.png" alt="Muted" width="100%"></td><td valign="top"><strong>Muted</strong>: the speaker button in the accent colour.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-favorites.png" alt="Favourites" width="100%"></td><td valign="top"><strong>The &quot;Favoriler&quot; tab.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-grid-3x2.png" alt="3×2" width="100%"></td><td valign="top"><strong>Cards, 3×2.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-grid-3x3.png" alt="3×3" width="100%"></td><td valign="top"><strong>Cards, 3×3.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-grid-list.png" alt="List grid" width="100%"></td><td valign="top"><strong>Cards as a list.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-layout-list.png" alt="Layout B" width="100%"></td><td valign="top"><strong>Liste layout</strong>: your name and Windows' name, a vertical volume slider.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-layout-tiles.png" alt="Layout C" width="100%"></td><td valign="top"><strong>Karo layout</strong>: the active output large, with its own volume and mute; the others as tiles.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-edit.png" alt="Edit" width="100%"></td><td valign="top"><strong>Edit mode</strong>: move, favourite, rename.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-keyboard.png" alt="Keyboard" width="100%"></td><td valign="top"><strong>The name keyboard</strong>, Turkish Q layout.</td></tr>
</table>

## Settings on the board — [page](Board-Settings)

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-top.png" alt="Top" width="100%"></td><td valign="top"><strong>GENEL</strong>: theme, colour, language, screen off.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-middle.png" alt="Middle" width="100%"></td><td valign="top"><strong>Scrolled</strong>: turn the display, startup screen, then SES.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-bottom.png" alt="Bottom" width="100%"></td><td valign="top"><strong>SES</strong>: layout, grid, startup tab, volume limit on switch.</td></tr>
</table>

## Themes, colours, languages

<table>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-light.png" alt="Dashboard, light" width="100%"></td><td valign="top"><strong>Dashboard, light theme.</strong> The cards keep their colours.</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-light.png" alt="Music, light" width="100%"></td><td valign="top"><strong>Music tab, light theme.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-light.png" alt="Audio, light" width="100%"></td><td valign="top"><strong>Audio tab, light theme.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-light.png" alt="Settings, light" width="100%"></td><td valign="top"><strong>Settings, light theme.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-dash.png" alt="Dashboard, English" width="100%"></td><td valign="top"><strong>English</strong>: the date in English (the PC's texts follow too).</td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-music.png" alt="Music, English" width="100%"></td><td valign="top"><strong>Music tab, English.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-audio.png" alt="Audio, English" width="100%"></td><td valign="top"><strong>Audio tab, English.</strong></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-settings.png" alt="Settings, English" width="100%"></td><td valign="top"><strong>Settings, English.</strong></td></tr>
</table>

![Six accent colours](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/accents.png)

*The six accent colours: orange, blue, green, purple, pink, yellow.*

## Settings window on the PC — [page](Settings-Window)

<table>
<tr><th width="50%">Light</th><th width="50%">Dark</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-general-light.png" alt="win general light" width="100%"><br><strong>General</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-general-dark.png" alt="win general dark" width="100%"></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-display-light.png" alt="win display light" width="100%"><br><strong>Appearance</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-display-dark.png" alt="win display dark" width="100%"></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-audio-light.png" alt="win audio light" width="100%"><br><strong>Audio</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-audio-dark.png" alt="win audio dark" width="100%"></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-dashboard-light.png" alt="win dashboard light" width="100%"><br><strong>Dashboard</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-dashboard-dark.png" alt="win dashboard dark" width="100%"></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-pictures-light.png" alt="win pictures light" width="100%"><br><strong>Pictures</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-pictures-dark.png" alt="win pictures dark" width="100%"></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-claude-light.png" alt="win claude light" width="100%"><br><strong>Claude Code</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-claude-dark.png" alt="win claude dark" width="100%"></td></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-about-light.png" alt="win about light" width="100%"><br><strong>About</strong></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-about-dark.png" alt="win about dark" width="100%"></td></tr>
</table>

![Crop dialog](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-crop-light.png)

*The crop dialog of the Pictures page.*

## How the pictures are made

**Board screens.** `firmware/simulator/doc_shots.c` runs the
firmware's own UI code (`firmware/ui`, the same LVGL build and
`lv_conf.h`) headless on Linux. It renders into a 480 × 272 RGB565
framebuffer — the format the LTDC shows on the panel — and feeds the UI
the same text records the PC program sends (cards, details, prompts,
banners, media). `tools/fb2png.py` converts each framebuffer, the same
converter used for dumps read from the board over SWD.

They are **not photos of the panel**. The method was checked against
the board on 2026-10-16: the simulator gave the LCD screenshots of that
firmware pixel for pixel (0 of 130,560 pixels differ;
`docs/images/README.md`). That compares framebuffers: colours and
sharpness on the real panel are REQUIRES HARDWARE.

**Demo data.** The texts are written by hand in the formats of
`protocol/protocol.md`; places, names, the track and the notifications
are made up. The picture (a sunset over a lake) and the cover (waves and
a moon) are drawn from formulas by `tools/doc-screens/assets.py`, so no
one else's photo or album art is in the repository.

**Settings window.** `tools/doc-screens/settings_window.py` opens the
window's page (`host/Platform/Windows/settings.html`) in headless
Chromium with a stand-in for WebView2's message channel that answers
with demo state. Layout, texts and controls are the page's own; the
Windows title bar is missing and Inter stands in for Segoe UI.

**Making them again** (Linux, after `git submodule update --init`):

```text
pip install numpy pillow playwright && python -m playwright install chromium
tools/doc-screens/make.sh            # → docs/images/screens/
```

The script builds the `ui_doc_shots` target of `firmware/simulator`,
draws the demo picture and cover, renders every screen and the composite
images (`hero.png`, `accents.png`), then the settings window (skipped
without Playwright). The test script of the simulator
(`tools/run-ui-simulator.sh`) is separate and runs in CI.
