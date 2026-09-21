# ESPHome + LVGL on cheap touchscreen devices

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/S6S21K2LK2)

## Supported Devices
* Guition `JC3248W535` 3.5" 320x480 portrait, with capacitive touch and USB-C. [AliExpress Link](https://www.aliexpress.com/item/1005007566046827.html).
* Sunton `ESP32-2432S028R` 2.8" 240x320 portrait, with resistive touch and USB micro-B. [AliExpress Link](https://www.aliexpress.com/item/1005004502250619.html).
* Sunton `ESP32-8048S043` 4.3" 480x800 portrait, with capactivive touch and USB-C. [AliExpress Link](https://www.aliexpress.com/item/1005004788147691.html).
* Sunton `ESP32-8048S050` 5.0" 480x800 portrait, with capactivive touch and USB-C. [AliExpress Link](https://www.aliexpress.com/item/1005004952694042.html).
* Elecrow CrowPanel `DIS05035H` (v2.2) 3.5" 320x480 portrait, with resistive touch and USB-C. [Manufacturer's Link](https://www.elecrow.com/esp32-display-3-5-inch-hmi-display-spi-tft-lcd-touch-screen.html).

## Changelog
### 2026-09-21
* [Breaking change] Each page is now its own file, `layouts/<WxH>/pages/<page>.yaml`, holding the page and the sensors its tiles need, and the top-level config lists the pages it wants in navigation order. A config that includes only `layout:` now gets no pages: copy the page lines from the matching `*-example.yaml`, or include `layouts/<WxH>/all.yaml` for every page. The sizing the pages share moved from the layout's `.sizing` anchors to `layouts/<WxH>/vars/`. See "How to choose which pages a device shows".
* Light and light-group tiles no longer set the light to 1% on a long press; a hold was too easy to hit by accident on a wall panel, and on a group tile it dimmed every light in the group. To keep it on a tile, include `dim_on_hold.yaml` instead of `widget.yaml` from the same directory (`light_buttons/` or `light_group_buttons/`); the vars and the sensors package are unchanged.
* Add `features/`, for behaviour a panel may or may not want, opted into from the top-level config. Device files stay hardware only and layouts stay pages only. See "Optional features".
* Add `features/idle/`: dim, go home and sleep when idle, wake on touch, with the brightness ceiling following the sun or a light sensor.
* `devices/SDL.yaml` declares its touchscreen as a list with `id: main_touchscreen`, like the other device files, so features can extend it.
* The boot screen is dark rather than white, so a reboot at night does not light the room.
* `480x320` panels have a **Home page** select and a **Show \<page\> page** switch per page in Home Assistant. By default every page shows and home is `home_page`, so nothing changes until you use them. A page hidden with `skip: true` in YAML stays hidden. See "How to run one image on several panels".
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

On `480x320` panels this is the default for the **Home page** select in Home Assistant, which changes it on a running panel.

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

On `480x320` panels each page also has a **Show \<page\> page** switch in Home Assistant, which hides and shows it on a running panel; the home page can't be hidden, and a page given `skip: true` stays hidden whatever its switch says. To have a page start hidden but leave Home Assistant able to show it, default its switch to off instead:
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

### How to dim and sleep the panel when it is idle
Add `features/idle/idle.yaml` to the top-level config, and for a brightness ceiling, `features/idle/sun.yaml` or `features/idle/ambient_light.yaml`. See "Optional Features". The timeouts and the dim level are Home Assistant controls whose defaults you can set in YAML.

## Todo
This readme isn't finished. I'll be elaborating on some more techniques being used in here, such as the modularization of the widgets using `!include` and how the stateful widget files relate to their sensor counterparts (tip, just make sure to pass the same `uid` and `entity_id` when including a widget and when including the related widget sensor).

## Photos
These look better in real life, I promise! I took these photos in low-light and displays are not easy to photograph in general.

4.3" 480x800 portrait (Sunton ESP32-8048S043)  
![Lighting Page](media/sunton_4.3_lighting.jpg "Lighting Page")
![Printers Page](media/sunton_4.3_printers.jpg "Printers Page")

3.5" 320x480 portrait (Guition JC3248W535)  
![Lighting Page](media/guition_3.5_lighting.jpg "Lighting Page")
![Printers Page](media/guition_3.5_printers.jpg "Printers Page")  

3.5" 320x480 portrait (Elecrow DIS05035H)  
![Lighting Page](media/elecrow_3.5_lighting.jpg "Lighting Page")
![Printers Page](media/elecrow_3.5_printers.jpg "Printers Page")

2.8" 240x320 portrait (Sunton ESP32-2432S028R)  
![Lighting Page](media/sunton_2.8_lighting.jpg "Lighting Page")
![Printers Page](media/sunton_2.8_printers.jpg "Printers Page")
