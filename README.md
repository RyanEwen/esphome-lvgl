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
* Add `features/`, for behaviour a panel may or may not want, opted into from the top-level config. Device files stay hardware only and layouts stay pages only. See "Optional features".
* Add `features/idle/`: dim, go home and sleep when idle, wake on touch, with the brightness ceiling following the sun or a light sensor.
* `devices/SDL.yaml` declares its touchscreen as a list with `id: main_touchscreen`, like the other device files, so features can extend it.
* The boot screen is dark rather than white, so a reboot at night does not light the room.
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

## Advanced YAML Techniques
Aside from the Packages feature used to separate device-specfic YAML from common YAML config, there are some other potentially unfamiliar techniques in use here. For example, the files within `layouts/` use [YAML anchors and aliases](https://ref.coddy.tech/yaml/yaml-anchors) which help reduce code duplication. I use anchors and aliases instead of `style_definitions` and `styles` as anchors can be used on anything instead of being restricted to just styles, and because they override `theme` settings when used (there is a bug or perhaps odd design choice that prevent `styles` from overriding `theme`). I define most of my anchors within a made-up section called `.sizing` because top-level sections prefixed with a period do not cause errors when parsed by ESPHome. 

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
### How to specify the home page on a particular device
To change which page loads at boot time and when the home button is pressed on a particular device, adjust the `home_page` variable in the device's config file to the ID of the desired page. 

For example, to set a page with the ID `printers`, adjust this in your device's config file:
```yaml
substitutions:
  ...
  home_page: printers
```

### How to hide pages on particular devices
To hide a page on a particular device, extend the desired `page` definition by adding `skip: true` using `!extend` (see [Packages](https://esphome.io/components/packages.html) feature). 

For example, to hide a page with the ID `bedroom`, add this to your device's config file:
```yaml
lvgl:
  pages:
    - id: !extend bedroom
      skip: true
```

### How to dim and sleep the panel when it is idle
A wall panel that stays at full brightness all night is its own night light. `lvgl:` has an `on_idle:` trigger, and every device file in this repo already gives its backlight the id `backlight`, so this works on any supported board. Add it to a device's config file — nothing in `devices/` or `layouts/` needs changing:

```yaml
touchscreen:
  - id: !extend main_touchscreen
    on_touch:
      - light.turn_on:
          id: backlight
          brightness: 100%

lvgl:
  on_idle:
    - timeout: 5min
      then:
        - light.turn_on:
            id: backlight
            brightness: 25%
    - timeout: 30min
      then:
        - light.turn_off: backlight
        - lvgl.pause:
            show_snow: false
```

Four things about this are worth knowing before you tune the numbers.

**The `on_touch` half is not optional.** `on_idle` dims the backlight but nothing puts it back: touching the screen resets the idle timer without restoring brightness, so without a wake handler the panel stays dim forever after the first five minutes.

**Waking from the 30-minute sleep is safe.** `lvgl.pause` respects the `resume_on_input` setting, which defaults to true, so the tap that wakes a paused display is consumed by the resume and never reaches the widget underneath it. You cannot turn a light on by accident while waking the panel. The dimmed state at five minutes is different — the screen is dark but completely live, and a tap there does what it lands on.

**`on_idle` lists merge across packages.** A device file and a layout can each contribute their own block; the entries concatenate and each keeps its own timeout. That is what lets UI-level idle behaviour live in the layout where its widget ids are, while the backlight stays in the device file where the hardware is.

**Do not hang a page change on the same timeout as `lvgl.pause`.** The order in which two entries with the same timeout run is not something you control, and a `lvgl.page.show` that lands after the pause is not worth betting on. If you want the panel to return to its home page while it is asleep, give that its own earlier timeout:

```yaml
    - timeout: 20min
      then:
        - script.execute: go_home
```

#### Going further: a brightness ceiling that follows the sun
Fixed percentages are fine until you use the panel at night. If Home Assistant is already the clock source, it can be the light meter too — `sun.sun` carries an `elevation` attribute, and these boards have no ambient light sensor to read instead:

```yaml
globals:
  - id: active_brightness   # the ceiling; the idle steps dim relative to it
    type: float
    restore_value: false
    initial_value: '1.0'
  - id: sun_band            # 2 day, 1 dusk, 0 night
    type: int
    restore_value: false
    initial_value: '2'

sensor:
  - id: sun_elevation
    platform: homeassistant
    entity_id: sun.sun
    attribute: elevation
    on_value:
      - lambda: |-
          if (std::isnan(x)) {
            return;
          }
          int band = x > 6.0f ? 2 : (x > -6.0f ? 1 : 0);
          if (band == id(sun_band)) {
            return;
          }
          id(sun_band) = band;
          id(active_brightness) = band == 2 ? 1.0f : (band == 1 ? 0.6f : 0.35f);
          // only follow the change if the panel is awake; if it is idling or
          // dark, the next touch picks the new ceiling up
          if (id(backlight).current_values.is_on() &&
              id(backlight).current_values.get_brightness() > id(active_brightness) * 0.5f) {
            auto call = id(backlight).turn_on();
            call.set_brightness(id(active_brightness));
            call.perform();
          }
```

Then use the global in place of the fixed percentages — `brightness: !lambda return id(active_brightness);` on touch, and `!lambda return id(active_brightness) * 0.25f;` at the five-minute step.

Bands rather than a continuous curve, because a brightness call on every elevation update would be a write every minute or two for a change nobody can see. The `isnan` guard matters: an unavailable sensor publishes NAN, and comparing NAN puts you in the night band at noon.

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
