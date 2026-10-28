# User Guide

How to use the board day to day. The screens are in Turkish by default;
English is one tap away ([Board Settings](Board-Settings) → "Dil").
To install the firmware and the PC program first, see
[Getting Started](Getting-Started#quick-start-from-a-release).

## The rail

![The rail on the left of every tab](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-main.png)

A 56 px rail on the left of the screen switches the tabs. Each tab keeps
its state (page, open detail, sub-tab) while you are on another one.

| Button | Opens |
|---|---|
| ▦ | **[Dashboard](Dashboard)** — clock, weather, menu, now playing, picture, status icons, media strip |
| ♪ | **[Music](Music-Tab)** — what plays on the PC |
| 🔊 | **[Audio](Audio-Tab)** — the Windows playback devices |
| ⚙ (at the foot) | **[Settings](Board-Settings)** of the board |
| plug (at the foot) | the connection mark: the PC program is connected |

The tab shown when the board starts is "Açılış ekranı" in the settings.

## Over every screen

Three things can appear over whatever tab is open:

<table>
<tr><th width="33%"></th><th width="33%">What</th><th width="33%">Page</th></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-prompt.png" alt="dash claude prompt" width="100%"></td><td valign="top">A <strong>Claude Code request</strong>: Onayla / Reddet / Terminalde sor, with a countdown. Lights a dark screen</td><td valign="top"><a href="Claude-Code-Approvals">Claude Code Approvals</a></td></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-banner.png" alt="dash banner" width="100%"></td><td valign="top">A <strong>notification banner</strong> for 5 s; a tap opens the list</td><td valign="top"><a href="Notifications">Notifications</a></td></tr>
<tr><td width="33%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-menu-warning.png" alt="dash menu warning" width="100%"></td><td valign="top">The <strong>morning menu warning</strong> when a listed dish is on today's menu; &quot;Tamam&quot; closes it</td><td valign="top"><a href="Menu-of-the-Day#the-morning-warning">Menu of the Day</a></td></tr>
</table>

A Claude Code request always wins: no banner is shown while one is open.

## On the PC

The tray program `dashboard-tray` serves the board. A **left click** on
its icon opens the [settings window](Settings-Window) — the weather
place, the menu import, food lists, pictures, the Claude Code hook,
names of the outputs and every board setting. A right click opens a
small menu (state, settings, log, exit).

## Messages on the board

![Waiting for the computer](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/waiting.png)

| Message | Meaning, what to do |
|---|---|
| Bilgisayar bekleniyor | The PC program is not connected. Check that the board's **USB OTG FS** cable is in the PC and that the tray program runs |
| Bilgisayar yanıt vermiyor | The PC did not answer within 2 seconds; the board asks for the list again |
| Ses çıkışı değiştirilemedi | Windows refused the switch; try again or check the output in Windows |
| Değişiklik kaydedilemedi | An edit (name, favourite, order) was not saved; try again |
| Ses çıkışı bulunamadı / Favori çıkış yok | No outputs in Windows, or none marked as favourite |
| Ayrıntı alınamadı | A detail (weather, menu, a list) could not be loaded; the board goes back to the cards |

Notices disappear by themselves after 3 seconds. More in
[Troubleshooting](Troubleshooting).

## Tips

- The board has two USB connectors: **ST-LINK** (power, firmware
  updates, log) and **USB OTG FS** (the link to the PC). Keep both
  connected: the board is powered through the ST-LINK connector.
- Only one program serves the board at a time: close the tray program
  before running `dashboard-host run` in a console.
- Turn on "Geçişte ses sınırı" if one of your outputs (headphones, for
  example) is often left loud: switching to it on the board never starts
  above the limit.
- When the screen is dark, the first touch only wakes it. Tap again.
- The picture card and the Music tab are one tap away: tap the picture
  to see it full screen, tap the now-playing card to open the Music tab.
- Every screen of the board, in one place: [Screen Gallery](Screen-Gallery).
