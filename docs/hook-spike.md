# Claude Code approval spike (Dashboard v1 step 10, #24)

Goal: answer the open points of ADR-007 on this PC before the bridge
(step 11) is built. Everything here runs on Windows with a real Claude
Code session: REQUIRES WINDOWS. The board is not needed.

## The probe

`dashboard-host hook probe` is a `PermissionRequest` hook that does
nothing but observe:

- it appends two JSON lines per call to
  `%APPDATA%\stm32-desk-dashboard\hook-probe.log`: `start` (time, process
  id, the complete hook input) and `end` (time, elapsed ms, what it
  answered);
- it answers as `%APPDATA%\stm32-desk-dashboard\hook-probe.txt` says,
  read on every call, so scenarios change without restarting Claude
  Code:

  ```text
  answer=none        # none (no decision), allow or deny
  wait=0             # seconds before answering (0..600)
  exit=0             # exit code (3 = a failing hook)
  ```

No file means `answer=none`, `wait=0`, `exit=0`: Claude Code asks in the
terminal as without the hook.

## Setup

1. Install the build of this branch (the "Host installer" artifact of
   the pull request, or a later release).
2. The full path of the program:

   ```powershell
   "$env:LOCALAPPDATA\Programs\STM32 Desk Dashboard\dashboard-host.exe"
   ```

3. Add the hook to the **user** settings of this PC,
   `%USERPROFILE%\.claude\settings.json` (merge with what is there). Exec
   form (`command` + `args`): Claude Code starts the exe directly, without
   Git Bash or PowerShell. Write the path with `\\`:

   ```json
   {
     "hooks": {
       "PermissionRequest": [
         {
           "matcher": "*",
           "hooks": [
             {
               "type": "command",
               "command": "C:\\Users\\<you>\\AppData\\Local\\Programs\\STM32 Desk Dashboard\\dashboard-host.exe",
               "args": ["hook", "probe"],
               "timeout": 90
             }
           ]
         }
       ]
     }
   }
   ```

4. Start a new Claude Code session in a throw-away folder (e.g.
   `C:\temp\hook-test`) in the default permission mode, so that commands
   and edits ask. `/hooks` should list the hook.
5. Note the Claude Code version (`claude --version`) and whether Git for
   Windows is installed.

Remove the hook from `settings.json` when done.

## Scenarios

For each: set `hook-probe.txt`, ask Claude for the action, write down
what the terminal showed and when, and keep the log lines.

| # | `hook-probe.txt` | Ask Claude to | Record |
|---|---|---|---|
| S1 | (none) | run `dir`; create `a.txt`; edit `a.txt`; fetch a web page | Tool names and the full input of each (`tool_name`, `tool_input`, `cwd`, `permission_mode`, other fields) from the log; is the terminal question shown? |
| S2 | `answer=allow` | run `dir` | Does the command run without a question? Time from the log's `start` to the result |
| S3 | `answer=deny` | run `dir` | What does Claude see and say? |
| S4 | `answer=none` `wait=20` | run `dir` | Is the terminal question shown **during** the 20 s, or only after the hook ends? |
| S5 | `answer=deny` `wait=20` | run `dir`, and answer **Yes** in the terminal before 20 s if the question shows | Which answer wins? Does the late `deny` change anything or cause an error? |
| S6 | `answer=none` `wait=120` (hook timeout 90) | run `dir` | What happens at 90 s: the terminal question, a refusal, an error text? Is the hook process killed (no `end` line)? |
| S7 | `exit=3` | run `dir` | A failing hook: question, refusal or error text? |
| S8 | (none), `command` changed to a path that does not exist | run `dir` | A missing exe: question, refusal or error text? Then put the path back |
| S9 | `answer=none` `wait=10` | a task with two or three commands in parallel (e.g. subagents) | Do hook calls overlap (`pid`, times)? |
| S10 | `answer=allow` | run `dir` ten times | Start-up time: `elapsed_ms` of `start` and the time between Claude's request and the command |

Also note: does a `PermissionRequest` fire for edits in the
`acceptEdits` mode (`Shift+Tab`)? Is there a tool named `PowerShell`
instead of `Bash` on this PC?

## Report

Paste into issue #24: the Claude Code version, Git for Windows yes/no,
the S1–S10 notes, and `hook-probe.log` (or the lines of each scenario).
The results go into ADR-007, "Spike results", and set the values of
step 11: the matcher, the hook timeout, the board's answer timeout, and
what the host does when the tray or the board is missing.
