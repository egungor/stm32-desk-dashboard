#!/usr/bin/env python3
"""Screenshots of the Windows settings window's page (ADR-011).

Opens host/Platform/Windows/settings.html, the page dashboard-tray shows
in WebView2, in headless Chromium (Playwright) with a stand-in for
``window.chrome.webview`` that answers ``get`` with demo state in the
format of host/App/SettingsModel.h. Every page of the window is captured,
in the light and the dark Windows theme.

Not the real window: the title bar is missing, and Segoe UI (not on
Linux) is replaced by Inter. Layout, texts and controls are the page's own.

Usage: settings_window.py <screens directory>
Needs: pip install playwright && playwright install chromium; Pillow.
"""

import io
import json
import os
import sys

from PIL import Image
from playwright.sync_api import sync_playwright

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PAGE = os.path.join(REPO, "host", "Platform", "Windows", "settings.html")
PREVIEW = "https://pictures.desk-dashboard/"

FOOD_LISTS = """[istemiyorum]
Etli bamya
Kabak tatlısı

[sevmiyorum]
Tarhana çorba
Yayla çorba

[seviyorum]
Kağıt kebabı
Fıstıklı baklava
"""

STATE = {
    "type": "state",
    "connected": True,
    "settings": {"layout": 0, "theme": 0, "accent": 0, "grid": 0, "startup_tab": 0, "last_tab": 0,
                 "screen_timeout": 15, "flipped": 0, "language": 0, "volume_limit_on": 1,
                 "volume_limit": 40, "startup_screen": 0},
    "startup": True,
    "devices": [
        {"id": 0, "name": "Hoparlörler (Realtek(R) Audio)", "alias": "Masaüstü hoparlör", "favorite": True},
        {"id": 1, "name": "Kulaklık (USB Gaming Headset)", "alias": "Oyun kulaklığı", "favorite": True},
        {"id": 2, "name": "Monitör (NVIDIA High Definition Audio)", "alias": "Monitör", "favorite": False},
        {"id": 3, "name": "Kablosuz kulaklık (Bluetooth)", "alias": "", "favorite": True},
        {"id": 4, "name": "Dijital çıkış (S/PDIF)", "alias": "", "favorite": False},
        {"id": 5, "name": "USB DAC", "alias": "", "favorite": False},
    ],
    "weather": {"name": "İstanbul", "latitude": 41.0138, "longitude": 28.9497},
    "pictures": [
        {"id": "p1", "name": "Gün batımı", "width": 4032, "height": 2268, "shuffle": True,
         "preview": PREVIEW + "p1.png", "crop": {"x": 0.0, "y": 0.0, "w": 1.0, "h": 1.0}},
        {"id": "p2", "name": "Sabah (telefon, dikey)", "width": 2268, "height": 4032, "shuffle": True,
         "preview": PREVIEW + "p2.png", "crop": {"x": 0.0, "y": 0.35, "w": 1.0, "h": 0.319}},
        {"id": "p3", "name": "Gece", "width": 4000, "height": 2250, "shuffle": False,
         "preview": PREVIEW + "p3.png", "crop": {"x": 0.0, "y": 0.0, "w": 1.0, "h": 1.0}},
    ],
    "currentPicture": "p1",
    "shuffle": {"enabled": True, "minutes": 10, "random": True},
    "menuNextDayHour": 14,
    "importing": False,
    "claudeKeySet": True,
    "claudeModel": "claude-sonnet-5-5",
    "foodLists": FOOD_LISTS,
    "hostVersion": "0.9.0-beta.14",
    "hostProtocol": 7,
    "firmware": "0.9.0-beta.14",
    "boardProtocol": 7,
    "folder": "C:\\Users\\kullanici\\AppData\\Roaming\\stm32-desk-dashboard",
}

STUB = """
window.chrome = { webview: {
  _l: [],
  addEventListener(type, f) { this._l.push(f); },
  postMessage(text) {
    const m = JSON.parse(text);
    if (m.cmd === "get") setTimeout(() => this._l.forEach(f => f({ data: JSON.stringify(window.__STATE) })), 0);
  }
}};
"""

FONT = "html, body, select, input, button, textarea { font-family: 'Segoe UI', Inter, sans-serif !important; }"

PAGES = ["general", "display", "audio", "dashboard", "pictures", "claude", "about"]


def variants(sunset_png):
    """Three pictures for the list: the drawn sunset, a cool morning and a night version."""
    base = Image.open(sunset_png).convert("RGB")
    morning = Image.merge("RGB", base.split()[::-1]).point(lambda v: min(255, int(v * 1.1)))
    night = base.point(lambda v: int(v * 0.45))
    tall = morning.resize((272, 480))
    out = {}
    for name, img in (("p1", base), ("p2", tall), ("p3", night)):
        buf = io.BytesIO()
        img.save(buf, "PNG")
        out[name] = buf.getvalue()
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    pics = variants(os.path.join(out, "dash-picture.png"))
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for scheme in ("light", "dark"):
            ctx = browser.new_context(viewport={"width": 1000, "height": 700}, device_scale_factor=1,
                                      color_scheme=scheme)
            ctx.route(PREVIEW + "*", lambda r: r.fulfill(
                status=200, content_type="image/png", body=pics[r.request.url.rsplit("/", 1)[1][:2]]))
            page = ctx.new_page()
            page.add_init_script("window.__STATE = " + json.dumps(STATE) + ";" + STUB)
            page.goto("file://" + PAGE)
            page.add_style_tag(content=FONT)
            page.wait_for_timeout(300)
            for name in PAGES:
                page.click("nav button[data-page=%s]" % name)
                page.wait_for_timeout(200)
                page.screenshot(path=os.path.join(out, "win-%s-%s.png" % (name, scheme)))
            if scheme == "light":
                page.click("nav button[data-page=pictures]")
                page.click("#pictureGrid .pic:first-child button:nth-of-type(2)")  # Kırp
                page.wait_for_timeout(400)
                page.screenshot(path=os.path.join(out, "win-crop-light.png"))
            ctx.close()
        browser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
