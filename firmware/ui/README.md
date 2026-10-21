# Touch UI (`firmware/ui`)

Hardware-independent LVGL user interface. It is compiled into the
firmware (`App/Src/ui_port.c`, `App/Src/ui_demo.c`) and into the Linux simulator
(`firmware/simulator`).

## Application screen (ADR-005, #10)

```text
┌────┬──────────────────────────────────────────┐
│ ▦  │                                          │
│ ♪  │   the shown tab, 424 x 272               │
│    │                                          │
│ ⚙  │                                          │
│ ⇄  │                                          │
└────┴──────────────────────────────────────────┘
```

- `ui_shell.c` builds the main screen: a 56 px rail on the left with the
  Dashboard (`th-large`) and Audio (speaker) tab buttons, the settings
  button and the connection mark at its foot, and one 424 × 272 container
  per tab. Both tabs are built with the other screens and the hidden one
  keeps its state (page, sub-tab). `Ui_CurrentAppTab` reports the shown
  tab.
- The first settings after `Ui_Init` choose the tab shown at startup
  (`startup_screen`: 0 Dashboard, 1 Audio); before them the Dashboard tab
  is shown. A rebuild keeps the shown tab.
- Dashboard tab (`ui_dash.c`, ADR-006, #14): a clock strip (`Ui_SetTime`,
  counted on with the LVGL tick, updated once a minute), four cards on a
  2×2 grid (`Ui_SetCard`, slots 0–3; the host's record text: icon, title,
  badge, up to three lines; an empty slot is a dim frame that cannot be
  tapped) and the media strip `<< || >>` (`UiDashHandlers.media`). A tap
  on a card shows the detail view in the tab ("Yükleniyor..." with the
  card's title until `Ui_ShowDetail`): a header with back, title,
  subtitle and ‹ › when the record's flags hold `n`; rows (label and
  value), section titles; up to three buttons at the bottom
  (`UiDashHandlers.action`). `Ui_DetailFailed` returns to the cards with
  a notice. The view is built when shown and deleted when closed; a
  rebuild keeps it open. The texts are kept in `g_ui` (4 cards + one
  detail, about 3 KB). Icons: the record's icon numbers draw nothing yet
  (protocol.md has only 0).
- Audio tab: everything below; the settings button moved to the rail and
  the flip button to the settings page (the waiting screen keeps its
  button). Waiting screen, settings, edit mode and keyboard stay full
  screen and hide the rail.
- The rail uses `th-large` for the Dashboard: a home glyph needs the fonts
  regenerated (`tools/gen-ui-fonts.sh`), which was not possible in the
  sandbox of #10.

## Boundary

- Depends only on LVGL (`lvgl.h`). No HAL, CMSIS, FreeRTOS, USB,
  protocol framing or Windows concepts.
- Input: the state reported by the host, through `Ui_SetDevices`,
  `Ui_SetActive`, `Ui_SetVolume`, `Ui_SetMuted`, `Ui_ShowWaiting`,
  `Ui_ShowError`.
- Output: the `UiSelectCallback` when the user taps a device. The caller
  sends `SELECT_DEVICE` and calls `Ui_SetPending`; the button turns
  active only when the host confirms with `DEVICE_CHANGED`
  (`protocol/protocol.md`, "Exchanges").
- Volume and mute (bottom strip; handlers set with `Ui_SetAudioHandlers`):
  `UiVolumeCallback` while the slider is dragged and once on release;
  `UiMuteCallback` with the state wanted. The UI does not change the mute
  icon itself; it shows what `Ui_SetMuted` reports. Moving the slider
  while muted calls `UiMuteCallback` with "unmute" once (again only after
  the next `Ui_SetMuted`), before the volume. The gap between the slider
  and the mute button (`VOLUME_GAP`) is wider than the slider's touch
  margin (`VOLUME_TOUCH_PAD`), so a tap on the button's edge never sets
  the volume. `UI_VOLUME_UNKNOWN` disables the controls. Rate limiting is
  the caller's job (`firmware/link`).
- Display flip: a button in the top right corner of the waiting screen
  and a switch on the settings page; the button, held
  for `UI_FLIP_HOLD_MS` (icon turns orange), it calls the
  `UiFlipCallback` (`Ui_SetFlipHandler`) when the finger lifts. The UI
  does not rotate anything: the display port turns the image and mirrors
  touch (`App/Inc/ui_port.h`).
- `UiDevice` mirrors the protocol's device entry (id, icon, UTF-8 name of
  up to 48 bytes, alias of up to 32 bytes shown instead of the name,
  favorite flag); `UiIcon` values equal `ProtoIcon`.
- Settings (`UiSettings`, same fields and values as the protocol's
  settings record, ADR-004): `Ui_ApplySettings` applies theme (dark/light),
  accent colour (6), language (Turkish/English) and the card grid (2x2,
  3x2, 3x3, list); a change of one of them rebuilds the screens. The first
  settings after `Ui_Init` choose the startup tab. The UI reports changes
  it makes itself (the tab, as `last_tab`, and everything on the settings
  page) with the whole record through the `UiSettingsCallback`
  (`Ui_SetSettingsHandler`). `flipped` and `screen_timeout` are for the
  display port, the volume limit for the host.
- Settings page (`ui_settings.c`, #60, design A of #57; sections since
  #10): the cog button on the rail opens it; the left half of its bar
  goes back. A scrolling list in two sections: GENEL — theme, accent
  colour (6 swatches), language, screen timeout (never / 1 / 5 / 15 min),
  flip (switch), startup screen (Dashboard / Ses); SES — layout, grid,
  startup tab, volume limit on switch (switch; when on, a slider in 5 %
  steps, reported on release). A tap applies the change at once and reports
  the record; a change of theme, accent, grid or language rebuilds the
  screens on the next timer run (`ui_request_rebuild`, so the tapped
  control is not deleted inside its own event) and the page stays open at
  the same scroll position. Segmented controls react to `CLICKED`, which
  LVGL does not send after a drag that scrolled, so scrolling over a
  control changes nothing. A new device list or settings from the host
  leave the page open; the waiting screen closes it. "Layout" chooses A, B
  or C; the grid row is greyed out (and ignores taps) outside layout A.
- Layouts of the Audio tab (`UiSettings.layout`, #60, designs of #57;
  reflowed to 424 px in #10): A cards (`ui.c`, sub-tabs and the edit
  button in the top bar, volume and pages in the bottom strip, grid
  2x2 / 3x2 / 3x3 / list); B list (`ui_layouts.c`: A's top bar over the
  rows, four rows with the Windows name under an alias and a check mark on
  the active one, vertical volume on the right; its own rail of #60 gave
  way to the application rail); C large tile (`ui_layouts.c`: the active
  device, its volume and mute in a panel on the left, the other devices as
  six tiles, tabs as pills — favorites as a star only — and the edit
  button). All three fill the same objects (device buttons, tabs, pager,
  slider, mute) so `ui.c` renders them; they differ in `CardKind`. The
  mockups scroll B and show page dots in C; both use the page buttons of A
  here.
- Edit mode (`ui_edit.c`, #60, design A of #57): the pencil button in
  the Audio tab's top bar (shown once `Ui_SetEditHandlers` is called) opens every
  device, in the user's order, as cards of four per page. Each card has
  four buttons: move one place left, move one place right, favorite (in
  the accent colour when set) and alias. They call `UiEditHandlers`; the
  cards change when the host's new list arrives, as the protocol says. A
  handler that returns false (edit not sent) leaves the tap without
  effect. The alias keyboard is an `lv_keyboard` with a Turkish Q layout
  (ç ğ ı İ ö ş ü; "ABC" / "abc" / "1#" switch modes) and a one-line text
  area that refuses input past 32 bytes of UTF-8; ✓ or "Kaydet" saves
  ("" removes the alias), × closes without saving. A card is three LVGL
  objects (label, `lv_buttonmatrix`, box) with shared styles: about
  2.4 KB, so the edit mode fits next to the main screen. Both screens
  exist only while shown; a rebuild keeps them open and keeps the typed
  text.
- Screen timeout: a 1 s LVGL timer compares LVGL's time since the last
  touch (`lv_display_get_inactive_time`) with `screen_timeout` minutes.
  When it runs out a black, clickable object on the system layer covers
  everything and the `UiBacklightCallback` (`Ui_SetBacklightHandler`) is
  told to switch the backlight off. The next press lands on that object,
  so it only wakes the screen (backlight on) and never reaches a control.
  A new timeout from the host counts from then; "never" wakes a dark
  screen.
- Tabs: "All" and "Favorites" (devices with the favorite flag), in the
  host's order.
- Memory: the device list is a fixed array of 32 entries; LVGL objects are
  created in `Ui_Init` and reused when the list or page changes. A rebuild
  (layout, theme, accent, language, grid) deletes the old screens before
  building the new ones, so the LVGL heap never holds two sets. The
  settings page (about 15 KB), the edit mode (about 10 KB) and the alias
  keyboard (about 6 KB) exist only while shown. The heap is 128 KB
  (`LV_MEM_SIZE`, raised from 64 KB in #60): with the densest main screen,
  the settings page and LVGL's draw buffers, 64 KB ran out in the
  simulator. Simulator (`Ui_MemoryUse`, #14, both tabs built with four
  cards): 44 KB in use on the main screen, 52 KB with a detail open; peak
  63 KB (48 %), below the 96 KB at which ADR-005 would build only the
  shown tab. No growth over opening and closing details. No growth over repeated
  rebuilds, layout changes, or opening and closing these screens. Avoid
  `opa` below 100 % on an object: LVGL then draws it through a layer
  buffer from this heap (up to 24 KB); use `bg_opa` / `text_opa`.
- Text: `ui_text.c`/`ui_text.h` are generated by `tools/gen-ui-text.py`
  (Turkish and English, UTF-8 as escapes); colours are in `ui_theme.c`.

## Files

| File | Content |
|---|---|
| `ui.h`, `ui.c` | Screens and API; Audio tab layout A |
| `ui_shell.c` | Main screen: rail, Dashboard and Audio tabs (ADR-005) |
| `ui_dash.c` | Dashboard tab: clock, cards, detail view, media strip (ADR-006) |
| `ui_settings.c` | Settings page |
| `ui_edit.c` | Edit mode (order, favorites) and alias keyboard |
| `ui_layouts.c` | Audio tab of layouts B (list) and C (large tile) |
| `ui_internal.h` | State and helpers shared by the UI files |
| `ui_theme.c` | Dark and light palettes, accent colours |
| `ui_text.c`, `ui_text.h` | UI text, Turkish and English (generated) |
| `lv_conf.h` | LVGL v9.5.0 configuration, shared by firmware and simulator |
| `fonts/` | Generated fonts: Montserrat 14/16/22 px with Turkish letters and Font Awesome icons, 40 px device icons (`tools/gen-ui-fonts.sh`) |

Fonts are generated by `tools/gen-ui-fonts.sh` (`lv_font_conv` 1.5.3,
fonts shipped with LVGL: Montserrat and Font Awesome Free, both SIL Open
Font License 1.1). Edit the script, not the generated `.c` files.

UI text (`TXT_*`) is generated into `ui_text.c` by `tools/gen-ui-text.py`,
in Turkish and English.

## LVGL

LVGL is a git submodule at
`firmware/STM32AudioController/Middlewares/Third_Party/lvgl`, pinned to
tag v9.5.0 (MIT licence). After cloning:

```text
git submodule update --init
```
