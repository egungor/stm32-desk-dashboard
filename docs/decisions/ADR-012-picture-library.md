# ADR-012: Picture library, shuffle and crop for the picture card

## Status

Accepted (issue #72). The owner's request of 2026-10-22: a Pictures page in
the settings window with the photos the user added, kept in the settings
folder; a shuffle with its settings (which photos, how often); the next
photo is sent to the board and replaces the shown one only when it has
fully arrived, the board keeps one; when adding a photo and later, the
user chooses the part shown on the board, with zoom; the folder keeps
the photos unchanged and the window shows the chosen part.

## Context

Until now the picture card showed one photo, chosen with a file dialog;
its path was kept in `picture.txt` and the host always showed the centred
480:272 part (ADR-010, protocol v7 480 × 272). The board already collects
a new picture in a second buffer and shows it only when complete
(`dash_client.c`), so a picture change needs no protocol change.

## Decision

- **Library** (`host/Picture/PictureLibrary`, platform independent and
  unit tested): `%APPDATA%\stm32-desk-dashboard\pictures.json` lists the
  photos (`id`, the copy's file name, the original name, the upright
  size, `crop`, `shuffle`) and the shuffle's settings (on/off, 1 min to
  24 h, random or as listed). Ids (`p1`, `p2`, …) are never used twice
  (`nextId`), so the window's cached previews never show an old photo.
- **Files**: an added photo is copied unchanged into `pictures\<id>.<ext>`;
  WIC writes an upright JPEG preview of at most 1600 px into
  `pictures\previews\<id>.jpg`, because the window cannot show HEIC and a
  12 MP photo is too large for it. The window gets the folder as
  `https://pictures.desk-dashboard/` (WebView2
  `SetVirtualHostNameToFolderMapping`; CSP `img-src` that host only).
- **Crop**: a rectangle in fractions of the upright photo, 480:272 in
  pixels (±2 %); the window's cropper (drag, wheel, slider, ±) works on
  the preview, the board's picture is cut from the original with WIC and
  scaled to 480 × 272. "Ortala" restores the centred part. The thumbnails
  show exactly the cropped part.
- **Ownership**: only the settings window writes `pictures.json` (then
  `SettingsChanged`); only the service writes which picture is shown
  (`picture-current.txt`: an id, `none`, or missing = the first). The
  window's "Göster" goes to the service as a request (`pictureShow`), like
  the preferences (ADR-011), so no file has two writers.
- **Shuffle**: the service (`WinPicture::onTick`) shows the next picture
  when the interval has passed since the last change, among the pictures
  marked for it (random: never the same twice in a row). The new picture
  goes out as `PICTURE` + parts (about 20 s); the board shows it when
  complete. "Resmi kaldır" shows none and turns the shuffle off.
- **Migration**: when there is no `pictures.json`, the tray adds the
  photo of `picture.txt` (before #72) as the first picture.
- **Console**: `dashboard-host picture` lists the library and checks the
  shown picture; `picture add <file>` adds a photo.

## Alternatives

- **Keep the board's pictures on the board** (several in SDRAM, shuffle
  there): more SDRAM and a protocol change; the owner asked for one on
  the board.
- **Store cropped copies**: the crop could not be changed later; the
  owner asked for the originals.
- **Show the originals in the window**: HEIC does not load, large photos
  are slow.

## Consequences

- New: `PictureLibrary`, `pictures.json`, `picture-current.txt`, the
  pictures folder; `WinPicture` reads the library, keeps the shown
  picture and runs the shuffle; `loadPicture` takes a crop.
- Settings window: a Pictures page (add, show, crop, delete, shuffle
  settings); the old single-picture card on the Dashboard page is gone.
- No protocol and no firmware change.
- REQUIRES WINDOWS + HARDWARE: adding (also HEIC), cropping, the shuffle
  on the board, the migration of an existing `picture.txt`.
