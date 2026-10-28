# Troubleshooting

## Firmware build stops with "LVGL not found"

The LVGL submodule is missing: `git submodule update --init`.

## CMake is the wrong version / the preset fails

The system CMake (3.26.3 on the developer PC) is first on the PATH. Put the
STM32Cube bundles first, as in [Getting Started](Getting-Started#2-build-the-firmware-windows-powershell);
`cmake --version` must print 4.4.0. From Git Bash convert the path with
`cygpath` (a Windows-style path in the Bash PATH is silently ignored).

## No log lines

- The log is on the **ST-LINK** USB connector, 115200 8N1. The COM port
  number differs per PC: `Get-CimInstance Win32_SerialPort`.
- Open the port before the reset (`read-vcp.ps1 -Reset`), or the first
  lines are lost. Only one program can hold the port.
- Close a VS Code debug session before flashing; it holds the ST-LINK.

## `SELFTEST SDRAM FAIL`

Note the `test=` and the address. The LTDC must be off during SDRAM init
(see [Hardware](Hardware#bring-up-lessons)); `STM32_Programmer_CLI -r32`
reads SDRAM words directly.

## The host does not find the board

- The PC link is the **USB OTG FS** micro-USB connector, not ST-LINK.
- The log must show `USB CONFIGURED`; Device Manager must list VID `1209`
  PID `0002` (`0001` is the audio controller's firmware: flash this
  project's firmware). A firmware built with `APP_UI_DEMO` does not use
  the PC link.
- The tray program (or `run`) scans once a second. Only one of them serves
  the board at a time; a second one says another program serves the
  controller and exits.

## The LCD shows the waiting screen ("Bilgisayar bekleniyor")

No host is talking to the board: start the tray program (Start menu,
"STM32 Masa Paneli") or `dashboard-host run`. After a selection that
the host did not answer in 2 s the board also returns to the waiting
screen and asks for the list again.

## An output gets quieter when I tap it

That is the volume limit: "Geçişte ses sınırı" on the settings page turns
an output down to the limit when you select it on the board
([Audio Tab](Audio-Tab#volume-limit-on-switch)). The tray program's log ("Günlüğü
aç") shows `Volume limit: "<name>" <from>% -> <to>%`. Turn the limit off
or raise it.

## The first touch does nothing

The screen was dark ("Ekran kapanma"): the first touch only turns the
backlight on. Tap again.

## `set-default` fails

Switching uses the undocumented `IPolicyConfig` interface
([ADR-002](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-002-default-device-switching.md)). If a
Windows update breaks it, `set-default` reports FAIL and the board shows
the notice "Ses çıkışı değiştirilemedi"; the rest keeps working.

## The settings window does not open

It needs the Microsoft Edge WebView2 Runtime (part of Windows 11; on
older systems install the "Evergreen" runtime from Microsoft). Without
it, the tray program says so when you click the icon. Its cache is in
`%LOCALAPPDATA%\stm32-desk-dashboard\WebView2`; deleting that folder
while the tray program is closed resets it.

## The settings window warns about the protocol

The About page compares the protocol of the program with the board's.
Update the side it names: flash the firmware of the same release as the
installer, or install the newer setup.

## No notifications on the board

- Windows Settings → Privacy & security → Notifications: let apps access
  your notifications. Without it the list says "Bildirim erişimi kapalı".
- No banners while Do not disturb is on, while a Claude Code request is
  shown or while the screen is dark: the bell and the list still update.
  `dashboard-host notify focus` shows what Windows reports.

## Claude Code requests do not reach the board

- The hook must be in `%USERPROFILE%\.claude\settings.json` (the user
  settings of this PC), copied from the settings window → Claude Code.
  Start a new Claude Code session after adding it.
- The tray program must run and the board must be connected; otherwise
  the request is asked in the terminal (by design: nothing is allowed by
  default).
- After an update that changed the snippet, copy it again.
- `host.log` shows `Claude Code request …` lines for every request.

## The menu card says "Menü yüklenmedi"

No menu for this month: import the images or saved portal pages
([Menu of the Day](Menu-of-the-Day#importing-a-month)). A failed import
shows a balloon with the reason (no API key, an image over 5 MB, more
than 10 images, an answer that did not pass the checks).

## The weather card shows "Güncellendi hh:mm"

The last fetch failed (no network, Open-Meteo down); the card keeps the
last data and tries again in 5 minutes. Without a place set, nothing is
fetched: set one in the settings window.

## The picture does not change

A new picture takes about 20 seconds to reach the board and is shown
only when complete. `dashboard-host picture` checks that the shown
picture loads. A picture added while the tray program was closed is read
when it starts.
