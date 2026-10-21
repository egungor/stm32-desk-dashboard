# Testing

## What CI runs on every pull request

| Workflow | Runs on | What |
|---|---|---|
| Firmware CI | Ubuntu | Firmware build, Debug and Release (Arm GCC 14.3.Rel1); `.elf` artifacts |
| Protocol CI | Ubuntu, Windows | Codec tests: encode/decode, CRC, validation |
| Host CI | Ubuntu, Windows (MSVC) | Host core tests; the Windows program is built on Windows |
| UI Simulator and Link CI | Ubuntu | UI screenshots (artifact `ui-screenshots`); STM32 protocol client tests |
| Integration CI | Ubuntu, Windows (MSVC) | Firmware ↔ host loopback test |

A green CI run does **not** mean anything works on the board or on
Windows. Those checks are in the [Hardware Test Checklist](Hardware-Test-Checklist).

## Running the tests locally

```text
cmake -S protocol -B build/protocol && cmake --build build/protocol && ctest --test-dir build/protocol
cmake -S host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake -S firmware/link -B build/link && cmake --build build/link && ctest --test-dir build/link
cmake -S tests/integration -B build/integration && cmake --build build/integration && ctest --test-dir build/integration
tools/run-ui-simulator.sh
```

On Windows add `--config Debug` to the build and `--build-config Debug`
to `ctest`.

## The loopback integration test

[tests/integration](https://github.com/egungor/stm32-desk-dashboard/blob/main/tests/integration/README.md) runs the real STM32
protocol client and the real host controller against each other. USB, the
LCD and Windows are models, with time simulated in 1 ms steps:

- one 64-byte report per direction every 4 ms;
- the STM32 receive queue (40 frames) and its overflow;
- the STM32 UI loop (every 10 ms, 8 frames), which can be made "busy";
- the host opening the board some time after it is plugged in.

Scenarios: connect; host started late; select on the board; Windows default
change; devices added and removed; default removed; all removed; Windows
refuses; a select racing a removal; unplug and replug; host stops
answering; host restart; 40 devices with long UTF-8 names; volume frames;
board busy while long lists arrive.

It found two real bugs before any hardware test: a stale device index after
a host restart switched Windows to the wrong device, and a lost list frame
left the board on an old list for good (both fixed, #34).

## Truthfulness labels

Every PR and document uses: VERIFIED, NOT VERIFIED, REQUIRES HARDWARE,
REQUIRES WINDOWS, NOT TESTED. A build or a simulation never counts as a
hardware test.
