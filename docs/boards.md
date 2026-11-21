# Boards

## Supported

| Board | Screen | Board file | Example |
| --- | --- | --- | --- |
| Sunton `ESP32-8048S050`, 5" | 800x480, run portrait (480x800) | `boards/sunton-50.yaml` | `sunton-50-example.yaml` |
| Sunton `ESP32-8048S043`, 4.3" | 800x480, run portrait (480x800) | `boards/sunton-43.yaml` | `sunton-43-example.yaml` |
| Guition `JC3248W535`, 3.5" | 320x480 portrait | `boards/guition-35.yaml` | `guition-35-example.yaml` |
| Guition `JC4880P443C`, 4.3", ESP32-P4, mic + speaker | 480x800 portrait | `boards/guition-43.yaml` | `guition-43-example.yaml` |
| Desktop preview (SDL) | 480x800 / 320x480 | `boards/sdl-480x800.yaml`, `sdl-320x480.yaml` | `sdl-example.yaml`, `sdl-320-example.yaml` |

The Suntons' panels are landscape; LVGL rotates its portrait canvas onto them in software,
which costs some redraw speed (a full redraw is ~200 ms on the 5"). The Guitions are portrait
natively. The 4.3" Guition decodes camera video in hardware and has the speed for voice.

**Deprecated** (too slow, or too little memory, for the panel layout): the Sunton 2.8"
`ESP32-2432S028R` and its ILI9342 revision, and the Elecrow CrowPanel `DIS05035H`. They keep
the classic layouts (`layouts/320x240`, `layouts/480x320`) with a page of light tiles; see
`sunton-28-example.yaml`, `sunton-28-9342-example.yaml` and `elecrow-35-example.yaml`.

## What's in a board file

```yaml
packages:
  device: !include ../devices/ESP32-8048S050.yaml   # the hardware: display, touch, backlight, ...
  size: !include ../layouts/ui/sizes/480x800.yaml   # the layout's sizes for this screen
  cameras: !include ../layouts/ui/pages/cameras.yaml # the Cameras page that suits it
```

* `devices/<board>.yaml` is hardware only: display, touchscreen (`id: main_touchscreen`),
  backlight (`id: backlight`, a light), plus audio where the board has it. It's usable on
  its own, without this layout.
* `layouts/ui/sizes/<canvas>.yaml` sets the layout's sizes for one canvas: fonts, the
  header and tab bar heights, tile height, the Weather page's columns, where the
  screensaver puts the clock. Boards with the same canvas share one.
* The Cameras page depends on how the board draws video. `cameras.yaml` is for the
  rotated Suntons (video windows in the panel's own coordinates, streams pre-rotated),
  `cameras_320x480.yaml` and `cameras_p4.yaml` for the portrait Guitions,
  `cameras_preview.yaml` a stand-in for the desktop.

## Adding a board

1. **The device file.** Write `devices/<board>.yaml` for the hardware, with the ids the
   layout relies on: `main_display`, `main_touchscreen` (as a list entry), and `backlight`.
   If the panel is landscape, rotate it portrait with `lvgl: rotation:` and leave the
   touch transform to LVGL.
2. **A size.** If its canvas is 480x800 or 320x480, use that size file. Otherwise copy the
   closer one to `layouts/ui/sizes/<W>x<H>.yaml` and adjust it, using the desktop preview
   (`tools/snapshot.py`) to check each page: copy `boards/sdl-480x800.yaml` with your size,
   and set `sdl_width` and `sdl_height` in the size file.
3. **A Cameras page.** Pick the one whose drawing matches (rotated or not, and whether the
   display takes whole frames or strips), or copy one and change the window positions.
4. **The board file.** `boards/<board>.yaml` with the three includes, and an example config
   next to the others.
