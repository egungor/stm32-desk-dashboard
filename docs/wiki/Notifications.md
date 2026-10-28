# Notifications

The board shows the notifications (toasts) Windows shows — WhatsApp,
Teams, Outlook, a browser, Windows Update, any app — as a short banner
when they arrive, and as a list behind the bell on the
[Dashboard](Dashboard). There is no list of apps to choose from: what
Windows shows, the board shows.

## The banner

![A banner over the Dashboard](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-banner.png)

A strip over the top of **every screen** for 5 seconds (the countdown is
at its top right): the bell and the app, then two lines — the sender or
title, and the text (cut with "…" when it is longer). A toast without
texts shows only the app.

- A **tap** opens the notifications list on the Dashboard. On the
  settings page or in the edit mode a tap only closes the banner, so a
  half-done change is not lost.
- A burst of toasts shows one banner, then the newest after 2 seconds.
- **No banner** while Windows' *Do not disturb* is on, while a
  [Claude Code request](Claude-Code-Approvals) is shown (a request never
  waits behind a banner), or while the screen is dark (it stays dark).
  The list and the bell still update.
- Toasts that were already there when the PC program starts or the board
  reconnects are not new: no banner for them.

![A banner over the Audio tab](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/audio-banner.png)

*The same banner over the Audio tab.*

## The bell and the list

The bell at the top right of the Dashboard counts the toasts in the
Windows notification centre. It is red while there are toasts you have
not seen in the list.

![Notifications list](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-notifications.png)

- The newest 20 toasts, newest first: the app and the time as a section
  title, then the sender or title and the text.
- A toast dismissed on the PC leaves the list (and the bell's number)
  within a second; the open list follows by itself.
- **The bin** at the top right clears the Windows notification centre,
  toast by toast; the list then says "Bildirim yok" and the bell has no
  number. It is shown only when there are toasts.

## Set-up

Nothing to install: the tray program reads the notifications through
Windows' notification listener (polled every 500 ms). Windows has to
allow it:

**Settings → Privacy & security → Notifications → let apps access your
notifications.**

Without the permission the list says "Bildirim erişimi kapalı" and
names that setting; if the listener fails, "Bildirimler okunamıyor" and
the program tries again every 30 seconds.

## Privacy

The texts of the notifications go to the board over USB and nowhere
else. `host.log` logs the app names, never the texts.

## Console

```text
dashboard-host notify          the card, the list and the banner of the newest toast, as the board gets them
dashboard-host notify watch    print new toasts as they arrive (Ctrl+C stops)
dashboard-host notify focus    whether Windows accepts pop-ups (Do not disturb), at every change
```

Design: [ADR-009](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-009-notifications.md).
Status: the Windows and board checks are open ([Hardware Test Checklist](Hardware-Test-Checklist)).
