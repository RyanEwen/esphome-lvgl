# Widgets

The widgets you put on your own pages (your Rooms page, your room pages). Each is one file in
`layouts/widgets/`, included with a few `vars`, one line per widget. The demo house
(`demo/pages/`) uses most of them, so it's the quickest place to see one in context.

## How a page of widgets works

A page file is a list of includes: `widgets/page.yaml` makes the page, then each
widget's line adds it, in order, two to a row.

```yaml
packages:
  - !include { file: ../../layouts/widgets/page.yaml, vars: { page: room_office, title: Office } }
  - !include { file: ../../layouts/widgets/light.yaml, vars: { page: room_office, entity_id: light.office_ceiling, text: Ceiling } }
  - !include { file: ../../layouts/widgets/dimmer.yaml, vars: { page: room_office, entity_id: light.office_lamp, text: Desk Lamp } }
```

Every widget takes:

* `page`: the page it goes on, as in that page's `widgets/page.yaml` line.
* `entity_id`: the Home Assistant entity it shows and controls.
* `text`: its name on screen.
* `uid` (optional): a short id for its parts, unique on the panel. It defaults to the
  entity id after the dot (`office_lamp`); set it when two widgets share an entity.
* `width`, `height` (optional): a half-width widget at this screen's tile height by default.
  `width: 100%` takes the whole row.

A widget brings its own Home Assistant subscription, so there's nothing else to add.
Widgets with state show it as a bar down the left edge: yellow when on, white when off, faint
until Home Assistant has said.

**`widgets/page.yaml`** takes `page` (its id), `title` (in the header, and its name in the
Home page list), and optionally `skip: "false"` for a page you swipe to like a tab (room
pages, opened from their widget, keep the default `"true"`), `tab` (the tab lit while it's
open, default `rooms`) and `scroll: "true"` for a page whose widgets may not fit the screen.

## Lights

| Widget | What it does | Extra vars |
| --- | --- | --- |
| `light.yaml` | Tap toggles the light. Also a light group with no page of its own. | |
| `dimmer.yaml` | Tap toggles; the chevron opens the brightness pop-up: a 1-100% slider and an On/Off toggle. | |
| `dimmer_hold.yaml` | `dimmer.yaml`, plus a long press that turns it on at 1% (a bedside lamp). | |
| `room.yaml` | A room: tap toggles every light in it; the chevron (or a long press) opens the room's page. | `room`: that page's id |

```yaml
  - !include { file: ../../layouts/widgets/room.yaml, vars: { page: rooms, entity_id: light.bedroom_lights, text: Bedroom, room: room_bedroom } }
```

A room widget works best on a [light group](https://www.home-assistant.io/integrations/group/)
helper: the group is on while any light in it is, and its brightness is the average of the
lights that are on (which is also what auto-dim reads).

## Media

| Widget | What it does | Extra vars |
| --- | --- | --- |
| `tv.yaml` | A full-width card: the TV's state and what's playing, then power, previous, play/pause and next. | `text` defaults to TV |
| `volume.yaml` | Volume down and up, side by side; holding one keeps stepping. No `text`. For a device with no media_player (an IR soundbar), give `down_script` and `up_script` plus a `uid` instead of `entity_id`. | `height` (default a little under the tile height), `down_script`, `up_script` |
| `media.yaml` | A plain widget that toggles a media player's power; on while it's playing, paused or idle. | `icon_on`, `icon_off` |

```yaml
  - !include { file: ../../layouts/widgets/tv.yaml, vars: { page: room_bedroom, entity_id: media_player.bedroom_tv } }
  - !include { file: ../../layouts/widgets/volume.yaml, vars: { page: room_bedroom, entity_id: media_player.bedroom_tv } }
  # a soundbar with no media_player: volume through two scripts (e.g. IR codes)
  - !include { file: ../../layouts/widgets/volume.yaml, vars: { page: room_living_room, uid: living_room_soundbar, down_script: script.living_room_volume_down, up_script: script.living_room_volume_up } }
```

## Scripts, switches and covers

| Widget | What it does | Extra vars |
| --- | --- | --- |
| `script.yaml` | An icon and a name; a tap runs the script (`script.turn_on`). No state. | `icon` |
| `toggle.yaml` | On/off for anything with a `toggle` action: a switch, a fan, an `input_boolean`. | `icon_on`, `icon_off` |
| `cover.yaml` | Opens and closes a cover: a blind, a garage door. | `icon_open`, `icon_closed` |

Icons are MDI glyphs (see "Icons" below):

```yaml
  - !include { file: ../../layouts/widgets/script.yaml, vars: { page: room_bedroom, entity_id: script.bedtime, text: Bedtime, icon: "\U000F0594", width: 100% } }
```

## Thermostats and numbers

Stepper cards (contributed), the page wide: `[-] value [+]`. Taps change the value at once
and send one call to Home Assistant a second after the last tap.

| Widget | What it does | Extra vars |
| --- | --- | --- |
| `thermostat.yaml` | A `climate` entity's target, with the room temperature under its name and an icon that shows heating or cooling. | `step` (default 0.5), `text` defaults to Thermostat |
| `number.yaml` | A `number` or `input_number`; its range and step come from the entity. | `icon`, `unit` (shown after the value) |

```yaml
  - !include { file: ../../layouts/widgets/thermostat.yaml, vars: { page: room_living_room, entity_id: climate.living_room } }
```

A thermostat that's off, or holding a high/low range, has no single target: the card shows
`--`. Changing the mode is left to Home Assistant.

## 3D printers

Bambu Lab printer cards (contributed), the page wide: the printer's name and its AMS trays
(colour and amount left), then its status, time left, finish time and a progress bar.
`demo/pages/room_office.yaml` has two, on a page with `scroll: "true"`.

| Widget | What it shows |
| --- | --- |
| `printer_combined.yaml` | One AMS's four trays on the name's line: compact, for a printer with a single AMS that's always there. |
| `printer.yaml` | A row per AMS (up to four, plus two AMS HTs) with humidity and a drying indicator, each appearing when the unit reports. |

Instead of `entity_id` they take:

* `prefix`: the start of the printer's entity ids, `sensor.<prefix>_...`.
* `integration`: where the printer comes from in Home Assistant:
  [ha-bambulab](https://github.com/greghesp/ha-bambulab) (`bambulab`, the default) or
  [printstream](https://printstream.app) (`printstream`), another Bambu Lab integration. Each names its entities its
  own way; the widget reads the right ones.

```yaml
  - !include { file: ../../layouts/widgets/printer_combined.yaml, vars: { page: room_office, prefix: p1s, text: P1S } }
  - !include { file: ../../layouts/widgets/printer.yaml, vars: { page: room_office, prefix: x1c, text: X1 Carbon, integration: printstream } }
```

## Your own widgets

A widget (`layouts/widgets/<name>.yaml`) is a small wrapper: it puts its part on the page's
grid (`${page}_widgets`) and includes the part's sensors. The parts, in
`layouts/widgets/parts/`, follow three rules:

* `<name>.yaml` is what's drawn: merged into a `button:` or `obj:` with `<<: !include`.
* `<name>.sensors.yaml` feeds it from Home Assistant: a package, included once per widget
  with the same `uid` and `entity_id`. A variant (`dimmer_hold.yaml`) uses its base's.
* `widgets/<name>.yaml` wraps `widgets/parts/<name>.yaml`.

`parts/base/` holds the templates the parts build on; `<name>.logic.yaml` (outside
`widgets/`, e.g. `layouts/ui/brightness_popup.logic.yaml`) is a layout part's scripts and
state. To make a new kind of widget, copy the closest widget and its part, and rename both.
Leave out a part's sensors and the build fails on purpose (`..._widget_sensor` not found)
rather than leaving a widget that never updates.

## Parts with no widget yet

For your own wrappers; give them a `uid`, `width` and `height`. All in
`layouts/widgets/parts/`.

* **Status displays** (`status/binary`, `status/threshold`, `status/value`, `status/alarm`,
  each with its `.sensors.yaml`): read-only, for a binary sensor, a number crossing a
  threshold, a value with a unit, or an alarm panel's state.
* `light_hold.yaml`, `light_group_hold.yaml`, `light_icons.yaml`: a light or group with a
  long press to 1%, or with icons of your choosing.
* `detail/` and `toggle_confirm_off.yaml`: kept from older layouts; they need globals and
  pages no shipped layout defines (see each file's header).

## Built into the layout

You don't place these; they come with `layouts/ui/layout.yaml`.

* **Header**: the page title, and the clock (hold it for Settings).
* **Tab bar** (`layouts/ui/tabs.yaml`): Rooms, Weather, Agenda, Cameras. Your
  Rooms page's id must be `rooms`.
* **Brightness pop-up** (`layouts/ui/brightness_popup.yaml`): opened by dimmer widgets.

And one you add to a config when the board has a microphone:

* **Voice** (`features/voice/voice.yaml`): say "Okay Nabu" to talk to Assist; a
  pop-up shows what it heard and the reply. Needs the board's `microphone` and a media
  player (the Guition 4.3" device file has both).

## Icons

Icons are [Material Design Icons](https://pictogrammers.com/library/mdi/) glyphs, written as
`"\U000Fxxxx"` (the icon's codepoint). The fonts carry only the glyphs listed in
`layouts/fonts/glyphs.yaml`, so a new icon must be added there too, or it draws as a blank
box; `tools/check_glyphs.py` finds the missing ones.
