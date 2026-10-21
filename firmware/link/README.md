# Protocol clients (`firmware/link`)

Two clients, hardware independent, share the USB link (ADR-006):
`link_client.c` (audio commands, below) and `dash_client.c` (dashboard,
at the end). `App/Src/link_port.c` decodes each received frame once and
gives it to the dashboard client when `Dash_Owns` says so, otherwise to
the audio client; the dashboard client numbers its frames from the audio
client's counter.

## Audio (`link_client.c`)

STM32 side of the audio commands of protocol v3 (`protocol/protocol.md`). Hardware independent
C: frames go out through a callback and come in through `Link_OnFrame`,
the UI is driven through callbacks, time is passed in. The firmware glue
is `App/Src/link_port.c` (USB HID in `App/Src/usb_link.c`).

What it does:

- after USB is configured, sends `GET_DEVICE_LIST` and repeats it every
  1 s until a complete list has arrived;
- assembles `DEVICE_LIST` frames by `generation` and `position` and shows
  the list only when complete; a new list that stays incomplete for 1 s
  after its last entry (a frame was lost) is requested again, and the
  older list stays on screen meanwhile;
- applies `DEVICE_CHANGED` (the default flag in the list until then);
- sends `SELECT_DEVICE` and marks the device as pending; without an answer
  in 2 s the host counts as not responding (notice, waiting screen,
  request the list again);
- shows a notice when the host answers the selection with `ERROR`, and
  re-requests the list after `UNKNOWN_DEVICE`;
- shows `VOLUME_CHANGED` / `MUTE_CHANGED`; after `DEVICE_CHANGED` "none"
  volume and mute are unknown (controls disabled) until reported again;
- `Link_SetVolume`: `SET_VOLUME` at most every 100 ms with the latest
  value, the last value always sent; host reports are not shown during a
  local change and for 300 ms after the last `SET_VOLUME`, then the
  host's value is shown (no knob jumping back while dragging);
- `Link_SetMute`: sends `SET_MUTE`; the mute state shown changes only
  with `MUTE_CHANGED`;
- answers invalid frames with `ERROR`, never an `ERROR` frame.

Tests (Linux, any C compiler). The client is also run against the real
host controller in `tests/integration`.

```text
cmake -S firmware/link -B build/link
cmake --build build/link
ctest --test-dir build/link --output-on-failure
```

## Dashboard (`dash_client.c`, #14)

- after USB is configured, sends `GET_CARDS`; the host answers with
  `TIME` and every card slot;
- collects `CARD_PART` / `DETAIL_PART` records (`ProtoAssembler`) and
  hands them to the UI only when complete; `CARD_CLEAR` empties a slot;
- a card record still incomplete 1 s after its last part: `GET_CARDS`
  again; a detail: `OPEN_DETAIL` again with the same argument;
- `Dash_OpenDetail` sends `OPEN_DETAIL`; detail parts for another slot
  than the open one are ignored; no complete detail and no `ERROR` within
  2 s, or an `ERROR` about it: `DASH_NOTICE_DETAIL_FAILED`;
- `Dash_Action` sends `ACTION` (detail buttons, media strip); an `ERROR`
  about it: `DASH_NOTICE_ACTION_FAILED`;
- `TIME` sets the clock; `Dash_LocalTime` counts on with the tick.

Tests: `tests/test_dash.c` (`ctest`, with `tests/test_link.c`).
