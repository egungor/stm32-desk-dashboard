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

The setup is not code-signed, so Windows SmartScreen may show "Windows
protected your PC": "More info" → "Run anyway".

## Build by hand (Windows)

```text
cmake -S host -B build/host -DBUILD_TESTING=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build/host --config Release
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 /DFileVersion=0.1.0.0 ^
    /DBinDir=%CD%\build\host\Release installer\stm32-desk-dashboard.iss
```

The setup lands in `installer\Output\`.
