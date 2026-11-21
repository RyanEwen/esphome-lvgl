"""Photo frame service for the panels' screensaver.

GET /photo.jpg?w=480&h=800  ->  a photo, cropped to fill w x h (a baseline JPEG, which the
                               panels' decoder needs)
GET /health                 ->  "ok"

Photos come from one of two sources, set in the environment (see .env.example):

  PHOTO_DIR=/photos     a folder of JPEG, PNG or WebP images (the default, /photos)
  IMMICH_URL, IMMICH_API_KEY, IMMICH_ALBUM_ID
                        an Immich album (used when all three are set)

They're shown in a shuffled order: every photo once before any repeats. The source is
re-read every REFRESH seconds (default 600), so new photos join the rotation.
"""

import io
import json
import os
import random
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image, ImageOps

IMMICH_URL = os.environ.get("IMMICH_URL", "").rstrip("/")
API_KEY = os.environ.get("IMMICH_API_KEY", "")
ALBUM_ID = os.environ.get("IMMICH_ALBUM_ID", "")
USE_IMMICH = bool(IMMICH_URL and API_KEY and ALBUM_ID)
PHOTO_DIR = Path(os.environ.get("PHOTO_DIR", "/photos"))
REFRESH = int(os.environ.get("REFRESH", os.environ.get("ALBUM_REFRESH", "600")))
MAX_SIDE = 1280  # largest w/h a caller may ask for
IMAGE_TYPES = {".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp"}

_lock = threading.Lock()
_items: list[str] = []
_bag: list[str] = []
_loaded_at = 0.0


# --- Immich ---------------------------------------------------------------------------

def immich(path: str, body: dict | None = None) -> bytes:
    req = urllib.request.Request(
        IMMICH_URL + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"x-api-key": API_KEY, "Content-Type": "application/json", "Accept": "*/*"},
    )
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read()


def list_immich() -> list[str]:
    ids, page = [], 1
    while page:
        res = json.loads(immich("/api/search/metadata", {
            "albumIds": [ALBUM_ID], "type": "IMAGE", "size": 1000, "page": page,
        }))["assets"]
        ids += [a["id"] for a in res["items"]]
        page = int(res["nextPage"]) if res.get("nextPage") else 0
    return ids


def open_immich(asset: str) -> Image.Image:
    # "preview" is Immich's ~1440px JPEG
    return Image.open(io.BytesIO(immich(f"/api/assets/{asset}/thumbnail?size=preview")))


# --- a folder -------------------------------------------------------------------------

def list_dir() -> list[str]:
    return sorted(str(p) for p in PHOTO_DIR.rglob("*") if p.suffix.lower() in IMAGE_TYPES and p.is_file())


def open_file(path: str) -> Image.Image:
    return Image.open(path)


list_items, open_item = (list_immich, open_immich) if USE_IMMICH else (list_dir, open_file)


# --- shared ---------------------------------------------------------------------------

def next_item() -> str:
    global _items, _bag, _loaded_at
    with _lock:
        if not _items or time.time() - _loaded_at > REFRESH:
            _loaded_at = time.time()  # also after a failure, so a source that's down isn't retried per request
            try:
                _items = list_items()
                present = set(_items)
                _bag = [a for a in _bag if a in present]
            except Exception as e:  # keep serving the old list if the source is briefly away
                print("refresh failed:", e, flush=True)
            if not _items:
                raise RuntimeError("no photos found")
        if not _bag:
            _bag = _items[:]
            random.shuffle(_bag)
        return _bag.pop()


def render(item: str, w: int, h: int) -> bytes:
    img = ImageOps.exif_transpose(open_item(item)).convert("RGB")
    # fill the panel; crop a little above centre, where faces usually are
    img = ImageOps.fit(img, (w, h), Image.LANCZOS, centering=(0.5, 0.4))
    out = io.BytesIO()
    img.save(out, "JPEG", quality=85, progressive=False, optimize=True)
    return out.getvalue()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/health":
            return self.reply(200, b"ok", "text/plain")
        if url.path != "/photo.jpg":
            return self.reply(404, b"not found", "text/plain")
        q = urllib.parse.parse_qs(url.query)
        try:
            w = max(16, min(MAX_SIDE, int(q.get("w", ["480"])[0])))
            h = max(16, min(MAX_SIDE, int(q.get("h", ["800"])[0])))
        except ValueError:
            return self.reply(400, b"bad size", "text/plain")
        for _ in range(3):  # skip a photo that can't be read
            try:
                return self.reply(200, render(next_item(), w, h), "image/jpeg")
            except Exception as e:
                print("photo failed:", e, flush=True)
        self.reply(502, b"no photo", "text/plain")

    def reply(self, code: int, body: bytes, ctype: str):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        print(self.address_string(), fmt % args, flush=True)


if __name__ == "__main__":
    print("photos from", f"Immich album {ALBUM_ID}" if USE_IMMICH else PHOTO_DIR, flush=True)
    ThreadingHTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
