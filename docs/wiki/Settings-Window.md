# Settings Window

On the PC, the board is served by **`dashboard-tray`**, a program in the
notification area (the tray) that the installer starts with Windows. Its
**settings window** holds every setting of the board and of the PC side.

> The pictures on this page are the window's own page
> (`host/Platform/Windows/settings.html`) rendered in Chromium with demo
> data: the Windows title bar is missing and the font stands in for
> Segoe UI. See [Screen Gallery](Screen-Gallery#how-the-pictures-are-made).

## The tray icon

| Click | Opens |
|---|---|
| **Left click** | the settings window |
| **Right click** | the menu |

| Menu item | Does |
|---|---|
| Kumanda bağlı / Kumanda bekleniyor | the state: board connected or not (greyed out; also the icon's tooltip) |
| Ayarlar… | the settings window |
| Günlüğü aç | opens `host.log`: every frame between the PC and the board, with the time |
| Çıkış | stops the program (preferences saved) and removes the icon; the board shows the waiting screen |

The menu follows the language chosen on the board. Only one program
serves the board at a time; a second tray program (or
`dashboard-host run`) says so and exits.

## The window

A list of pages on the left, with the connection state under the title.
Changes apply at once: a setting goes to the board within a second, and
a change made on the board updates the open window. It follows the
Windows light or dark mode.

### General

![General](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-general-light.png)

Language, **start with Windows** (current user only), the startup
screen of the board, the screen timeout and turning the display upside
down.

### Appearance

![Appearance](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-display-light.png)

Theme and accent colour of the board; the Audio tab's layout, grid and
startup tab.

### Audio

![Audio](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-audio-light.png)

Every Windows output with its name on the board (an empty box shows
Windows' name), ★ for favourites, ↑ ↓ for the order. Below: the volume
limit on switch and its value. The board shows each change at once.

### Dashboard

![Dashboard page](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-dashboard-light.png)

- **Weather**: the place in use and a search (Open-Meteo's geocoding);
  pick a result and the card follows.
- **Menu**: load the month's menu images or saved portal pages, the hour
  from which the card shows the next working day, and the Claude API key
  (saved to `claude.txt`; the model in use is shown).
- **Food lists**: the three lists under `[istemiyorum]`, `[sevmiyorum]`
  and `[seviyorum]`; "Kaydet" colours the menu card at once.

More: [Menu of the Day](Menu-of-the-Day).

### Pictures

![Pictures page](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-pictures-light.png)

- **Fotoğraf seç…** adds photos (JPEG, PNG, an iPhone's HEIC, …). They are
  copied unchanged into the settings folder (`pictures\`), with an upright
  preview for the window.
- Each picture: the part the board shows as a thumbnail, its size, "In
  shuffle", **Göster** (show it now), **Kırp** (crop) and **Sil**
  (delete). The one on the board is framed and marked "Kartta".
- **Karışık göster** (shuffle): every 1 minute to 24 hours, random or in
  the list's order, among the pictures marked for it. A new picture
  reaches the board in about 20 seconds and replaces the old one only
  when it is complete.
- **Resmi kaldır**: no picture on the board; the shuffle stops too.

**Cropping**: the frame is the board's 480 × 272. Drag to move, zoom with
the wheel, the slider or − +; "Ortala" centres it. The board's picture is
cut from the original photo, not from the preview.

![Crop dialog](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-crop-light.png)

### Claude Code

![Claude Code page](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-claude-light.png)

**Kopyala** copies the hook setting to merge into
`%USERPROFILE%\.claude\settings.json`. More:
[Claude Code Approvals](Claude-Code-Approvals#set-up-once).

### About

![About page, dark mode](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-about-dark.png)

The program's version and protocol, the firmware's version and protocol
(read from the board's USB string), and a warning when the two protocols
differ ("update the firmware" or "update the program"). Buttons open the
settings folder and the log.

*Above in Windows' dark mode; every page follows it.*

## Files in the settings folder

`%APPDATA%\stm32-desk-dashboard\` ("Ayarlar klasörü" on the About
page):

| File | What |
|---|---|
| `preferences.txt` | the board's settings, the outputs' names, favourites and order |
| `weather.txt` | the weather place (name, latitude, longitude) |
| `menu-YYYY-MM.json` | a month's menu |
| `menu-lists.txt` | the food lists |
| `menu-settings.txt` | the next-day hour (`next_day_hour=14`, `0` = off) |
| `claude.txt` | the Claude API key and, optionally, the model |
| `pictures.json`, `pictures\` | the picture library, the photos and their previews |
| `picture-current.txt` | which picture the board shows |
| `host.log` | the log; above 1 MB at start it becomes `host.log.1` |

The window needs the Microsoft Edge WebView2 Runtime (part of
Windows 11); without it, the tray program says where to get it.

Status: built in CI with MSVC; the Windows and board checks are open
([Hardware Test Checklist](Hardware-Test-Checklist)). Design:
[ADR-011](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-011-settings-window.md),
[ADR-012](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-012-picture-library.md).
