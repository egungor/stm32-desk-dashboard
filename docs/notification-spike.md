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

`dashboard-host notify watch` prints each new toast as each way sees it,
with `delay_ms` (the time between its arrival and when it was seen):
`[listener]` polls `GetNotificationsAsync` every 500 ms (and prints
`gone` when a toast leaves the list), `[event]` is the listener's
`NotificationChanged` event if Windows lets an unpackaged program
subscribe, `[database]` reads the database again every second.

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
| N3 | `notify watch`, then send a notification | `delay_ms` of `[listener]` and `[database]`; is there an `[event]` line, or did `NotificationChanged` fail? The texts as the listener gives them |
| N4 | Turn on "Do not disturb" (focus) and send one | Is it in the database? (The board might then still show it.) |
| N5 | During `notify watch`, dismiss a notification in the notification centre | A `[listener] gone` line? Does it disappear from the database (`notify probe`)? (Whether the board can follow "read".) |
| N6 | Notifications of an app turned off in Settings → System → Notifications | Are they in the database? |

## Results

N1, 2026-10-22, the owner's PC (Windows 11, host from `main` `471519c`):

- **Way 1 works without package identity:** `Current()` OK, `GetAccessStatus` and `RequestAccessAsync` both `Allowed`, `GetNotificationsAsync` returned the one toast in the notification centre (WhatsApp). The documentation's need for identity did not apply here.
- **Way 2 works too:** the live database opened read-only next to the notification service. The tables match the query (`Notification`: `Id`, `HandlerId`, `Type`, `Payload`, `ArrivalTime`, …; `NotificationHandler`: `RecordId`, `PrimaryId`, …), and the same WhatsApp toast was there with its text (`PrimaryId` `5319275A.WhatsAppDesktop_cv1g1gvanyjgm!App`, the package's AppUserModelID).
- Both saw the same single toast.

N2, the owner, with test toasts from Windows PowerShell (`ToastText02`):
the listener gives the display name ("Windows PowerShell") and both
`<text>` lines intact (Turkish letters, an em dash); the database has the
same ids (45655–45658) and times, but only the AppUserModelID as the
app.

N3 and N5, `notify watch` run by Claude Code on the owner's PC with two
tagged test toasts (no other notification arrived during the run):

| | Result |
|---|---|
| `NotificationChanged` | `FAIL 0x80070490` (element not found): an unpackaged program cannot subscribe; the listener has to be polled |
| `[listener]` delay | 131 ms and 504 ms (polled every 500 ms) |
| `[database]` delay | 147 ms and 1058 ms (read every 1 s) |
| Removed from the history (`ToastNotificationManager.History.Remove`, as dismissing in the notification centre) | `[listener] gone id=45663` at the next poll |
| `ClearNotifications` (later, #77, from the tray program) | `FAIL 0x80070490`, as `NotificationChanged` |
| `RemoveNotification(id)` (later, #77, an unpackaged PowerShell, its own test toast) | OK: the toast left the notification centre |

N6, the owner: with the Snipping Tool's notifications turned off, its
screenshot toast appeared in neither way.

N4, the owner, with Do not disturb on: Windows showed no pop-up, but
both test toasts (ids 45668, 45669) were in the listener and in the
database as usual. So Do not disturb only hides Windows' own pop-up; if
the board is to stay quiet too, the host has to learn the state itself
(open point for the ADR, e.g. `SHQueryUserNotificationState`).

**Result:** way 1, `UserNotificationListener` polled every 500 ms, is
enough: documented, no package identity, no administrator rights; app
names, texts and removals; Windows itself leaves out apps whose
notifications are turned off. Way 2 (the undocumented database) and way 3
(a sparse package) are not needed. These are the inputs of the ADR of
#34.

## Report

Paste into #33: the Windows version, N1 (all of it), and N2–N6 with the
texts replaced by `…`. The results go into an ADR (notifications) and
choose the way for #34: listener, database, or listener with a sparse
package; plus the polling interval, which apps are shown, and what
"read" means on the board.
