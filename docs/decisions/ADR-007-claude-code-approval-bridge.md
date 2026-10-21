# ADR-007: Claude Code approvals on the board

## Status

Proposed (issue #5). Open points are settled by the spike of Dashboard
v1 step 10 and recorded here; the bridge is built in step 11
(`docs/roadmap.md`).

## Context

When a Claude Code session wants to run a tool that needs permission
(a shell command, a file edit), it asks in the terminal. The user wants
to answer these requests on the board (2026-10-21). Claude Code sessions
may run on other machines too; approvals on the board are wanted only
for sessions **on this PC**.

Claude Code has a `PermissionRequest` hook event that fires when a tool
call needs a permission decision. A hook can be a command (input JSON
on stdin), an HTTP endpoint and others; it answers with
`hookSpecificOutput.decision.behavior` `allow` or `deny`. Exit code 2 is
not honoured for this event; a hook that ends without a decision leaves
the permission flow unchanged. The default timeout of command hooks is
600 s. On Windows without Git Bash a shell-form `PermissionRequest`
command hook that fails can block the tool call. Cloud sessions do not
read the local `~/.claude/settings.json`.
Source: Claude Code hooks reference, https://code.claude.com/docs/en/hooks
(read 2026-10-21).

## Decision

### Path of a request

```text
Claude Code ──stdin JSON──► dashboard-host hook permission
                                   │ named pipe (this user only)
                                   ▼
                            dashboard-tray ──PROMPT_PART──► board
                                   ▲                          │ tap
                                   └──── ACTION kind 1 ◄──────┘
dashboard-host ◄── decision ── dashboard-tray
Claude Code ◄── stdout JSON (allow / deny) or nothing
```

1. **Command hook, not HTTP.** The existing console program gets a mode
   `dashboard-host hook permission`. It reads the hook input from stdin,
   passes it to the running tray program over a named pipe, waits for
   the answer and prints the decision. An HTTP hook would need an HTTP
   server and an open port in the tray program.
2. **Named pipe** `\\.\pipe\stm32-desk-dashboard-hook`, created by the
   tray program with a security descriptor that admits only the current
   user. One request per connection: one JSON line in, one JSON line
   out.
3. **The tray program** turns a request into a prompt (ADR-006): title
   "Claude Code izin istiyor", subtitle = project folder name and the
   queue position ("ayrac · 1/2"), rows = tool name and its main input
   (the command, or the file path), buttons Onayla / Reddet / Terminalde
   sor. Requests wait in a queue; the board shows one at a time.
4. **Answers:**

   | On the board | Hook output | Effect in Claude Code |
   |---|---|---|
   | Onayla | `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}` | tool runs |
   | Reddet | same with `"behavior":"deny"` | tool call refused |
   | Terminalde sor | nothing, exit 0 | the normal question in the terminal |
   | no answer in 60 s | nothing, exit 0 | the normal question |
   | tray not running, board not connected, pipe error | nothing, exit 0 | the normal question |

   The default is always "no decision", never "allow".
5. **Settings snippet.** The installer does not touch Claude Code's
   settings. The tray menu copies a snippet for `~/.claude/settings.json`
   **of this PC** (user settings, not a repository's `.claude/`), so the
   hook never runs on other machines or in cloud sessions:

   ```json
   {
     "hooks": {
       "PermissionRequest": [
         {
           "matcher": "Bash|Edit|Write",
           "hooks": [
             { "type": "command", "command": "\"<install folder>\\dashboard-host.exe\" hook permission", "timeout": 90 }
           ]
         }
       ]
     }
   }
   ```

   No `onFailure: "block"`: with it a closed tray program would refuse
   every request.

### What the board shows must be what is approved

The prompt is limited to 1024 bytes (ADR-006). If the tool input does
not fit, the host cuts it, marks it "(kısaltıldı)" and the board shows
**only Reddet and Terminalde sor** — a request the user cannot read in
full is never approvable on the board.

### Not in v1

- "Always allow" (`updatedPermissions`) and changing the tool input
  (`updatedInput`): the hook supports both; they widen what one tap can
  do and wait for a separate decision.
- The `Notification` hook (`idle_prompt`: "Claude is waiting for input")
  and `Stop` ("task finished") as dashboard cards: same pipe, later.

### Exec form (hooks reference, read again 2026-10-21 for step 10)

A command hook with `args` runs in **exec form**: Claude Code starts
`command` as a program, without a shell, and on Windows it must be a real
`.exe` (not a `.cmd` / `.bat`). That is the case of `dashboard-host.exe`,
so the hook does not depend on Git Bash or PowerShell, and the
shell-form failure on Windows without Git Bash does not apply. The
snippet of step 11 becomes `"command": "<install folder>\\dashboard-host.exe",
"args": ["hook", "permission"]`. A program that is missing, fails or
times out is a non-blocking error unless `onFailure: "block"` is set
(which on `PermissionRequest` would deny): to be confirmed by the spike.

### Open points for the spike (step 10)

The probe and the scenarios are in `docs/hook-spike.md` (#24); results
go into "Spike results" below.


- Is the terminal question shown while the hook waits? If both can
  answer, which one wins and does the late one cause harm?
- The exact input fields on this Claude Code version (`tool_name`,
  `tool_input`, `cwd`, `session_id`) and their shape for Bash, Edit,
  Write.
- How the hook command runs on Windows without Git Bash, and what a
  missing or crashing exe does to the tool call.
- Whether 60 s / 90 s fits real use.

## Spike results

Not run yet (#24, `docs/hook-spike.md`): REQUIRES WINDOWS.

## Alternatives

- **HTTP hook to a local server in the tray program:** no extra process
  per request, but an HTTP server, a port and its access rules.
- **Answer automatically by rules** (allow `git status`, …): Claude
  Code's own permission rules already do this; the board is for the
  cases a person decides.
- **Remote/cloud sessions on the board:** would need a service reachable
  from outside the PC; not wanted (decision of 2026-10-21).

## Consequences

- `dashboard-host` gains a third role (hook client) next to the console
  tools; it must start fast and never block longer than its own 60 s.
- The tray program owns a pipe server thread; its events go through the
  existing `EventQueue`, like HID and Core Audio events.
- Unit tests cover the decision JSON, the timeout, the cut-input rule
  and the no-tray case without Windows; the end-to-end check with a real
  session REQUIRES WINDOWS and REQUIRES HARDWARE.
