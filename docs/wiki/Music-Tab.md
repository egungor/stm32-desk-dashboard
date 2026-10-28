# Music Tab

The second button on the rail (♪). It shows what plays in Windows'
media session — the same player the Windows volume flyout shows:
Spotify, Deezer, a YouTube tab in the browser, the Media Player — and
controls it.

![Music tab, playing](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-playing.png)

| Part | What |
|---|---|
| Cover | 120 × 120, as the player publishes it (cut to a square); a music note without one. A new track's cover arrives within about 3 seconds |
| Title | two lines at most |
| Artist, album, app | the app is the player ("Spotify", "Chrome"); "· Duraklatıldı" while paused |
| Progress | position and duration, counted on once a second while playing |
| ⏮ ▶/⏸ ⏭ | previous, play or pause, next; a control the player does not offer is greyed out |
| Volume (right edge) | the Windows default output's volume and mute; the same as on the Dashboard strip and the Audio tab |

## Jumping in the track

When the player can seek, the progress bar has a knob: **tap** the bar
to jump there, or **drag** the knob — the time under your finger is shown
and the jump happens when you let go. Players that cannot seek (some
browser tabs) show the bar without a knob and ignore taps.

## Paused and nothing playing

<table>
<tr><th width="50%">Paused</th><th width="50%">Nothing playing</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-paused.png" alt="Paused" width="100%"><br>The track stays with &quot;Duraklatıldı&quot;; play resumes it</td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-none.png" alt="Nothing playing" width="100%"><br>&quot;Şu an çalan bir şey yok&quot;; the buttons stay enabled and go out as Windows media keys</td></tr>
</table>

When one player pauses and another starts, the tab follows the one that
plays.

## Light theme and English

<table>
<tr><th width="50%">Light</th><th width="50%">English</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/music-light.png" alt="Light" width="100%"></td><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/en-music.png" alt="English" width="100%"></td></tr>
</table>

## Start on the Music tab

[Board settings](Board-Settings) → "Açılış ekranı" → **Müzik** opens
the Music tab when the board starts.

## How it works

The tray program reads Windows' media session (`Windows.Media.Control`)
every 500 ms on its own thread, converts a new cover to 120 × 120 RGB565
and sends it in parts (about 2.7 seconds, without holding up other
frames). The controls go to the session; without a session, or when it
refuses, they go out as media keys. `dashboard-host media` prints what
the program sees; `dashboard-host media next` sends a control.

Status: VERIFIED on the board and Windows with Deezer (2026-10-22). Other
players and seeking: REQUIRES WINDOWS + HARDWARE. Design:
[ADR-008](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-008-media-session-and-music-tab.md).
