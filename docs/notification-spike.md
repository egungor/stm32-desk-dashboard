# Notification spike (#33)

Goal: find out on this PC how the tray program can learn about Windows
notifications (the owner's request of 2026-10-21: the fourth card, a
list, and a short pop-up on the board, #34), before the feature is
designed. Everything here runs on Windows: REQUIRES WINDOWS. The board is
not needed.

## The ways

| # | Way | Documented | What it needs | Open points |
|---|---|---|---|---|
| 1 | `UserNotificationListener` (`Windows.UI.Notifications.Management`) | yes | the user's consent ("Notification access" in Settings → Privacy); per the documentation also **package identity** (MSIX, or a sparse package for an unpackaged program) and the `userNotificationListener` capability | Does an unpackaged program get an exception, `Denied`, or the toasts? |
| 2 | The notification database `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db` | no | read access to the user's own file (SQLite in WAL mode, kept open by the notification service); Windows' own `winsqlite3.dll` | Can it be opened read-only while in use? Which tables and columns does this Windows have? How soon does a new toast appear in it? |
| 3 | Package identity for way 1: a sparse package (`Add-AppxPackage -ExternalLocation`) signed with a certificate the PC trusts | yes | a signing certificate; trusting a self-signed one needs administrator rights once; the installer registers the package | Only if way 1 needs identity and way 2 is not good enough |
| 4 | UI Automation on the toast pop-ups or the notification centre | partly | nothing extra | Fragile (the shell's UI changes between Windows versions); only what is on screen. Not probed |

Ways 1 and 2 are probed below. Way 3 is a follow-up if the results ask
for it. Way 4 is only a fallback.

## The probe

`dashboard-host notify probe [n]` (default n = 10):

1. Way 1:
   - `UserNotificationListener::Current()`, `GetAccessStatus()` and `RequestAccessAsync()`;
   - with access, `GetNotificationsAsync(Toast)`: the count, and the app of each toast;
   - for every call, the result or the error (HRESULT and its message).
2. Way 2:
   - opens the database **read-only**; if that fails, opens a copy in `%TEMP%` instead (the database and its `-wal` file);
   - prints the table definitions (`sqlite_master`);
   - prints the last n toasts: local time, id, app (the handler's `PrimaryId`) and the `<text>` lines of the payload.

`dashboard-host notify watch` reads the database again every second and
prints each new toast with `delay_ms`, the time between its arrival
(`ArrivalTime`) and when the probe saw it.

The probe only prints; it keeps and sends nothing, and it never writes
to the live database.

**Privacy:** the output holds the texts of your notifications (messages,
e-mail subjects). Before pasting it into #33, replace the texts with
`…`. The times, app names and table definitions are what is needed.

## Setup

1. Install the build of this branch: the "Host installer" artifact of the pull request, or a later build of `main`.
2. Open a console. The program is at:

   ```powershell
   & "$env:LOCALAPPDATA\Programs\STM32 Desk Dashboard\dashboard-host.exe" notify probe
   ```

3. Note the Windows version (`winver`) and whether "Notification access"
   is listed for any app under Settings → Privacy & security.

## Scenarios

| # | Do | Record |
|---|---|---|
| N1 | `notify probe` | Way 1: each line (exception, `Denied`, `Unspecified` or `Allowed` and a count). Way 2: does `open` say "live file" or "a copy"? The table definitions (`Notification`, `NotificationHandler`, …) |
| N2 | Send yourself a few notifications: a message in WhatsApp or Teams, an e-mail in Outlook, a Chrome site's notification, a Windows one (e.g. the Snipping Tool after a screenshot); then `notify probe 20` | Does each appear? App names (`PrimaryId`) as printed; how many `<text>` lines each has and what they are (title, body, …) |
| N3 | `notify watch`, then send a notification | `delay_ms`; does it show at all while the live file is in use? |
| N4 | Turn on "Do not disturb" (focus) and send one | Is it in the database? (The board might then still show it.) |
| N5 | Dismiss a notification in the notification centre, then `notify probe` | Does it disappear from the database? (Whether the board can follow "read".) |
| N6 | Notifications of an app turned off in Settings → System → Notifications | Are they in the database? |

## Report

Paste into #33: the Windows version, N1 (all of it), and N2–N6 with the
texts replaced by `…`. The results go into an ADR (notifications) and
choose the way for #34: listener, database, or listener with a sparse
package; plus the polling interval, which apps are shown, and what
"read" means on the board.
