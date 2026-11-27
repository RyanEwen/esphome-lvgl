"""MJPEG-over-HTTP stream rendered directly to a display window.

Designed for go2rtc's /api/stream.mjpeg endpoint (multipart/x-mixed-replace with a
Content-Length header per part). Frames are received on core 0, decoded with JPEGDEC
(S3 SIMD) on core 1, and pushed to the display from the main loop so it can coexist
with LVGL drawing other parts of the screen. Only the newest frame is ever kept, so
latency cannot accumulate when the device falls behind.

Uses the JPEGDEC build ESPHome's runtime_image adds (esphome-libs), so the two never disagree.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import display
from esphome.components.esp32 import add_idf_component, include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import (
    CONF_DISPLAY_ID,
    CONF_HEIGHT,
    CONF_ID,
    CONF_TRIGGER_ID,
    CONF_URL,
    CONF_WIDTH,
    CONF_X,
    CONF_Y,
)

DEPENDENCIES = ["esp32", "display", "network"]
MULTI_CONF = True

CONF_AUTOSTART = "autostart"
CONF_STATS_INTERVAL = "stats_interval"
CONF_MAX_FRAME_SIZE = "max_frame_size"
CONF_MAX_WIDTH = "max_width"
CONF_MAX_HEIGHT = "max_height"
CONF_ON_FIRST_FRAME = "on_first_frame"
CONF_BIG_ENDIAN = "big_endian"
CONF_RELEASE_WHEN_STOPPED = "release_when_stopped"
CONF_PUSH_ROWS = "push_rows"
CONF_INIT_DELAY = "init_delay"

mjpeg_stream_ns = cg.esphome_ns.namespace("mjpeg_stream")
MjpegStream = mjpeg_stream_ns.class_("MjpegStream", cg.Component)
FirstFrameTrigger = mjpeg_stream_ns.class_("FirstFrameTrigger", automation.Trigger.template())

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MjpegStream),
        cv.GenerateID(CONF_DISPLAY_ID): cv.use_id(display.Display),
        cv.Optional(CONF_URL, default=""): cv.string,
        cv.Optional(CONF_X, default=0): cv.int_range(min=0, max=2048),
        cv.Optional(CONF_Y, default=0): cv.int_range(min=0, max=2048),
        cv.Required(CONF_WIDTH): cv.int_range(min=8, max=2048),
        cv.Required(CONF_HEIGHT): cv.int_range(min=8, max=2048),
        cv.Optional(CONF_AUTOSTART, default=True): cv.boolean,
        cv.Optional(CONF_ON_FIRST_FRAME): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FirstFrameTrigger)}
        ),
        cv.Optional(CONF_INIT_DELAY, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_BIG_ENDIAN, default=True): cv.boolean,
        # free the frame buffers while stopped (they come back at the next start)
        cv.Optional(CONF_RELEASE_WHEN_STOPPED, default=False): cv.boolean,
        cv.Optional(CONF_PUSH_ROWS, default=16): cv.int_range(min=0, max=480),
        cv.Optional(CONF_MAX_WIDTH, default=0): cv.int_range(min=0, max=2048),
        cv.Optional(CONF_MAX_HEIGHT, default=0): cv.int_range(min=0, max=2048),
        cv.Optional(
            CONF_STATS_INTERVAL, default="5s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MAX_FRAME_SIZE, default=65536): cv.int_range(
            min=4096, max=1048576
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config):
    # Frames are drawn in the panel's native coordinates. Since 2026.11, LVGL hands its rotation
    # to drivers that can rotate (mipi_rgb: the Sunton RGB boards), and the driver would then
    # turn these frames too. Marking the display as not rotating keeps LVGL on software
    # rotation, as before 2026.11, so the driver stays at 0.
    from dataclasses import replace

    from esphome.components.display import _get_metadata_list

    entries = _get_metadata_list()
    disp_id = config[CONF_DISPLAY_ID]
    for i, (id_, meta) in enumerate(entries):
        if id_.id == disp_id.id and meta.has_hardware_rotation:
            entries[i] = (id_, replace(meta, has_hardware_rotation=False))
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    include_builtin_idf_component("esp_http_client")
    # Same library, version and esp-dsp as esphome/components/runtime_image (JPEG format).
    cg.add_library(
        "JPEGDEC",
        "1.8.4",
        "https://github.com/esphome-libs/JPEGDEC#1.8.4-esphome.4",
    )
    add_idf_component(name="espressif/esp-dsp", ref="1.8.2")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    disp = await cg.get_variable(config[CONF_DISPLAY_ID])
    cg.add(var.set_display(disp))
    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_window(config[CONF_X], config[CONF_Y], config[CONF_WIDTH], config[CONF_HEIGHT]))
    cg.add(var.set_autostart(config[CONF_AUTOSTART]))
    cg.add(var.set_stats_interval(config[CONF_STATS_INTERVAL].total_milliseconds))
    cg.add(var.set_max_frame_size(config[CONF_MAX_FRAME_SIZE]))
    cg.add(var.set_init_delay(config[CONF_INIT_DELAY].total_milliseconds))
    cg.add(var.set_big_endian(config[CONF_BIG_ENDIAN]))
    cg.add(var.set_release_when_stopped(config[CONF_RELEASE_WHEN_STOPPED]))
    cg.add(var.set_push_rows(config[CONF_PUSH_ROWS]))
    cg.add(var.set_max_window(config[CONF_MAX_WIDTH], config[CONF_MAX_HEIGHT]))

    for conf in config.get(CONF_ON_FIRST_FRAME, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
