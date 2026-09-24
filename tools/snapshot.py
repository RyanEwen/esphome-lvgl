#!/usr/bin/env python3
"""Render every page of an SDL config to PNG, without hardware or Home Assistant.

    tools/snapshot.py sdl-example.yaml            # -> snapshots/<page>.png
    tools/snapshot.py sdl-example.yaml -o /tmp/s  # somewhere else
    tools/snapshot.py sdl-example.yaml --all      # skip: true pages too

It builds the config for the host with LVGL's snapshot support, shows each
page in turn, and saves the screen with the header and footer drawn on top,
at the canvas size the SDL config sets (sdl_width x sdl_height). Nothing
talks to Home Assistant, so tiles show their unknown state. Needs what the
SDL example needs (SDL2 installed); no Python packages.

Use it to check a layout change on a canvas you don't have a board for, or
to make README screenshots.
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

HEADER = r"""#pragma once
#include <cstdio>
#include <string>
#include "lvgl.h"
// written by tools/snapshot.py; dumps the screen and the top layer as raw pixels
namespace lvgl_snap {
inline void dump(lv_obj_t *obj, lv_color_format_t cf, int bpp, const std::string &path) {
  lv_draw_buf_t *b = lv_snapshot_take(obj, cf);
  if (b == nullptr) return;
  FILE *f = fopen(path.c_str(), "wb");
  uint32_t wh[2] = {b->header.w, b->header.h};
  fwrite(wh, sizeof wh, 1, f);
  for (uint32_t y = 0; y < b->header.h; y++)
    fwrite(b->data + y * b->header.stride, 1, b->header.w * bpp, f);
  fclose(f);
  lv_draw_buf_destroy(b);
}
inline void take(const char *page) {
  std::string base = std::string("@DIR@/") + page;
  dump(lv_screen_active(), LV_COLOR_FORMAT_RGB888, 3, base + ".scr");
  dump(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888, 4, base + ".top");
}
}  // namespace lvgl_snap
"""


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def compose(scr_path, top_path):
    """The screen with the top layer alpha-blended over it, as RGB bytes."""
    s = Path(scr_path).read_bytes()
    t = Path(top_path).read_bytes()
    w, h = struct.unpack("<II", s[:8])
    s, t = s[8:], t[8:]
    out = bytearray(w * h * 3)
    for i in range(w * h):
        sb, sg, sr = s[i * 3], s[i * 3 + 1], s[i * 3 + 2]          # LVGL stores BGR
        tb, tg, tr, ta = t[i * 4], t[i * 4 + 1], t[i * 4 + 2], t[i * 4 + 3]
        out[i * 3] = (tr * ta + sr * (255 - ta)) // 255
        out[i * 3 + 1] = (tg * ta + sg * (255 - ta)) // 255
        out[i * 3 + 2] = (tb * ta + sb * (255 - ta)) // 255
    return w, h, bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("config", help="a top-level config that uses devices/SDL.yaml")
    ap.add_argument("-o", "--out", default="snapshots", help="output directory (default: snapshots/)")
    ap.add_argument("--all", action="store_true", help="include skip: true pages")
    ap.add_argument("--settle", type=int, default=700, help="ms to wait after showing a page")
    args = ap.parse_args()

    r = subprocess.run(["esphome", "config", args.config], capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        sys.exit(f"esphome config {args.config} failed:\n{r.stdout[-2000:]}")
    resolved = r.stdout
    if "platform: sdl" not in resolved:
        sys.exit(f"{args.config} doesn't use the SDL display; snapshots need a host build")
    name = re.search(r"^esphome:\n(?:  .*\n)*?  name: (\S+)", resolved, re.M).group(1)
    # only the pages under lvgl: -> pages:, not msgboxes or other id lists.
    # `lvgl:` resolves to a list, so pages: sits at 4 spaces, its entries at 6.
    pg = re.search(r"^ {2,4}pages:\n((?:(?: {6,}.*)?\n)*)", resolved, re.M)
    block = pg.group(1) if pg else ""
    pages = []
    for m in re.finditer(r"^      - id: (\w+)\n((?:        .*\n)*)", block, re.M):
        if m.group(1) != "splash" and (args.all or "skip: true" not in m.group(2)):
            pages.append(m.group(1))
    if not pages:
        sys.exit(f"{args.config}: found no pages under lvgl: pages:")
    boot_screen = re.search(r"^\s+id: boot_screen$", resolved, re.M) is not None

    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="snapshot-"))
    header = ROOT / "_snapshot.h"
    wrapper = ROOT / "_snapshot.yaml"
    steps = ["        - delay: 3s"]
    if boot_screen:
        steps.append("        - lvgl.widget.hide: boot_screen  # no Home Assistant to dismiss it")
    for p in pages:
        steps += [f"        - lvgl.page.show: {p}", f"        - delay: {args.settle}ms",
                  f'        - lambda: lvgl_snap::take("{p}");']
    steps.append("        - lambda: exit(0);")
    try:
        header.write_text(HEADER.replace("@DIR@", str(work)))
        wrapper.write_text(
            "# written by tools/snapshot.py; deleted when it finishes\n"
            f"packages:\n  base: !include {args.config}\n"
            "esphome:\n  includes:\n    - _snapshot.h\n"
            "  platformio_options:\n    build_flags:\n      - -DLV_USE_SNAPSHOT=1\n"
            "  on_boot:\n    - priority: -200\n      then:\n" + "\n".join(steps) + "\n")
        b = subprocess.run(["esphome", "compile", wrapper.name], capture_output=True, text=True, cwd=ROOT)
        if b.returncode:
            sys.exit(f"build failed:\n{(b.stdout + b.stderr)[-3000:]}")
        program = ROOT / ".esphome/build" / name / ".pioenvs" / name / "program"
        try:
            subprocess.run([str(program)], cwd=ROOT, capture_output=True,
                           timeout=10 + len(pages) * (args.settle / 1000 + 1))
        except subprocess.TimeoutExpired:
            print("warning: the program didn't exit on its own; some pages may be missing")
        for p in pages:
            scr, top = work / f"{p}.scr", work / f"{p}.top"
            if not scr.exists():
                print(f"{p}: no snapshot")
                continue
            w, h, rgb = compose(scr, top)
            write_png(out / f"{p}.png", w, h, rgb)
            print(f"{p}: {out / (p + '.png')} ({w}x{h})")
    finally:
        header.unlink(missing_ok=True)
        wrapper.unlink(missing_ok=True)
        for f in work.glob("*"):
            f.unlink()
        work.rmdir()


if __name__ == "__main__":
    main()
