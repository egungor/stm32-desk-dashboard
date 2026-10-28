#!/usr/bin/env bash
# Screenshots of every LCD screen for the README and the wiki.
#
# Builds the UI simulator's ui_doc_shots (firmware/simulator/doc_shots.c:
# the firmware's own UI code with demo data), draws the demo picture and
# cover, runs it, converts the framebuffer dumps to PNG with
# tools/fb2png.py and makes the composite images (contact.py).
#
# Usage: tools/doc-screens/make.sh [output directory]   (default: docs/images/screens)
# Needs: CMake, a C compiler, Python 3 with numpy and Pillow, the LVGL
#        submodule (git submodule update --init).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/ui-sim-build"
OUT_DIR="$(mkdir -p "${1:-$REPO_ROOT/docs/images/screens}" && cd "${1:-$REPO_ROOT/docs/images/screens}" && pwd)"
WORK_DIR="$REPO_ROOT/build/doc-screens"

cmake -S "$REPO_ROOT/firmware/simulator" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD_DIR" --parallel --target ui_doc_shots > /dev/null

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/dumps"
python3 "$REPO_ROOT/tools/doc-screens/assets.py" "$WORK_DIR"
"$BUILD_DIR/ui_doc_shots" "$WORK_DIR/dumps" "$WORK_DIR" > "$WORK_DIR/run.log"

rm -f "$OUT_DIR"/*.png
for dump in "$WORK_DIR"/dumps/*.bin; do
    name="$(basename "${dump%.bin}")"
    python3 "$REPO_ROOT/tools/fb2png.py" "$dump" "$OUT_DIR/$name.png" > /dev/null
done
python3 "$REPO_ROOT/tools/doc-screens/contact.py" "$OUT_DIR"

# The Windows settings window's page (optional: needs Playwright's Chromium).
if python3 -c "import playwright" 2> /dev/null; then
    python3 "$REPO_ROOT/tools/doc-screens/settings_window.py" "$OUT_DIR"
else
    echo "Playwright not installed: settings window screenshots skipped"
fi
echo "Screenshots in $OUT_DIR"
