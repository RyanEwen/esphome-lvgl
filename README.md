# ESPHome + LVGL on cheap touchscreen devices

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/S6S21K2LK2)

## Supported Devices
* Guition `JC3248W535` 3.5" 320x480 portrait, with capacitive touch and USB-C. [AliExpress Link](https://www.aliexpress.com/item/1005007566046827.html).
* Sunton `ESP32-2432S028R` 2.8" 240x320 portrait, with resistive touch and USB micro-B. [AliExpress Link](https://www.aliexpress.com/item/1005004502250619.html).
* Sunton `ESP32-2432S028`, USB-C + micro-B revision with an ILI9342 panel: 2.8" 320x240 landscape, drawn portrait, with resistive touch. Same board as the `ESP32-2432S028R` otherwise.
* Sunton `ESP32-8048S043` 4.3" 480x800 portrait, with capactivive touch and USB-C. [AliExpress Link](https://www.aliexpress.com/item/1005004788147691.html).
* Sunton `ESP32-8048S050` 5.0" 480x800 portrait, with capactivive touch and USB-C. [AliExpress Link](https://www.aliexpress.com/item/1005004952694042.html).
* Elecrow CrowPanel `DIS05035H` (v2.2) 3.5" 320x480 portrait, with resistive touch and USB-C. [Manufacturer's Link](https://www.elecrow.com/esp32-display-3-5-inch-hmi-display-spi-tft-lcd-touch-screen.html).

## Changelog
### 2026-09-22
* Add `tools/`: `lint_configs.py`, `check_glyphs.py`, `compare_configs.py` and `snapshot.py`, for catching config mistakes that build fine but misbehave on the panel. No config changes. See "Testing tools".
### 2026-09-21
* Add `devices/ESP32-2432S028-9342.yaml` and `sunton-28-9342-example.yaml`, for the USB-C + micro-B CYD with an ILI9342 panel. It includes `ESP32-2432S028R.yaml` and changes only the panel: ESPHome's own `ESP32-2432S028-9342` display model in RGB order, `lvgl: rotation: 270`, and the touch transform and calibration to match.
* Add `printers-on-a-diet` to `320x240`: a printers page cut down to fit a board without PSRAM, commented out in the 2.8" examples and `all.yaml`. It lists each printer's AMS units rather than using `tile.yaml`'s twelve self-revealing slots, which don't fit a board without PSRAM. `320x240`'s `text_sm` drops from 14 to 12 so `100%` fits a tray pill; only the printer tiles use it. See "Putting a low-memory panel on a diet".
* Add stepper tiles, `[-] value [+]`: `widgets/stepper/climate/` for a thermostat's target temperature and `widgets/stepper/number/` for a `number` or `input_number`. Every layout has a Climate page with one of each, commented out in the examples and `all.yaml` so nothing changes until you opt in. See "How to add a thermostat or number tile".
* [Breaking change] Each page is now its own file, `layouts/<WxH>/pages/<page>.yaml`, holding the page and the sensors its tiles need, and the top-level config lists the pages it wants in navigation order. A config that includes only `layout:` now gets no pages: copy the page lines from the matching `*-example.yaml`, or include `layouts/<WxH>/all.yaml` for every page. The sizing the pages share moved from the layout's `.sizing` anchors to `layouts/<WxH>/vars/`. See "How to choose which pages a device shows".
* Light and light-group tiles no longer set the light to 1% on a long press; a hold was too easy to hit by accident on a wall panel, and on a group tile it dimmed every light in the group. To keep it on a tile, include `dim_on_hold.yaml` instead of `widget.yaml` from the same directory (`light_buttons/` or `light_group_buttons/`); the vars and the sensors package are unchanged.
* Add `features/`, for behaviour a panel may or may not want, opted into from the top-level config. Device files stay hardware only and layouts stay pages only. See "Optional features".
* Add `features/idle/`: dim, go home and sleep when idle, wake on touch, with the brightness ceiling following the sun or a light sensor.
* `devices/SDL.yaml` declares its touchscreen as a list with `id: main_touchscreen`, like the other device files, so features can extend it.
* The boot screen is dark rather than white, so a reboot at night does not light the room.
* [Breaking change] `widgets/printers/widget.yaml` and `widgets/printers/sensors.yaml` are replaced. A page that included them switches the tile body to `widgets/printers/tile_combined.yaml` and its sensors to `widgets/printers/printer_combined.sensors.yaml`; the printers page in each layout shows the shape. The tile looks the same as before.
* A printer tile can instead use `widgets/printers/tile.yaml` with `printer.sensors.yaml`: one row per AMS unit, with humidity and a heater icon, and rows that appear and disappear with the hardware. See "How to show every AMS unit".
* Tray text takes its colour from the filament, so a white or black spool stays readable. An idle or offline printer shows an empty grey bar rather than the last job's full one.
* `common.yaml` now always reports **Uptime** and **Reset Reason**, so an unexplained restart leaves evidence. `features/diagnostics/` adds opt-in heap, loop-time and PSRAM sensors for chasing a leak. See "Optional features".
* [Breaking change] **Restart** is now a button rather than a switch, so Home Assistant shows it as an action instead of an on/off state. Its entity moves from `switch.<name>_restart` to `button.<name>_restart`; update any automation or dashboard that pressed the old one. Home Assistant removes the old switch by itself once the device reconnects.
* [Breaking change] **WiFi Strength** is gone. It was WiFi Signal rescaled to a percentage, but it kept the dBm sensor's `signal_strength` device class, which Home Assistant only accepts in dB or dBm and warned about. Use WiFi Signal.
* **Uptime** reports the boot time, once per boot, rather than a seconds count every minute. If it reads unavailable after the update, reload the device in Settings > Devices & services > ESPHome: Home Assistant keeps the old seconds unit on the existing entity and rejects the new value.
* Add `features/ble_proxy/`: the panel as a Home Assistant Bluetooth proxy, with a switch to turn it off. Boards with PSRAM only; it costs ~95KB of internal RAM and some WiFi latency. See "Optional features".
* Add `features/sleep_clock/sleep_clock.yaml`: a dim split-flap clock in place of the dark sleep, with its own brightness and optional red night colours. Needs `features/idle/idle.yaml`. See "Optional features".
* A **24-hour time** switch (in the shared header package) sets the header clock, the sleep clock and the printer end times.
* Every layout's `lvgl:` block now has `id: main_lvgl`, for features that need the LVGL component itself.
* ESPHome 2026.9.0's bundled LVGL 9.5.0 leaks memory for every frame that draws an object scaled to 0 (fixed in LVGL 9.6.0; esphome/esphome#19439). If you animate `transform_scale_x/y`, hide the object while its scale is 0.
* Every layout has a **Home page** select and a **Show \<page\> page** switch per page in Home Assistant. By default every page shows and home is `home_page`, so nothing changes until you use them. A page hidden with `skip: true` in YAML stays hidden. See "How to run one image on several panels".
### 2026-09-17
* Document that every supported board draws portrait, and that the files in `layouts/` are named for the panel's nominal landscape resolution rather than for the canvas LVGL draws on. No config changes; the device files were already correct.
* [Breaking change] `common.yaml` now requires an encrypted API and OTA. Add an `api_encryption_key` to your `secrets.yaml` (Home Assistant shows a generated key when adding an ESPHome device, or see the [API docs](https://esphome.io/components/api/)), then reflash each device and enter the same key in Home Assistant. A device that is only reachable over OTA should be flashed before Home Assistant loses the connection to it.
* A bare `encryption:` under the `esphome` OTA platform reuses the API key, so there is no second secret to manage, and its presence is what drops the plaintext OTA fallback (removed from ESPHome after 2027.3.0).
* Update `common.yaml` to require ESPHome min version 2026.9.0, which is the release that added OTA encryption.
### 2024-11-06
* Update `common.yaml` to require ESPHome min version 2024.11.0 (currently in dev) due to upcoming changes to some display and touch drivers, and to add support for the Guition device which uses drivers not supported on the stable release as of yet.
* Update Sunton `ESP32-8048S043` and `ESP32-8048S050` configs to support upcoming ESPHome changes that affect display & touch rotation.
* Add support for Guition `JC3248W535` 3.5" device.
* Tweak devce configs so that majority of sections are in list format rather than a mix of formats.
* Fix touchscreen configs for devices with resistive touch on newer ESPHome builds.
### 2024-11-01
* [Breaking change] Moved `device_name` and `friendly_name` from `substitutions:` to `esphome:` at the top-level. This allows for ESPHome's Rename Hostname feature to work again.
* Added an `id` to each device's `display:` and `touchscreen:` config to allow extending them more easily (for example, if you want to rotate a display / touchscreen from the top-level config for a specific device).

## Orientation and Layout Naming
Every board here draws portrait. The files in `layouts/` are named for the panel's nominal landscape resolution rather than for the canvas LVGL ends up with, so `layouts/480x320.yaml` drives a 320px wide, 480px tall canvas:

| Board | Panel | Canvas LVGL draws on | Layout |
| --- | --- | --- | --- |
| Guition `JC3248W535` | 320x480 | 320x480 | `layouts/480x320.yaml` |
| Elecrow `DIS05035H` | 320x480 | 320x480 | `layouts/480x320.yaml` |
| Sunton `ESP32-2432S028R` | 240x320 | 240x320 | `layouts/320x240.yaml` |
| Sunton `ESP32-2432S028` (ILI9342) | 320x240 | 240x320, via `rotation: 270` | `layouts/320x240.yaml` |
| Sunton `ESP32-8048S043` | 800x480 | 480x800, via `rotation: 90` | `layouts/800x480.yaml` |
| Sunton `ESP32-8048S050` | 800x480 | 480x800, via `rotation: 90` | `layouts/800x480.yaml` |

Widget widths in the layouts are percentages, which is what lets one layout serve two different panels. Any size given in pixels has to be budgeted against the canvas width in the table above and not against the layout's filename, which is roughly 150px narrower than the name suggests on the 3.5" boards.

## File Structure
If all you are looking for is a device-specific config then look no further than the `devices/` directory. The YAML files in there are clean and free from anything not related to the devices themselves. They are intended to be used as [Packages](https://esphome.io/components/packages.html) in a higher-level YAML config file, which allows for device-specific settings and common settings to be kept in separate files, avoiding duplicate code and making it easier to update groups of devices. 

The YAML files in the root of this repo demonstrate how to use each device's config file with a common config, as well as a resolution-specific (but not device-specific) LVGL config/layout. 

Each layout is split by resolution:
* `layouts/<WxH>.yaml` - fonts, theme, the header, footer and boot screen, and `go_home`. No pages of its own.
* `layouts/<WxH>/pages/<page>.yaml` - one page and the sensors its tiles need, as a package. The top-level config lists the ones it wants, after `layout:`, in navigation order.
* `layouts/<WxH>/all.yaml` - every page for that resolution, for a config that wants them all.
* `layouts/<WxH>/vars/` - the sizing the pages share (page padding, button sizes, and so on).

`tools/` has scripts that check configs before you flash them; see "Testing tools".

## Advanced YAML Techniques
Aside from the Packages feature used to separate device-specfic YAML from common YAML config, there are some other potentially unfamiliar techniques in use here. For example, the files within `layouts/` use [YAML anchors and aliases](https://ref.coddy.tech/yaml/yaml-anchors) which help reduce code duplication. I use anchors and aliases instead of `style_definitions` and `styles` as anchors can be used on anything instead of being restricted to just styles, and because they override `theme` settings when used (there is a bug or perhaps odd design choice that prevent `styles` from overriding `theme`). I define most of my anchors within a made-up section called `.sizing` because top-level sections prefixed with a period do not cause errors when parsed by ESPHome. 

Anchors don't reach across files, though, so the page files can't use the layout's. The sizing they share lives in small files under `layouts/<WxH>/vars/` instead, merged the same way an anchor was: `<<: !include ../vars/page.yaml` in place of `<<: *page_styles`.

## Required Setup in Home Assistant
Don't forget to Configure your ESPHome Devices in Home Assistant, to allow them to perform actions:
![Allow device to perform Home Assistant actions](https://github.com/user-attachments/assets/ca5c3cb4-a4fd-44ea-a5f6-159ccd6401df)

## Optional Features
Behaviour that not every panel wants lives in `features/` and is opted into from the top-level config. List features **after** `layout:`:

```yaml
packages:
  common: !include common.yaml
  device: !include devices/JC3248W535.yaml
  layout: !include layouts/480x320.yaml
  pages: !include layouts/480x320/all.yaml
  idle: !include features/idle/idle.yaml
  ceiling: !include features/idle/sun.yaml
```

Each setting is a Home Assistant control whose starting value comes from a substitution, so the YAML sets the default and Home Assistant can change a running panel without a reflash. To change a default, set the substitution in the top-level config:

```yaml
substitutions:
  idle_sleep_minutes: "60"
```

A feature relies on stable ids from the device file (`backlight`, `main_touchscreen`) and the layout (`go_home`, `home_btn`), which every file here already provides.

### `features/idle/idle.yaml`
Dims the backlight, returns to the home page, and sleeps after the panel has been left alone; a touch wakes it. Controls: **Dim after**, **Dim level**, **Go home after**, **Sleep after** (minutes; 0 means never), a **Sleep now** button, and **Sleep last event**. Holding the footer's home button for 1.5 seconds also sleeps. The tap that wakes a dark panel is swallowed rather than pressing whatever it landed on.

### `features/idle/sun.yaml` and `features/idle/ambient_light.yaml`
The brightness ceiling that the dim and wake levels are relative to, in three bands: day 100%, dusk 60%, night 35%. `sun.yaml` uses Home Assistant's `sun.sun` elevation, for boards with no light sensor. `ambient_light.yaml` is for boards that have one: it needs a sensor with `id: ambient_light` reporting lux in the device file. Use one or neither; without one the ceiling stays at 100%.

### `features/diagnostics/memory.yaml` and `features/diagnostics/psram.yaml`
Sensors for chasing a leak or a slow crash: **Heap Free**, **Heap Min Free**, **Heap Largest Block** and **Loop Time**, plus **PSRAM Free** for boards with PSRAM (the Guition and the Sunton 4.3" / 5"). A leak shows as Free or Min Free trending down over hours; Largest Block falling while Free holds is fragmentation. Each reports once a minute, a recorder row a minute per sensor, so turn them on for the panel you are chasing a problem on rather than everywhere. Uptime and Reset Reason are always on, in `common.yaml`: the evidence for an unexplained restart can only be caught at the boot that follows it.
### `features/ble_proxy/ble_proxy.yaml`
Makes the panel a [Bluetooth proxy](https://esphome.io/components/bluetooth_proxy/) for Home Assistant, relaying advertisements and lending connection slots, so HA's Bluetooth reaches the room the panel is in. The **Bluetooth proxy** switch starts and stops the whole Bluetooth stack without a reflash; `ble_proxy_default` (`ON` or `OFF`) sets its first value.

It is not free, which is why it is a feature and off in the examples. Measured on a Guition `JC3248W535`:

* **Boards:** needs PSRAM and ~400KB of flash, so the Guition and the Sunton `ESP32-8048S043` / `ESP32-8048S050` only. The Elecrow and the CYD have no PSRAM, and the CYD's firmware no longer fits its app partition.
* **RAM:** ~95KB of internal RAM while running, which on the ESP32-S3 cannot move to PSRAM. Free heap went from 157KB to 62KB, and its low point from 135KB to 26KB.
* **WiFi:** WiFi and Bluetooth share one radio. Packet loss did not change, but the slowest replies got slower (p99 ping 1.0s to 2.5s). ESPHome's default scans continuously, which was worse again, so this feature listens 30ms in every 320ms instead. A panel with a weak WiFi signal feels this most.
* **CPU:** ~2% of a core.
* **Turning it off** hands most of the RAM back (the static ~23KB stays) and ends the radio sharing, but the heap stays fragmented until the next restart. Starting or stopping the stack pauses the screen for ~0.2s.

### `features/sleep_clock/sleep_clock.yaml`
A dim split-flap clock in place of the dark sleep, whether the sleep comes from the idle timer, holding the home button or **Sleep now**. Turn it on with the **Sleep clock** switch; **Sleep clock brightness** sets how bright it is, as a percentage of the ceiling. **Night colours** (At night / Always / Never) turns the face red, where "at night" follows `sun.yaml` or `ambient_light.yaml`. A touch returns to the page the clock replaced. Needs `features/idle/idle.yaml`, listed before it.

The card sizes default to the 320px-wide canvas of the 3.5" boards. For the others, set these substitutions in the top-level config (the matching examples carry them, commented out):

| Layout | `sleep_clock_card_width` | `_card_height` | `_card_gap` | `_pair_gap` | `_digit_size` |
| --- | --- | --- | --- | --- | --- |
| `320x240` | 50 | 84 | 4 | 12 | 66 |
| `480x320` | 68 (default) | 112 | 6 | 16 | 88 |
| `800x480` | 102 | 168 | 8 | 24 | 132 |

## How-tos
### How to choose which pages a device shows
List the pages after `layout:` in the device's config file. They appear in the order they are listed, so reordering the lines reorders the navigation, and leaving a line out leaves that page off the device:
```yaml
packages:
  common: !include common.yaml
  device: !include devices/JC3248W535.yaml
  layout: !include layouts/480x320.yaml
  lighting_1: !include layouts/480x320/pages/lighting_1.yaml
  printers: !include layouts/480x320/pages/printers.yaml
```

To get every page for the resolution, in its usual order, include `all.yaml` in place of the list:
```yaml
  pages: !include layouts/480x320/all.yaml
```

The examples list their pages, with the `all.yaml` line commented out above the list.

### How to specify the home page on a particular device
To change which page loads at boot time and when the home button is pressed on a particular device, adjust the `home_page` variable in the device's config file to the ID of the desired page. The page has to be one the config includes.

For example, to set a page with the ID `printers`, adjust this in your device's config file:
```yaml
substitutions:
  ...
  home_page: printers
```

This is the default for the **Home page** select in Home Assistant, which changes it on a running panel.

### How to hide pages on particular devices
The simplest way is to leave the page out of the device's page list (see "How to choose which pages a device shows").

To keep a page on the device but leave it out of the next/previous navigation, extend the desired `page` definition by adding `skip: true` using `!extend` (see [Packages](https://esphome.io/components/packages.html) feature). 

For example, to hide a page with the ID `bedroom`, add this to your device's config file:
```yaml
lvgl:
  pages:
    - id: !extend bedroom
      skip: true
```

Each page also has a **Show \<page\> page** switch in Home Assistant, which hides and shows it on a running panel; the home page can't be hidden, and a page given `skip: true` stays hidden whatever its switch says. To have a page start hidden but leave Home Assistant able to show it, default its switch to off instead:
```yaml
switch:
  - id: !extend show_bedroom_page
    restore_mode: RESTORE_DEFAULT_OFF
```

### How to run one image on several panels
Build one config and flash the same firmware to every panel of the same board, then set each one up from Home Assistant. Add this to the config:
```yaml
esphome:
  ...
  name_add_mac_suffix: true
```
Each panel then appends the end of its MAC address to its name (`guition-35-test-eda9b8`), so Home Assistant adds each one as its own device. Rename them there, and pick each panel's **Home page** and **Show \<page\> page** switches. The image has to carry every room's pages.

Two things to know: `esphome upload` can no longer find the panel by name, so pass each one's address with `--device`; and every panel shares the API key in `secrets.yaml`. Hiding a page only takes it off the touch screen; the panel still subscribes to every entity, and its web server can flip the switches.

### How to add a thermostat or number tile
`widgets/stepper/` is a tile with `-` and `+` either side of a value. Taps change the value on screen straight away, and one call goes to Home Assistant a second after the last tap, so a run of taps is one change rather than one each. A few seconds later the tile takes Home Assistant's value back, in case it clamped or refused it.

* `stepper/climate/` sets a thermostat's target temperature and shows the room temperature beside its icon. The icon follows what the system is doing: a flame while heating, a snowflake while cooling, a fan while only the fan runs. The range comes from the thermostat; the `step` is a var (1 for Fahrenheit, 0.5 for Celsius is typical). A thermostat that is off, or in heat/cool with a high/low pair, has no single target, so the tile shows `--` and the buttons do nothing.
* `stepper/number/` sets a `number` or `input_number`, with its range and step from the entity. `domain` is `number` or `input_number`; `unit` is shown after the value.

As with the other tiles, include the widget on the page and its sensors package with the same `uid`. `layouts/<WxH>/pages/climate.yaml` has one of each, with placeholder entities; uncomment its line in your top-level config (or in `all.yaml`) and point it at your own. The tile's sizing is in `layouts/<WxH>/vars/stepper.yaml`. A name too long for the space left of the buttons ends in `...`, which on the 240px-wide `320x240` canvas starts at about six characters.

### Putting a low-memory panel on a diet
The 2.8" CYD boards have 180KB of RAM, no PSRAM and 4MB of flash. Nothing warns at build time: a config that is too big compiles, flashes, and then crashes during boot, so the screen stays dark. It shows up on the serial log as `failed to create task`, `Failed to allocate` from LVGL, or an `abort()` while the Home Assistant sensors set up.

To see how much room a panel has, add `features/diagnostics/memory.yaml` and watch **Heap Min Free**. Every failure below happened during setup, so a panel that boots and stays up for an hour has enough.

What it took to fit a full personal layout on an ESP32-2432S028 (ILI9342): eight pages, two printers, the sleep clock and idle.

| Build | Static RAM | HA subscriptions | Result |
| --- | --- | --- | --- |
| everything, twelve AMS slots per printer, 25% LVGL buffer | 48% | 104 | crash-loops in setup |
| no printers page, no sleep clock | 41% | 64 | boots; 95KB free, 86KB at the lowest |
| + printers with twelve slots | | | LVGL runs out of memory, watchdog reboot |
| printers with only the real AMS units, + sleep clock | 46% | 104 | aborts growing the HA subscription list |
| same, `buffer_size: 12%` | 46% | 104 | boots; 73KB free, 63KB at the lowest, 46ms loop |

Ways to put a panel on a diet, biggest measured effect first:

1. **A smaller LVGL draw buffer.** `lvgl: buffer_size: 12%` in the top-level config, instead of the device file's 25%. The buffer is one 38KB block taken early in setup; halving it left room for everything that allocates after. Full-screen redraws, like a page change, get a little slower.
2. **List the AMS units a printer has**, as `320x240`'s `printers-on-a-diet` page does, rather than `tile.yaml`'s twelve slots. Each slot is a row of widgets plus tray, humidity and drying sensors.
3. **Use the combined tile** (`tile_combined.yaml`) for a printer with one AMS. It is one line, and subscribes only to that unit's trays.
4. **Count the Home Assistant sensors.** Each is a subscription and a sensor object, and attribute sensors (a light's brightness, a tray's colour and amount) add up.
5. **Include only the pages a panel needs**, since each is one line in the top-level config.
6. **Leave features off** that a panel can do without, and diagnostics except while chasing a problem.

Not measured, so no promises: `minimum_chip_revision: "3.1"` under `esp32: framework: advanced:` makes a smaller binary on rev 3 chips, which ESPHome suggests at boot on the newer CYDs; turning off `web_server`; a quieter `logger`.

### How to dim and sleep the panel when it is idle
Add `features/idle/idle.yaml` to the top-level config, and for a brightness ceiling, `features/idle/sun.yaml` or `features/idle/ambient_light.yaml`. See "Optional Features". The timeouts and the dim level are Home Assistant controls whose defaults you can set in YAML.

### How to show every AMS unit
The demo tiles use `tile_combined.yaml`, which puts the printer name and one AMS unit's four trays on a single line. To show all of a printer's units instead, switch that tile's body to `tile.yaml` in `layouts/<WxH>/pages/printers.yaml`:

```yaml
- obj: # printer 1
    <<: !include ../vars/printer_tile.yaml
    layout:
      <<: !include ../vars/printer_tile_layout.yaml
    widgets: !include { file: ../../widgets/printers/tile.yaml, vars: {
      uid: printer_1, name: 1 - Fred,
      <<: [!include ../vars/ams_row.yaml, !include ../vars/printer_bar.yaml] } }
```

and its sensor package, in the same file, to `printer.sensors.yaml`:

```yaml
printer_1_sensors: !include { file: ../../widgets/printers/printer.sensors.yaml, vars: {
  uid: printer_1,
  entity_id_prefix: p1s_1
}}
```

Change both halves together; a mismatched pair fails at config time on the ids the wrong half cannot find. The two formats can sit side by side on one page.

`tile.yaml` carries all twelve slots a printer can have (AMS units `1` to `4`, AMS HTs `128` and `129`), each hidden until its unit reports humidity. Every AMS reports humidity, so that doubles as "this unit is here". A slot whose entities do not exist never sends anything and stays hidden. A unit that stops reporting hides again, and moving an AMS to another printer needs no reflash. The heater icon is discovered the same way: a unit with no drying hardware has no `_drying` entity, so its icon stays blank.

The cost is the slots you do not use: on a Guition `JC3248W535`, going from 4 enumerated rows to 12 slots across two printers took RAM from 41.2% to 44.0% and flash from 18.7% to 19.3%. Empty slots are silent at boot, since Home Assistant sends nothing for an entity that does not exist.

## Testing tools
Most mistakes in these configs build and flash without complaint and only show on the panel: a blank icon, a tile stuck on its unknown state, a page that moved. The scripts in `tools/` catch them from a checkout. They need Python 3 and the `esphome` command, run from the repo root, and resolve configs with `esphome config`, so a non-SDL config needs its `secrets.yaml`.

**`tools/lint_configs.py [config.yaml ...]`** -- run it before flashing, and before opening a PR. With no arguments it checks every `*-example.yaml`. It fails on:
* an undefined substitution. A `${var}` nobody set is only a warning in ESPHome, and the widget gets the literal text, so a tile included without its `entity_id` subscribes to `${entity_id}` and never updates.
* an icon missing from the MDI font subset, which renders blank (below).
* once page access is in use, a page under `layouts/*/pages/` that doesn't include `widgets/page_access.yaml` exactly once for its own id, or a `skip: true` page that does.

**`tools/check_glyphs.py config.yaml ...`** -- the icon check on its own. The MDI fonts carry only the glyphs listed in `layouts/fonts/glyphs.yaml`, so an icon used but not listed renders blank with no build error, and one listed but never used is flash spent on nothing. It lists both. Unused glyphs are normal for a layout that shows only some pages. Run it when you add an icon.

**`tools/compare_configs.py [--base REF] [-v] [config.yaml ...]`** -- run it when you change a layout, a widget or a shared vars file. It resolves each config at `REF` (default `main`, checked out in a temporary git worktree) and in your working tree, and reports whether it still resolves, whether its pages are the same and in the same order, and how many resolved lines were added and removed. `-v` prints the lines. A refactor that is meant to change nothing should show `+0 -0`.

**`tools/snapshot.py config.yaml [-o DIR] [--all]`** -- run it to see a layout on a canvas you don't have a board for, or to make screenshots. It builds an SDL config (such as `sdl-example.yaml`) for the host with LVGL's snapshot support, shows each page in turn and writes `snapshots/<page>.png` with the header and footer on it. Nothing talks to Home Assistant, so tiles show their unknown state. `--all` includes `skip: true` pages. It needs SDL2, like the SDL example.

## Todo
This readme isn't finished. I'll be elaborating on some more techniques being used in here, such as the modularization of the widgets using `!include` and how the stateful widget files relate to their sensor counterparts (tip, just make sure to pass the same `uid` and `entity_id` when including a widget and when including the related widget sensor).

## Photos
These look better in real life, I promise! I took these photos in low-light and displays are not easy to photograph in general.

4.3" 480x800 portrait (Sunton ESP32-8048S043)  
![Lighting Page](media/sunton_4.3_lighting.jpg "Lighting Page")
![Printers Page](media/sunton_4.3_printers.jpg "Printers Page")

3.5" 320x480 portrait (Guition JC3248W535)  
![Lighting Page](media/guition_3.5_lighting.jpg "Lighting Page")
![Printers Page](media/guition_3.5_printers_ams.jpg "Printers Page")  

3.5" 320x480 portrait (Elecrow DIS05035H)  
![Lighting Page](media/elecrow_3.5_lighting.jpg "Lighting Page")
![Printers Page](media/elecrow_3.5_printers.jpg "Printers Page")

2.8" 240x320 portrait (Sunton ESP32-2432S028R)  
![Lighting Page](media/sunton_2.8_lighting.jpg "Lighting Page")
![Printers Page](media/sunton_2.8_printers.jpg "Printers Page")
