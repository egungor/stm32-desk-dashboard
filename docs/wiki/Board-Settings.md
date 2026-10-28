# Board Settings

The cog at the foot of the rail opens the settings page. Every change
applies at once and is stored on the PC (in `preferences.txt`), so it
survives a restart of the board or the PC. The same settings are in the
[settings window](Settings-Window) on the PC; a change in one shows in
the other within a second.

<table>
<tr><th width="33%">Top (GENEL)</th><th width="33%">Middle</th><th width="33%">Bottom (SES)</th></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-top.png" alt="Settings, top" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-middle.png" alt="Settings, middle" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-bottom.png" alt="Settings, bottom" width="100%"></td></tr>
</table>

Scroll with a finger; scrolling over a control does not change it.
"‹ Ayarlar" at the top left goes back.

## GENEL — the whole board

| Row | Choices | What it does |
|---|---|---|
| Tema | Koyu / Açık | dark or light theme |
| Renk | six colours | accent colour: orange, blue, green, purple, pink, yellow |
| Dil | Türkçe / English | language of the board, the tray menu and the texts the PC sends |
| Ekran kapanma | Kapalı / 1 dk / 5 dk / 15 dk | backlight off after that long without a touch |
| Ekranı döndür | off / on | turns the picture and touch by 180°, for a board mounted upside down |
| Açılış ekranı | Dashboard / Müzik / Ses | the tab shown when the board starts |

**Screen off**: when the backlight is off, the first touch only wakes the
screen and selects nothing; tap again to use it. A Claude Code request
or the morning menu warning lights the screen by itself; a notification
banner does not.

## SES — the Audio tab

| Row | Choices | What it does |
|---|---|---|
| Görünüm | Kart / Liste / Karo | layout of the [Audio tab](Audio-Tab#layouts-and-grids) |
| Izgara | 2×2 / 3×2 / 3×3 / Liste | grid of the Kart layout (greyed out in the others) |
| Açılış sekmesi | Son / Tümü / Favori | the Audio sub-tab at start: the last one used, or a fixed one |
| Geçişte ses sınırı | off / on + a slider in 5 % steps | turn an output selected on the board down to the limit first ([details](Audio-Tab#volume-limit-on-switch)) |

## Accent colours

![Six accent colours](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/accents.png)

The accent colours the rail's active button, the active output, sliders,
the play button and the chosen options. The Dashboard's cards keep their
own colours (weather, menu, now playing).

## Light theme and English

<table>
<tr><th width="50%">Light theme</th><th width="50%">English</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/settings-light.png" alt="Light" width="100%"></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-settings.png" alt="English" width="100%"></td></tr>
</table>

<table>
<tr><th width="33%">Dashboard, light</th><th width="33%">Audio tab, light</th><th width="33%">Audio tab, English</th></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-light.png" alt="dash light" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-light.png" alt="audio light" width="100%"></td><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-audio.png" alt="en audio" width="100%"></td></tr>
</table>

## Turning the display

"Ekranı döndür" turns the picture and the touch by 180°. The waiting
screen ("Bilgisayar bekleniyor") also has a button for it at the top
right: **hold it for one second**, then let go — useful when the board is
mounted upside down before the PC program runs.

## Where the settings live

The settings record (12 bytes, [Protocol](Protocol)) is kept by the PC
in `%APPDATA%\stm32-desk-dashboard\preferences.txt`, together with the
outputs' names, favourites and order, and sent to the board when it
connects.

Status: themes, accent colours, layout A and the display flip VERIFIED
on the board; the settings page as a whole and the screen timeout
REQUIRES HARDWARE (`docs/hardware.md`).
