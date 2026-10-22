# ADR-009: Windows notifications on the board; protocol version 5

## Status

Proposed (issue #34). Based on the spike #33 (`docs/notification-spike.md`)
and the owner's decisions of 2026-10-22.

## Context

The owner wants Windows notifications on the board: the fourth card
(slot 2, kept for it since ADR-006), a list of them, and a short pop-up
when one arrives.

The spike showed on the owner's PC (Windows 11):

- `UserNotificationListener` works for the unpackaged tray program: no
  package identity, no administrator rights; access `Allowed`.
- Polled with `GetNotificationsAsync(Toast)` it gives each toast's id,
  time, app display name and AppUserModelID, and the `ToastGeneric`
  texts (title, body) intact; a toast removed in Windows leaves the list
  at the next poll. Delay 0.1–0.5 s at a 500 ms poll.
- Its `NotificationChanged` event fails for an unpackaged program
  (`0x80070490`): polling it is.
- Windows leaves out apps whose notifications are turned off (N6). Do
  not disturb only hides Windows' own pop-up; the toasts are still
  listed (N4).

Decisions of the owner (2026-10-22): every app Windows shows
notifications for (no own app list); while Do not disturb is on, no
pop-up on the board, but the card and the list still update.

## Decision

### On the host

- A small interface `INotificationSource` (the toasts now: id, time, app,
  title, body) in `host/Notify/`, and `WinNotificationListener` in
  `host/Platform/Windows/` (the C++/WinRT library `host_media_win`, C++20,
  as `WinMediaSession`): `UserNotificationListener` polled every 500 ms
  on a worker thread; a change queues `HostEvent::NotificationsChanged`.
  The rest of the host does not see WinRT.
- An interface `IFocusState` (Do not disturb on or off) with a Windows
  implementation; the candidate is `SHQueryUserNotificationState`
  (documented, shell32): `QUNS_ACCEPTS_NOTIFICATIONS` (5) means pop-ups
  may show. On the owner's PC it returns 5 with Do not disturb off; what
  it returns with it on is checked in #34 before the pop-up is built on
  it. If it does not follow Do not disturb, the fallback is the
  `NOC_GLOBAL_SETTING_TOASTS_ENABLED` value under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings`
  (undocumented), or no Do not disturb rule.
- A `NotificationService` (platform independent, tested):
  - card slot 2: "BİLDİRİMLER", the count as badge, the newest three as
    `app: title` lines; "Bildirim yok" without any;
  - its detail: the newest 20, each a section `#app · hh:mm` and a row
    with the title and the body; no buttons (the list mirrors Windows:
    a toast dismissed on the PC leaves the board at the next poll);
  - a pop-up (below) for each toast that is new since the last poll,
    not when the host starts or reconnects (those are not new), not
    while Do not disturb is on, and at most one every 2 s (a burst shows
    the newest).
- Texts follow the board's language for the fixed words. Notification
  texts go to the board over USB; `host.log` logs frames without their
  record text (as today), so the texts are not written to disk.

### Thin firmware still holds

The card and the detail are the generic records of ADR-006. The pop-up
is one more generic component, the **banner**: a strip over the top of
every screen with an icon, a title, up to two lines and a countdown; a
tap opens the detail of a card slot the record names, or just closes it.
It knows nothing about notifications.

The prompt (ADR-007) is not reused: it is modal, has buttons, keeps the
screen lit until answered and is queued one at a time; a banner is none
of that and must never hide or delay a Claude Code prompt.

### Protocol version 5

| Code | Command | Direction | Payload |
|---|---|---|---|
| `0x29` | `BANNER_PART` | Host → STM32 | part header (`id` = banner id 0–255) + record text |

Banner record, at most 256 bytes (5 parts), first line
`icon ␟ title ␟ seconds ␟ slot`, then up to two text lines:

- `icon`: the icon numbers of `protocol.md` ("Icon"); this ADR adds
  **12 = bell** (notifications), also for the card;
- `seconds`: how long it stays (1–30; empty = 5);
- `slot`: card slot 0–7 whose detail a tap opens, empty = a tap only
  closes it.

A newer banner replaces the one shown. A banner never covers a prompt:
while a prompt is shown, the board drops banners. The banner does not
wake a dark screen (the screen timeout, ADR-004); the card still
changes.

Why a version: an older board answers `BANNER_PART` with
`UNKNOWN_COMMAND`. Firmware and host are released together (ADR-008),
so both move to 5.

### Board memory

One banner record being assembled in `dash_client.c` and the shown one
in `g_ui` (257 bytes each); the banner's LVGL objects exist while it is
shown. No change to the cover buffers.

## Alternatives

- **The notification database** (`wpndatabase.db`, spike way 2): works,
  but undocumented and has only the AppUserModelID as app name.
  Rejected while the listener works.
- **A sparse package for identity** (spike way 3): would allow the
  `NotificationChanged` event instead of polling, but needs a signing
  certificate the PC trusts (administrator once). Not needed now.
- **The prompt as pop-up**: modal and queued with Claude Code approvals;
  rejected (above).
- **A banner flag in the card record** (no new command, no version):
  the card has no time-out or tap target; mixing them makes both
  harder. Rejected.
- **An own app list on the host**: the owner chose Windows' own
  per-app setting instead.

## Consequences

- Protocol version 5; firmware and host updated together.
- `protocol.md` (command, record, icon 12), codec and tests; host
  (`INotificationSource`, `WinNotificationListener`, `IFocusState`,
  `NotificationService`); board (banner, icon 12, `dash_client.c`).
- Notification texts are shown on a desk display: anyone at the desk can
  read them. A setting to hide the texts (only the app) could follow if
  the owner wants it.
- REQUIRES WINDOWS: the Do not disturb state through
  `SHQueryUserNotificationState`; which apps' toasts carry `ToastGeneric`
  texts (some may show only the app name). REQUIRES HARDWARE: the banner
  over every screen, never over a prompt.
