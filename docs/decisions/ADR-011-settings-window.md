# ADR-011: Settings window (WebView2), versions, settings sent on change

## Status

Accepted (issue #61, continues #6). The owner's request of 2026-10-22: a
window in the Windows program where every setting is made, showing the
program's and the firmware's version; settings are sent when they are
changed, the program does not keep checking files. The owner chose
WebView2 (HTML) for the window and the USB descriptor for the firmware
version.

## Context

Until now the settings were spread over the board's own settings screen
(display, audio), the tray menu (start-up, menu images, picture, food
lists in Notepad, the Claude Code hook) and console commands (`alias`,
`weather`, `picture`). The service found changes made by the console by
polling: `preferences.txt` every second, `weather.txt` and the menu files
every minute, `picture.txt` every 5 s. Neither program could tell which
firmware runs on the board; a host and a board on different protocol
versions only showed `UNSUPPORTED_VERSION` errors in the log.

## Decision

### Window

- `dashboard-tray` opens a top-level window (`WinSettingsWindow`) with a
  WebView2 control on a left click on the tray icon or "Ayarlar…" in the
  menu (a right click shows the menu). The page is `settings.html`, an
  RCDATA resource; it uses no network and no files (a strict CSP).
- WebView2 SDK 1.0.4129.50 (NuGet, BSD-style licence) is fetched by CMake
  into the build folder and checked by SHA-256; the static loader is
  linked, so nothing is installed besides the programs. The runtime is
  part of Windows 11 (Windows 10: the Evergreen runtime); without it the
  tray shows a message with the download link. Only MSVC builds the
  window (the loader is an MSVC library); a MinGW build has no window.
- Pages: General (language, start with Windows, startup screen, screen
  timeout, upside down), Appearance (theme, accent, Audio tab layout,
  grid, startup tab), Audio (the outputs: name on the board, favourite,
  order; volume limit), Dashboard (weather location with a search,
  picture, menu images, Claude API key, the menu's next-day hour, food
  lists), Claude Code (copy
  the hook setting), About (versions, connection, settings folder, log).
  Texts follow the board's language; colours follow Windows' light/dark.
- The page and the tray talk in JSON strings (`host/App/SettingsModel.h`,
  platform independent and unit tested): `{"cmd":...}` requests, checked
  by `parseSettingsRequest` (setting names as in `preferences.txt`,
  ranges as `Proto_SettingsValid`), and `state`, `places` and `notice`
  messages back.

### Settings are sent when they change

- Preferences (the settings record, aliases, favourites, order) belong to
  the service loop. The tray passes the window's request as
  `HostEvent::SettingsRequest`; the loop applies it
  (`applySettingsRequest`), sends the list and `SETTINGS` to the board
  and saves `preferences.txt` (one second later, as for the board's own
  changes). The tray never writes `preferences.txt`, so it cannot race the
  service's save.
- The loop publishes what the window shows (`ServiceSnapshot`:
  connection, versions, settings record, devices) through
  `HostServiceHooks::snapshot` when it changes; an open window is updated
  at once, also when the board changed a setting.
- File settings (`weather.txt`, `picture.txt`, `menu-lists.txt`) are
  written by whoever changes them, who then says so: the tray pushes
  `HostEvent::SettingsChanged`, the console tools set the named event
  `Local\stm32-desk-dashboard-settings` (`WinSettingsSignal`), which the
  service turns into the same event. On it the service reads
  `preferences.txt`, the weather location, the menu files and the picture
  path again. Nothing is polled any more. `claude.txt` (API key) is read
  by the menu import itself.

### Versions

- Firmware: `FW_VERSION` (CI: the tag without "v", else `dev+<commit>`;
  locally `git describe` at configure time) in the USB configuration
  string: `Desk Dashboard fw <version> p<protocol>` (`PROTO_VERSION_TEXT`).
  bcdDevice stays as generated (CubeMX code is not edited).
- Host: `DASHBOARD_VERSION` (the installer workflow passes the setup's
  version; "dev" otherwise).
- `WinHidLink` reads string index 4 when it opens the board
  (`HidD_GetIndexedString`), the service logs it and warns when the
  protocol versions differ; the window shows both versions and the
  warning; `dashboard-host version` prints them. The string is read even
  when the protocols differ, which is the point: the window can say which
  side to update.

## Alternatives

- **Native Win32 dialogs**: no dependency and small, but a plainer look
  and more code per control; the owner chose HTML.
- **The firmware version in a protocol message**: unreadable exactly when
  the versions differ.
- **Keep polling the files**: simple, but wakes the service for nothing
  and lets a change wait up to a minute; the owner asked for changes to
  be sent when made.
- **The window writes `preferences.txt` and signals**: races the
  service's debounced save of a change made on the board.

## Consequences

- New: `host/App/SettingsModel`, `Platform/Windows/WinSettingsWindow`,
  `WinSettingsSignal`, `settings.html`; `HostEvent::SettingsRequest` and
  `SettingsChanged` (replaces `PictureChanged`); `PreferenceStore::reload`
  (`poll` became `onTick`, saving only); `WeatherService::reload`;
  `MenuService::reload` reads only when called.
- Tray menu: "Ayarlar…" first; a left click on the icon opens the window.
- `dashboard-host version`. Console tools that change settings signal
  the running tray.
- No protocol change; `PROTO_VERSION_TEXT` is a text copy of the number.
- REQUIRES WINDOWS: the window (WebView2), every request, the live
  update from the board, the console signal. REQUIRES HARDWARE: the
  firmware version from the USB string.
