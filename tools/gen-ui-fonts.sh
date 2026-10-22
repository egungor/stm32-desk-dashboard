#!/usr/bin/env bash
# Regenerate the LVGL fonts used by firmware/ui (Turkish letters + icons).
#
# Sources are the fonts shipped with LVGL (scripts/built_in_font/):
#   Montserrat-Medium.ttf                  SIL Open Font License 1.1
#   FontAwesome5-Solid+Brands+Regular.woff Font Awesome Free (SIL OFL 1.1)
# Requires Node.js; lv_font_conv is fetched by npx at the pinned version.
#
# Usage: tools/gen-ui-fonts.sh
set -euo pipefail

# Paths are relative to the repository root so the "Opts:" line that
# lv_font_conv writes into each file does not depend on the checkout path.
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="firmware/STM32AudioController/Middlewares/Third_Party/lvgl/scripts/built_in_font"
OUT="firmware/ui/fonts"
CONV=(npx --yes lv_font_conv@1.5.3)

# Basic Latin, Latin-1 Supplement, Latin Extended-A (covers Turkish).
TEXT_RANGE="0x20-0x7E,0xA0-0x17F"
# check, warning, chevron-left, chevron-right, plug, hourglass, volume-up,
# volume-mute, sync-alt (display flip); M7 (#60): star, cog, pencil-alt,
# list, th-large, arrow-up, arrow-down, backspace, times, and the device
# icons for the smaller cards and list rows: music, headphones, headset,
# desktop, usb; weather (#18, protocol.md "Icon" 1-10): sun, moon,
# cloud-sun, cloud-moon, cloud, smog, cloud-rain, cloud-showers-heavy,
# snowflake, bolt; menu (#20, "Icon" 11): utensils; media (#32): play, pause,
# step-backward, step-forward; notifications (#34, "Icon" 12): bell; Claude
# Code status icon (#47, "Icon" 13): robot
TEXT_SYMBOLS="0xF00C,0xF071,0xF053,0xF054,0xF1E6,0xF254,0xF028,0xF6A9,0xF2F1,0xF005,0xF013,0xF303,0xF03A,0xF009,0xF062,0xF063,0xF55A,0xF00D,0xF001,0xF025,0xF590,0xF108,0xF287,0xF185,0xF186,0xF6C4,0xF6C3,0xF0C2,0xF75F,0xF73D,0xF740,0xF2DC,0xF0E7,0xF2E7,0xF04B,0xF04C,0xF048,0xF051,0xF0F3,0xF544"
# music (generic), volume-up (speakers), headphones, headset, desktop
# (display), usb (digital output), plug, hourglass, warning; media (#32): play,
# pause (Music tab); the Dashboard's card icons (#47, ADR-010): the weather
# icons ("Icon" 1-10: sun, moon, cloud-sun, cloud-moon, cloud, smog,
# cloud-rain, cloud-showers-heavy, snowflake, bolt), utensils, image
ICON_SYMBOLS="0xF001,0xF028,0xF025,0xF590,0xF108,0xF287,0xF1E6,0xF254,0xF071,0xF04B,0xF04C,0xF185,0xF186,0xF6C4,0xF6C3,0xF0C2,0xF75F,0xF73D,0xF740,0xF2DC,0xF0E7,0xF2E7,0xF03E"

text_font() { # <size>
    "${CONV[@]}" --bpp 4 --size "$1" --no-compress --format lvgl --lv-include lvgl.h \
        --font "$SRC/Montserrat-Medium.ttf" -r "$TEXT_RANGE" \
        --font "$SRC/FontAwesome5-Solid+Brands+Regular.woff" -r "$TEXT_SYMBOLS" \
        -o "$OUT/ui_font_$1.c"
}

text_font 14
text_font 16
text_font 22
"${CONV[@]}" --bpp 4 --size 40 --no-compress --format lvgl --lv-include lvgl.h \
    --font "$SRC/FontAwesome5-Solid+Brands+Regular.woff" -r "$ICON_SYMBOLS" \
    -o "$OUT/ui_icons_40.c"
