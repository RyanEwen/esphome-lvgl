# Getting started

This walks through putting the panel layout on a board, then making it yours: your rooms,
your lights, your weather and cameras. Everything is plain ESPHome YAML; no code to write.

## 1. Flash an example

Pick the example for your board:

| Board | Example |
| --- | --- |
| Sunton 5" `ESP32-8048S050` | `sunton-50-example.yaml` |
| Sunton 4.3" `ESP32-8048S043` | `sunton-43-example.yaml` |
| Guition 3.5" `JC3248W535` | `guition-35-example.yaml` |
| Guition 4.3" `JC4880P443C` (ESP32-P4, with voice) | `guition-43-example.yaml` |

Copy it to a name of your own, say `kitchen-panel.yaml`, and change `name:` and
`friendly_name:` at the top. Your copy stays out of git: `.gitignore` ignores every
top-level config except the examples and `common.yaml`, so the ESPHome dashboard still
lists it here.

Create `secrets.yaml` with your Wi-Fi and an API key:

```yaml
wifi_ssid: "your-network"
wifi_password: "your-password"
api_encryption_key: "..." # Home Assistant shows a generated one when you add an ESPHome device
```

Flash it, then add the panel in Home Assistant (Settings > Devices & services > ESPHome).
On the device's **Configure** page, turn on **Allow the device to perform Home Assistant
actions**: the widgets, Weather and Agenda all need it.

The panel now runs the demo house: made-up rooms whose entity ids won't exist in your Home
Assistant, so the widgets stay on their faint "unknown" bar. Next, give it your rooms.

On the panel: the tabs along the bottom switch between Rooms, Weather, Agenda and Cameras;
a room widget's chevron opens that room; and **pressing and holding the clock** in the
top-right corner opens **Settings** (home page, brightness, auto-dim, network details).
Left alone, it shows the screensaver; a tap wakes it.

## 2. How a config is put together

```yaml
packages:
  common: !include common.yaml               # Wi-Fi, API, OTA, a few diagnostics
  board: !include boards/sunton-50.yaml      # the hardware, sized for this screen
  ui: !include layouts/ui/layout.yaml        # the layout: header, tabs, Weather, Agenda, ...
  rooms: !include demo/pages.yaml # Rooms tab and each room's page: the part you write
```

* **`boards/`**: one file per board. It brings in the hardware (`devices/`), the layout's
  sizes for that screen (fonts, tile heights, positions) and the Cameras page that suits
  it. You never touch sizes unless you add a board; see [boards.md](boards.md).
* **`layouts/ui/layout.yaml`**: the layout. It brings the header (title and clock), the tab
  bar (Rooms, Weather, Agenda, Cameras), and the pages that work the same in every home:
  Weather, Agenda, the photo screensaver and Settings.
* **Your pages**: the Rooms tab and any room pages it opens. These are yours to write.

## 3. Make the rooms yours

Copy the demo house to `private/`, which git ignores, and point your config at your copy:

```
cp -r demo private
```

```yaml
  rooms: !include private/pages.yaml
```

`demo/` and `private/` sit at the same depth, so the copied pages' includes still work.

### The Rooms page

`private/pages/rooms.yaml` is the first tab: one widget per room. A page file is a list of
includes, one line each: the page first, then its widgets in the order they appear, two to a
row.

```yaml
packages:
  - !include { file: ../../layouts/widgets/page.yaml, vars: { page: rooms, title: Rooms, skip: "false" } }
  - !include { file: ../../layouts/widgets/light.yaml, vars: { page: rooms, entity_id: light.kitchen_lights, text: Kitchen } }
  - !include { file: ../../layouts/widgets/light.yaml, vars: { page: rooms, entity_id: light.porch_light, text: Porch } }
```

Each widget names the page it goes on (`page:`), its entity and its name on screen. It brings
everything it needs with it: what's drawn, what a tap does, and the Home Assistant
subscription that keeps its state bar right (yellow on, white off). The Rooms page's id
must be `rooms` (the first tab opens it), and `skip: "false"` lets you swipe to it like
the other tabs. [widgets.md](widgets.md) lists every widget.

### A room with several lights

Give the room a light group in Home Assistant (Settings > Devices & services > Helpers >
Group > Light group), then use a **room widget** for it on Rooms: tapping it toggles the
whole room, and its chevron (or a long press) opens the room's own page.

```yaml
  - !include { file: ../../layouts/widgets/room.yaml, vars: { page: rooms, entity_id: light.bedroom_lights, text: Bedroom, room: room_bedroom } }
```

The room page (`demo/pages/room_bedroom.yaml`) is a page file like Rooms, opened from its
room widget rather than swiped to: each light, a TV card, volume buttons, a script button.

```yaml
packages:
  - !include { file: ../../layouts/widgets/page.yaml, vars: { page: room_bedroom, title: Bedroom } }
  - !include { file: ../../layouts/widgets/light.yaml, vars: { page: room_bedroom, entity_id: light.bedroom_ceiling, text: Ceiling } }
  - !include { file: ../../layouts/widgets/dimmer.yaml, vars: { page: room_bedroom, entity_id: light.bedroom_lamp, text: Lamp } }
  - !include { file: ../../layouts/widgets/tv.yaml, vars: { page: room_bedroom, entity_id: media_player.bedroom_tv } }
```

Add it to `private/pages.yaml`:

```yaml
packages:
  rooms: !include pages/rooms.yaml
  room_bedroom: !include pages/room_bedroom.yaml
```

### The Home page

`home_page:` in your config is the page a panel opens on at boot and returns to from the
screensaver: a page's title, as in `home_page: Bedroom` for a bedside panel. Every page
made with `widgets/page.yaml` is in the list. On a running panel the Home page is also a
dropdown in Settings and a select in Home Assistant.

### Auto-dim rooms

Settings can tie a panel's brightness to a room: the display dims while that room's lights
are all off or at 1%. Each room it can follow is one line in `private/pages.yaml`, with a
light or a light group covering the room:

```yaml
  dim_bedroom: !include { file: ../layouts/ui/autodim_room.yaml, vars: { uid: bedroom, label: Bedroom, entity_id: light.bedroom_lights } }
```

A panel's starting room can be set in its config: `autodim_room_default: "Bedroom"`.

## 4. Your house settings

The layout's defaults are in `layouts/ui/main.yaml` under `substitutions:`. Set yours in a
file of your own, `private/house.yaml`, and include it after `ui:` in each panel config so
every panel shares them:

```yaml
# private/house.yaml
substitutions:
  weather_entity: weather.forecast_home       # Weather page and screensaver
  calendars: ""                               # empty = every calendar (each panel can hide some)
  screensaver_photos: "true"                  # photos on the screensaver (off by default)
  photo_server: http://192.168.1.10:8000      # where the photo-frame service runs
  go2rtc: http://192.168.1.10:1984            # the Cameras page
  cam_1: front_door                           # go2rtc stream keys, and the names on the buttons
  cam_1_name: Door
  cam_2: driveway
  cam_2_name: Drive
  cam_3: backyard
  cam_3_name: Back
  cam_4: garage
  cam_4_name: Garage
```

The Weather page shows temperatures in whatever unit Home Assistant uses, but colours them
on a Celsius scale (blue for cold through red for hot), so in Fahrenheit they all run warm;
`temperature_color()` in `layouts/ui/ui.h` holds the scale.

```yaml
packages:
  common: !include common.yaml
  board: !include boards/sunton-50.yaml
  ui: !include layouts/ui/layout.yaml
  house: !include private/house.yaml
  rooms: !include private/pages.yaml
```

### Cameras

The Cameras page plays MJPEG streams from [go2rtc](https://github.com/AlexxIT/go2rtc) (the
WebRTC Camera integration runs one; port 1984). The panel asks for `esp_<key>` streams
already sized for its screen, so go2rtc does the scaling and the panel only decodes. The
names it uses, per board:

| Board | Two feeds | Fullscreen |
| --- | --- | --- |
| Sunton 5" / 4.3" | `esp_<key>_wall` (480x270, turned 90° clockwise) | `esp_<key>_wallfull` (670x480) |
| Guition 3.5" | `esp_<key>` (320x180) | `esp_<key>_full` (320x480, turned 90°) |
| Guition 4.3" | `esp_<key>_480` (480x270) | `esp_<key>_full_480` (480x680, turned 90°) |

For example, in go2rtc's config:

```yaml
streams:
  esp_front_door_wall: ffmpeg:rtsp://user:pass@camera/stream2#video=mjpeg#raw=-vf scale=480:270,transpose=1,fps=5 -q:v 7
  esp_front_door_wallfull: ffmpeg:rtsp://user:pass@camera/stream1#video=mjpeg#raw=-vf scale=670:480:force_original_aspect_ratio=decrease,pad=670:480:(ow-iw)/2:(oh-ih)/2,hflip,vflip,fps=10 -q:v 7
```

The 5" and 4.3" Suntons draw portrait by rotating in software, and the camera component
draws straight to the panel, so their streams arrive pre-rotated.

### Screensaver photos

After a while untouched (**Screensaver after**, a setting on the panel's device page in
Home Assistant), the screensaver shows the time, the weather and today's events. Behind
them, one of:

* **Your photos**, from `tools/photo-frame`: a small service that serves a folder of photos
  (or an Immich album) cropped to each panel. Run it with `docker compose up -d` in that
  folder, put photos in `tools/photo-frame/photos/`, and set `screensaver_photos: "true"`
  and `photo_server: http://<host>:8000`. A new photo fades in every minute (**Photo every**, on
  the device page too).
* **A still image** built into the firmware, as the demo does (`demo/screensaver_still.yaml`;
  swap in your own picture there). No server needed.
* **Black**, with neither.

### Voice

On a board with a microphone and speaker (the Guition 4.3", `guition-43-example.yaml`),
`features/voice/voice.yaml` adds voice: say **"Okay Nabu"**, then a command. The wake
word is heard on the panel itself; what you say goes to Home Assistant's **Assist**, and the
reply is spoken on the panel's speaker and shown in a pop-up. Home Assistant needs:

1. **An Assist pipeline** with speech-to-text and text-to-speech (Settings > Voice
   assistants). Either:
   * **Home Assistant Cloud** (Nabu Casa), which provides both; or
   * **local**, with the [Wyoming](https://www.home-assistant.io/integrations/wyoming/)
     services: [Speech-to-Phrase](https://github.com/OHF-Voice/speech-to-phrase) (fast, for
     device commands) or [Whisper](https://github.com/rhasspy/wyoming-faster-whisper) for
     speech-to-text, and [Piper](https://github.com/rhasspy/wyoming-piper) for
     text-to-speech. Run them as add-ons (Home Assistant OS) or containers, add each with
     the Wyoming Protocol integration, then make a pipeline that uses them.
2. **The panel set to use it**: on its device page in Home Assistant, set **Assistant** to
   that pipeline (or leave it on the preferred one).
3. **Your devices exposed to Assist** (Settings > Voice assistants > Expose), so commands
   like "turn on the kitchen lights" can reach them.

Without a pipeline the pop-up shows the error and goes away; the rest of the panel works as
usual.

## 5. Check it without a board

ESPHome can build a panel for your computer instead of a board, drawing the screen in a
window with [SDL2](https://www.libsdl.org/). It's the quickest way to try a page: click to
tap, drag to move a slider, and change the YAML and run it again in seconds.

1. Install SDL2's development files: `sudo apt install libsdl2-dev` (Debian/Ubuntu),
   `brew install sdl2` (macOS), or your system's SDL2 package.
2. Run one of the desktop configs:

   ```
   esphome run sdl-example.yaml        # 480x800: the Sunton and Guition 4.3"/5" screens
   esphome run sdl-320-example.yaml    # 320x480: the Guition 3.5"
   ```

   It builds for your computer and opens a window running the demo house. Close the window
   or press Ctrl-C to stop.

**No install: Docker.** The ESPHome image can build and run it, installing SDL2 inside the
throwaway container; the window opens on your desktop through its X server (Linux, including
Wayland desktops via XWayland, and Windows 11's WSL2). From the repo:

```
xhost +local:            # let local containers open a window (undo: xhost -local:)
docker run --rm -it --network host \
  -e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v "$PWD":/config -w /config --entrypoint bash esphome/esphome \
  -c "apt-get update -qq && apt-get install -y -qq libsdl2-dev && esphome run sdl-example.yaml"
```

`--network host` also lets Home Assistant reach the preview (see "With real data" below).
On macOS, run [XQuartz](https://www.xquartz.org/) and use `-e DISPLAY=host.docker.internal:0`
after `xhost +localhost` in place of the X11 lines.

The desktop configs (`boards/sdl-480x800.yaml`, `boards/sdl-320x480.yaml`) are the real
layout with a stand-in for what a computer doesn't have: no camera video, sample Wi-Fi
details in Settings, and Weather, Agenda and a few light states filled with sample data, so
every page has something on it without Home Assistant. The idle screensaver is off (the
config sets **Screensaver after** to 0), so it won't cover the page you're testing; hold
the clock for Settings.

**Your own pages.** Copy `sdl-example.yaml`, point its `rooms:` at `private/pages.yaml`,
and add `house: !include private/house.yaml` after `ui:`. Its sample-state lambda names the
demo's widgets, so delete that block (or change the ids to yours).

**With real data.** The desktop build runs ESPHome's API like a board does, so Home
Assistant can adopt it: add an ESPHome device at your computer's address, port 6053, and
allow it to perform actions. The widgets, Weather and Agenda then show your real home.
Remove it from Home Assistant when you're done.

**Screenshots.** `tools/snapshot.py` builds a desktop config, shows each page in turn and
saves it as a PNG, without a window:

```
tools/snapshot.py sdl-example.yaml --all        # snapshots/<page>.png; --all includes room pages
```

or, with nothing installed, in Docker (no window needed):

```
docker run --rm -v "$PWD":/config -w /config -e SDL_VIDEODRIVER=offscreen --entrypoint bash esphome/esphome \
  -c "apt-get update -qq && apt-get install -y -qq libsdl2-dev && python3 tools/snapshot.py sdl-example.yaml --all"
```

## Next

* [widgets.md](widgets.md): every widget and card, with the YAML to use it.
* [boards.md](boards.md): the supported boards, and adding one.
