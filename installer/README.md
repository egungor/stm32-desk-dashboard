# Windows installer

`stm32-desk-dashboard.iss` (Inno Setup 6) installs the host programs for
the current user, without administrator rights (issue #74):

| | |
|---|---|
| Folder | `%LOCALAPPDATA%\Programs\STM32 Desk Dashboard` |
| Programs | `dashboard-tray.exe` (tray program), `dashboard-host.exe` (console tools) |
| Start menu | "STM32 Masa Paneli" and its uninstaller |
| "Windows açılınca başlat" | checked by default: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `STM32 Desk Dashboard`, the same value the tray menu and `dashboard-host startup on` use |
| After setup | the tray program starts |
| Update | run a newer setup: a running tray program is closed (Restart Manager) and started again |
| Uninstall | closes the tray program, removes the programs and the Run value; the preferences in `%APPDATA%\stm32-desk-dashboard` stay |

Setup speaks Turkish or English (Windows language, or the choice on the
first page).

## Where to get it

The **Host installer** workflow builds it on every change to `host/`,
`protocol/` or `installer/`: Actions → Host installer → the run →
artifact `stm32-desk-dashboard-setup`. On a `v*` tag it is attached to
the GitHub release (created if missing; a tag with a "-", such as
`v1.0.0-beta.1`, makes a pre-release). The **Firmware CI** workflow
attaches the Release firmware to the same release as
`stm32-desk-dashboard-firmware-<version>.elf`, `.hex` and `.bin`:

```text
git tag v1.0.0-beta.1 <commit>
git push origin v1.0.0-beta.1
```

Code signing (#79, README "Code signing policy"): on a `v*` tag the
workflow sends the setup to SignPath Foundation and attaches the signed
file. The owner approves each signing request in SignPath (the step waits
up to an hour). It needs, in the repository settings:

| Kind | Name | Value |
|---|---|---|
| Secret | `SIGNPATH_API_TOKEN` | API token of the SignPath CI user |
| Variable | `SIGNPATH_ORGANIZATION_ID` | SignPath organisation id |
| Variable | `SIGNPATH_PROJECT_SLUG` | the project's slug |
| Variable | `SIGNPATH_POLICY_SLUG` | the signing policy's slug (release signing) |

Without the secret the step is skipped and the setup stays unsigned; then
Windows SmartScreen may show "Windows protected your PC": "More info" →
"Run anyway". A newly signed setup may also warn for a while, until the
certificate has a reputation.

Setup shows the privacy statement first (`privacy-tr.txt`,
`privacy-en.txt`; README "Privacy") and installs `LICENSE.txt` and
`THIRD-PARTY-NOTICES.txt` next to the programs.

## Build by hand (Windows)

```text
cmake -S host -B build/host -DBUILD_TESTING=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build/host --config Release
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 /DFileVersion=0.1.0.0 ^
    /DBinDir=%CD%\build\host\Release installer\stm32-desk-dashboard.iss
```

The setup lands in `installer\Output\`.
