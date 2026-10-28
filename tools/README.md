# Tools

| Tool | Purpose |
|---|---|
| `build-firmware-linux.sh` | Build the firmware on Linux with toolchains from `.toolchain/` (used by CI and Claude's sandbox; the developer build is in `docs/toolchain.md`) |
| `run-ui-simulator.sh` | Build the headless UI simulator and write its screens as PNG |
| `doc-screens/make.sh` | Screenshots of every screen with demo data for the README and the wiki (`docs/images/screens/`): the simulator's `ui_doc_shots`, the drawn demo picture and cover (`assets.py`), the composites (`contact.py`) and the settings window in headless Chromium (`settings_window.py`, needs Playwright) |
| `fb2png.py` | Convert a raw RGB565 framebuffer dump (from the board or the simulator) to PNG |
| `gen-ui-fonts.sh` | Regenerate the LVGL fonts in `firmware/ui/fonts` |
| `read-vcp.ps1` | Read the firmware log from the ST-LINK virtual COM port on Windows |
