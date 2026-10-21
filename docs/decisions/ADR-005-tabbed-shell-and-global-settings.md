# ADR-005: Tabbed shell and global settings

## Status

Proposed (issue #5). Implemented in Dashboard v1 step 4 (`docs/roadmap.md`).

## Context

The inherited UI (`firmware/ui`, ADR-004) is one application: every
screen is its own LVGL screen (`lv_obj_create(NULL)`) — waiting, main
(audio devices), settings, edit mode, keyboard — and the main screen
carries its own top bar (All / Favorites tabs, edit, settings) or, in
layout B, a rail on the left. Layout A and the settings page are
verified on the board; B and C in the simulator.

The dashboard adds a second application of the same weight. The user
asked for tabs on the LCD with Dashboard as the main tab and the audio
controller as another tab (2026-10-21); more tabs come later.

Limits that shape the decision:

- the LCD is 480×272; height is the scarcer side;
- the LVGL heap is 128 KB; the audio UI peaks at 53 KB in the simulator
  (`firmware/ui/README.md`);
- the audio screens have sliders and lists, which fight with swipe
  gestures.

## Decision

### Shell

One new module, `firmware/ui/ui_shell.c`, owns the application screen:

```text
┌────┬──────────────────────────────────────────┐
│ ⌂  │                                          │
│ ♪  │   content area 424 × 272                 │
│    │   (the shown tab)                        │
│ ⚙  │                                          │
│ ⇄  │                                          │
└────┴──────────────────────────────────────────┘
 rail 56 px: tabs top-down, settings and the connection mark at the foot
```

- **Rail, 56 px, on the left**, always visible on the application
  screen. v1 tabs: Dashboard, Audio. Settings sits at the foot of the
  rail, with the connection mark (host connected or not) below it.
- **Tab content** is a container of 424×272 in the content area. Every
  tab is built once and hidden with `LV_OBJ_FLAG_HIDDEN` when another tab
  is shown, so its state (page, scroll position) survives a tab switch.
  If the measured peak heap use passes 96 KB (75 % of 128 KB), the tab
  that is not shown is deleted and rebuilt on show instead; that is a
  change inside `ui_shell.c` only.
- **Full-screen screens stay full screen** and hide the rail: waiting
  screen (no host), settings, the audio edit mode and its keyboard. They
  are modal; a back button returns to the application screen.
- **The approval prompt** of ADR-006/ADR-007 is drawn on `lv_layer_top()`
  so it appears over any screen, and wakes a dark screen.

### Audio tab

- The audio main screen becomes the Audio tab's content: built inside
  the 424 px container instead of on its own screen. The `Ui_*` calls
  that `link_client` uses (`Ui_SetDevices`, `Ui_SetActive`,
  `Ui_SetVolume`, …) do not change.
- Its top bar keeps All / Favorites and the edit button; the settings
  button moves to the rail; the flip button leaves the main view (it is
  already on the settings page as "Ekranı döndür").
- Layout B's own rail (tabs, favorites, edit, settings) folds into the
  shell: its list keeps the vertical volume slider; tabs and edit move
  into a top bar like layout A's.
- Layout A is ported first. If B or C need more than a reflow to fit
  424 px, they get their own issue; until then the layout setting offers
  only what has been ported.

### Global settings

- Theme, accent colour, language, screen timeout and flip apply to the
  whole application (they already are global in effect).
- The settings page gets two sections: **Genel** (theme, accent,
  language, screen timeout, flip, startup screen) and **Ses** (layout,
  grid, startup tab, volume limit on switch).
- The settings record (ADR-004) gains one byte: `startup_screen`
  (0 Dashboard, 1 Audio; default 0). This is part of protocol version 3
  (ADR-006). `startup_tab` keeps its meaning inside the Audio tab.
- Screen timeout and notices work as today, for every tab.

## Alternatives

- **`lv_tabview` with a top tab bar.** Takes 30–40 px of the 272 px
  height and brings swipe-to-switch, which clashes with the volume slider
  and the device lists.
- **Bottom navigation bar.** Same height cost; the audio tab already has
  its bottom strip (mute, volume, pages).
- **Separate screens with a home button.** No new layout work for the
  audio tab, but every switch costs two taps and there is no place that
  shows where the user is.
- **Rebuild each tab on show from the start.** Saves heap but loses page
  and scroll state and costs a rebuild per switch; kept as the fallback
  above, decided by measurement.

## Consequences

- The audio screens are reflowed from 480 to 424 px; all UI screenshots
  in `docs/images/` and the simulator CI are regenerated.
- `ui.c` loses its screen switching for the main screen to `ui_shell.c`;
  the waiting, settings, edit and keyboard screens keep their own.
- The settings record grows to 12 bytes, which needs protocol version 3
  on both sides at once (ADR-006).
- The audio checks of `docs/hardware.md` must be repeated on the board
  for the Audio tab (REQUIRES HARDWARE).
