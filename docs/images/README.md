# Images

Board photos, LCD screenshots and simulator screenshots used by the
documentation and the project presentation live here.

## Screens of the README and the wiki (`screens/`)

Every screen of the board and of the settings window, made on
2026-10-28 from `main` + #88 with `tools/doc-screens/make.sh`:

- **Board screens** (`dash-*`, `music-*`, `audio-*`, `settings-*`,
  `en-*`, `accent-*`, `waiting`): `firmware/simulator/doc_shots.c` runs
  `firmware/ui` headless with one set of demo data (a Wednesday morning in
  Istanbul; made-up names, track, menu and notifications) and writes the
  RGB565 framebuffer, converted with `tools/fb2png.py`. The same method
  gave the LCD screenshots below pixel for pixel; colours on the panel
  are REQUIRES HARDWARE. The picture and the cover are drawn by
  `tools/doc-screens/assets.py`, not photos.
- **Settings window** (`win-*-light`, `win-*-dark`, `win-crop-light`):
  `host/Platform/Windows/settings.html` in headless Chromium with demo
  state (`tools/doc-screens/settings_window.py`); no title bar, Inter
  instead of Segoe UI.
- **Composites**: `hero.png` (Music, Dashboard, Audio in a bezel, the
  README's header) and `accents.png` (the six accent colours), made by
  `tools/doc-screens/contact.py`.

The wiki's [Screen Gallery](https://github.com/egungor/stm32-desk-dashboard/wiki/Screen-Gallery)
shows and describes each file. Make them again after a UI change and
commit them with it; the file names are referenced by the README and the
wiki pages, so keep them.

## LCD screenshots of the UI

Exact captures of the LCD: the framebuffer dumped over SWD and converted
with `tools/fb2png.py` (see `docs/toolchain.md`). Prefer these over phone
photos.

Taken 2026-10-16 on firmware v0.7.0-beta.2 (`e46f5ee`, Release; the board's
flash was read back and is identical to the release `.bin`), host
v0.7.0-beta.2, with the developer's own Windows outputs and aliases. Dark
theme, blue accent, volume 40 %. The capture reads the framebuffer the LTDC
shows (`LTDC_L1CFBAR`, `0x400168AC`); a dump taken while the screen changes
mixes two frames, so only dumps of a still screen are kept.

| File | Screen |
|---|---|
| `ui-main-screen.png` | Layout A (cards, 2×2), "Tümü" tab, page 1 of 3, active device |
| `ui-favorites.png` | Layout A, "Favoriler" tab |
| `ui-muted.png` | Layout A, "Favoriler" tab, muted (mute set with `stm32-audio-host mute on`) |
| `ui-layout-list.png` | Layout B (list), side rail, vertical volume slider |
| `ui-layout-tiles.png` | Layout C (large tile for the active device, tiles for the others) |
| `ui-settings.png` | Settings page, top |
| `ui-waiting-for-host.png` | No host program connected (tray program closed); identical to the M6 capture of `41bfed5` |

- Use descriptive lowercase names with the `ui-` prefix, e.g.
  `ui-volume.png`.
- Record the firmware commit the screenshot was taken from in the commit
  message and in the table above.

## Simulator screenshots

Output of the UI simulator (`firmware/simulator`, `tools/run-ui-simulator.sh`),
which runs the same `firmware/ui` code and writes the same RGB565
framebuffer format as the board. They show layout, not the panel: colours
and speed on the real LCD are REQUIRES HARDWARE. Prefix `sim-`.

The simulator screenshots of the development branches (`sim-m7-*`) were
removed on 2026-10-16: the LCD screenshots above replace them. The CI
artifact `ui-screenshots` of the *UI Simulator and Link CI* job has the
current simulator output. The light theme with blue accent and the 3×2
grid were checked on the board on 2026-10-15 (`docs/hardware.md`, #60).

### Presentation screens

The screens in the product presentation. `firmware/ui` at `main` `c1342cf`
(#73), driven through `ui.h` by a small headless program like the simulator
(same LVGL build, same RGB565 framebuffer), with demo outputs: aliases
"Masaüstü hoparlör", "Oyun kulaklığı" (active), "Monitör", "Kablosuz
kulaklık", three outputs without alias, volume 62 %. Not captured on the
board: REQUIRES HARDWARE.

| File | Screen |
|---|---|
| `sim-pitch-a-dark.png` | Layout A, 2×2 cards, dark theme, orange accent |
| `sim-pitch-a-muted.png` | As above, muted |
| `sim-pitch-a-light-3x2.png` | Layout A, 3×2 cards, light theme, blue accent |
| `sim-pitch-a-english.png` | Layout A in English |
| `sim-pitch-acc-dark-0.png` … `-5.png` | The six accent colours, dark theme |
| `sim-pitch-b-dark.png` | Layout B (list), dark theme |
| `sim-pitch-c-dark.png` | Layout C (large tile), dark theme, purple accent |
| `sim-pitch-edit.png` | Edit mode |
| `sim-pitch-keyboard.png` | Alias keyboard |
| `sim-pitch-settings.png` | Settings page |
| `sim-pitch-waiting.png` | Waiting for the computer |

How close the simulator is to the board: the same method, run on the UI
code of `41bfed5`, gives the three LCD screenshots above pixel for pixel
(0 of 130,560 pixels differ; checked 2026-10-16). That compares the
framebuffer, not the panel.

## Project presentation

The presentation (Turkish, 12 slides) is a claude.ai artifact owned by the
project developer:
<https://claude.ai/artifact/1iZGqWiF55fRbjfhNWXViQ>

Since 2026-10-16 (#67) it is a product presentation: what the controller
does for the user (problem, solution, setup, main screen, three layouts,
colours, aliases and order, volume, details, specifications, status), not
how it is built. Every screen in it is one of the "Presentation screens"
above, labelled on the slides as the device software's own output, not a
photo. The closing slide separates what was verified on the board and on
Windows (M0–M6) from what is still being tested (M7, installer). The owner
can export it as PDF or PowerPoint from the artifact.

A PDF copy for reading without the artifact is in the repo:
[`docs/presentation/stm32-ses-kumandasi.pdf`](../presentation/stm32-ses-kumandasi.pdf)
(12 pages, 1920×1080). It was rendered from the artifact's slide files with
headless Chromium, so small details (line breaks, icon shapes) can differ
from the artifact's own export. The artifact is the source: when it changes,
render or export the PDF again and replace this copy.

When a screen changes, replace the file here first, then the image in the
presentation.
