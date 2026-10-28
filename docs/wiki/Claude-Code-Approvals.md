# Claude Code Approvals

When a Claude Code session **on this PC** wants to run a command, edit
a file or use another tool that needs your permission, the board asks
you — over every screen, with a countdown — and sends your answer back to
Claude Code. You can keep working in another window and answer with one
tap.

![A permission request on the board](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-prompt.png)

## The request

- **Header**: "Claude Code izin istiyor", the project folder and, when
  several requests wait, which one this is ("1/2"); the countdown at the
  right (60 s).
- **Rows**: what the tool wants to do, in the tool's own terms:

  | Tool | Rows |
  |---|---|
  | Bash, PowerShell | Araç (tool), Komut (the command), Açıklama (Claude's description) |
  | Edit | Araç, Dosya (file), Eski (old text), Yeni (new text), Tümü (replace all) |
  | Write, NotebookEdit | Araç, Dosya, İçerik (content) |
  | MultiEdit | Araç, Dosya, Değişiklikler (edits) |
  | WebFetch | Araç, Adres (URL), İstem (prompt) |
  | any other tool | Araç, Girdi (its input as JSON) |

- **Buttons**:

  | Button | Claude Code |
  |---|---|
  | **Onayla** | allows this one request |
  | **Reddet** | denies it |
  | **Terminalde sor** | asks in the terminal, as without the board |

<table>
<tr><th width="50%">An edit</th><th width="50%">A request too long for the screen</th></tr>
<tr><td width="50%" valign="top"><img src="https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-prompt-edit.png" alt="Edit request" width="100%"></td><td valign="top">Marked &quot;(kısaltıldı: tamamı ekrana sığmıyor, burada onaylanamaz)&quot; and only <strong>Reddet</strong> and <strong>Terminalde sor</strong>: what is not shown cannot be approved</td></tr>
</table>

The request lights a dark screen and keeps it lit until it is answered.
Notification banners wait: a request never hides behind one.

## Nothing is allowed by default

If you do not answer within the countdown, if the tray program is not
running, if the board is unplugged, or if the request did not fit the
screen, the decision goes back to the terminal: Claude Code asks there as
it would without the board. The board can say yes only when you tap
**Onayla**.

## Answered somewhere else

A request answered in Claude Desktop (Code tab) or in the terminal
leaves the board by itself: when the command has run, or — after a
refusal — when the turn ends or you write again. The list shows it as
"PC'de cevaplandı". (This needs the hook snippet of `v0.9.0-beta.10` or
later: copy it again after updating.)

## The list

The Claude Code symbol at the top right of the [Dashboard](Dashboard)
opens the list: requests still waiting, then the last decisions with the
time and the result.

![Claude Code list](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/dash-claude-list.png)

| Result | Meaning |
|---|---|
| onaylandı | allowed on the board |
| reddedildi | denied on the board |
| terminalde | sent back to the terminal (button) |
| süre doldu | no answer within the countdown |
| kart yok | no board connected |
| iptal | withdrawn before an answer: the hook command went away (Claude Code stopped waiting) or the tray program was closed |
| PC'de cevaplandı | answered in Claude Desktop or the terminal |

The symbol's colour tells you what happened while you were away: **red**
while a request waits (with the number), **yellow** after one timed out,
**green** after an allow. Opening the list makes it normal again. The bin
at the top right clears the decisions.

## Set-up (once)

1. Open the settings window (left click on the tray icon) → **Claude
   Code** → **Kopyala**.

   ![Claude Code page of the settings window](https://raw.githubusercontent.com/egungor/stm32-desk-dashboard/main/docs/images/screens/win-claude-light.png)

2. Merge the copied JSON into `%USERPROFILE%\.claude\settings.json` —
   the user settings of this PC, not a repository's `.claude/`.
3. Start a new Claude Code session. The next permission request shows up
   on the board.

The snippet registers two commands of the installed `dashboard-host`:

| Hook | Command | Does |
|---|---|---|
| `PermissionRequest` | `dashboard-host hook permission` | hands the request to the tray program over the named pipe `\\.\pipe\stm32-desk-dashboard-hook` and waits for the answer |
| `PostToolUse`, `PostToolUseFailure`, `Stop`, `UserPromptSubmit` | `dashboard-host hook resolved` | closes requests that were answered in Claude Desktop or the terminal |

## Status

Built and tested in CI (unit tests and the loopback integration test);
how the hook behaves on Windows in every corner case is still being
checked (spike, issue #24, `docs/hook-spike.md`), and the board check is
open. Design:
[ADR-007](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/decisions/ADR-007-claude-code-approval-bridge.md).
