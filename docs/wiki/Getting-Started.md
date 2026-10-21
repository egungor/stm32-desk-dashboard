# Getting Started

## Quick start from a release

No build needed. From the [Releases page](https://github.com/egungor/stm32-desk-dashboard/releases) take the latest
`stm32-desk-dashboard-setup-<version>.exe` and
`stm32-desk-dashboard-firmware-<version>.elf`.

1. **Flash the board.** Connect the board's **ST-LINK** USB connector to
   the PC, install
   [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html),
   then:

   ```text
   STM32_Programmer_CLI -c port=SWD -w stm32-desk-dashboard-firmware-<version>.elf -v -rst
   ```

   The output ends with `Download verified successfully`. The `.hex` and
   `.bin` files are the same firmware for other tools (`.bin` at
   `0x08000000`).
2. **Install the host.** Run the setup. It installs for the current user,
   without administrator rights, and starts the tray program. It is not
   code-signed: if SmartScreen warns, "More info" → "Run anyway".
3. **Connect.** Plug the board's **USB OTG FS** micro-USB connector into
   the PC. The tray icon says "Kumanda bağlı" and the LCD shows the
   Dashboard tab; the speaker on the left rail opens the playback
   devices.

How to use it: [User Guide](User-Guide). To build everything yourself,
read on.

## What you need

- STM32F746G-DISCO board (MB1191) and two USB cables: one to the
  **ST-LINK** connector (power, flashing, log) and one to the **USB OTG
  FS** micro-USB connector (the link to the PC).
- A Windows PC for flashing, the log and the host program.
- For the firmware: the ST toolchain installed through *STM32Cube for VS
  Code* (versions in [toolchain.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/toolchain.md)).
- For the host program: Visual Studio 2022 or newer with the C++ desktop
  workload, and CMake.

## 1. Clone

```text
git clone https://github.com/egungor/stm32-desk-dashboard
cd stm32-desk-dashboard
git submodule update --init      # LVGL; without it the firmware build stops
```

## 2. Build the firmware (Windows, PowerShell)

The STM32Cube bundles are not on the PATH, and an older system CMake may
be. Put the bundles first for the session:

```powershell
$b = "$env:LOCALAPPDATA\stm32cube\bundles"
$env:PATH = "$b\cmake\4.4.0+st.1\bin;$b\ninja\1.13.2+st.1\bin;" +
            "$b\gnu-tools-for-stm32\14.3.1+st.2\bin;$b\programmer\2.23.0\bin;$env:PATH"
cd firmware\STM32AudioController
cmake --preset Debug
cmake --build --preset Debug
```

Build options (add to the configure command, e.g.
`cmake --preset Debug -DAPP_TEST_SCREEN=ON`):

| Option | Effect |
|---|---|
| default | The UI, fed by the PC over USB |
| `-DAPP_TEST_SCREEN=ON` | M1 test pattern and five touch targets (touch check) |
| `-DAPP_UI_DEMO=ON` | The UI with a fixed demo device list, no PC needed |

On Linux, `tools/build-firmware-linux.sh [Debug|Release]` builds the same
project (secondary build, used by CI).

## 3. Flash and read the log

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/STM32AudioController.elf -v -rst
powershell -ExecutionPolicy Bypass -File ..\..\tools\read-vcp.ps1 `
    -Port COM4 -Reset -Until '^(APP READY|.*FAIL)'
```

`COM4` is the ST-LINK virtual COM port on the developer PC; check yours
with `Get-CimInstance Win32_SerialPort`. The log lines are listed in
[Firmware](Firmware#log-lines).

## 4. Build and run the host program

```text
cmake -S host -B build/host
cmake --build build/host --config Release
ctest --test-dir build/host --build-config Release --output-on-failure
```

With the Visual Studio generator the program is
`build\host\Release\dashboard-host.exe`.

```text
dashboard-host list          # playback devices, * = default
dashboard-host run           # serve the board over USB HID (Ctrl+C stops)
```

All commands: [Host Application](Host-Application).

## 5. Try the UI without a board

```text
tools/run-ui-simulator.sh      # Linux; writes PNG screenshots to build/ui-sim
```

The *UI Simulator and Link CI* job also uploads them as the
`ui-screenshots` artifact.
