// Tray pill colours, shared by every tray in every AMS row.
//
// This exists because ESPHome YAML has no way to share a lambda: the same
// twenty lines were pasted once per tray, ten times over, and a fix to one
// copy is a fix to one copy.
//
// Returns packed 0xRRGGBB rather than lv_color_t so the header needs nothing
// from LVGL; the caller wraps it in lv_color_hex().
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace tray {

static const uint32_t TILE_BG = 0x42495A;  // "no data" -- the tile's own background
static const uint32_t INK_LIGHT = 0xCCCCCC;
static const uint32_t INK_DARK = 0x111111;

// Home Assistant sends "unavailable" or "None" when an entity has no value, and
// this firmware is built -fno-exceptions, so std::stoi on either would abort and
// reboot the panel. strtol cannot throw; the shape check rejects the rest.
// #RRGGBB and #RRGGBBAA are both accepted -- an empty tray reports the latter.
inline bool parse(const std::string &s, long &rgb) {
  if ((s.size() != 7 && s.size() != 9) || s[0] != '#') {
    return false;
  }
  std::string hex = s.substr(1, 6);  // named: strtol's end pointer must not dangle
  char *end = nullptr;
  rgb = strtol(hex.c_str(), &end, 16);
  return end != nullptr && *end == '\0';
}

inline uint32_t bg(const std::string &s) {
  long rgb;
  return parse(s, rgb) ? (uint32_t) rgb : TILE_BG;
}

// Ink picked for contrast against the filament, by relative luminance with the
// sRGB gamma curve -- a weighted average of the raw bytes misjudges the middle
// of the range, putting light text on Bambu's green at 1.8:1 where dark ink
// gives 6.4:1. 0.179 is where the two contrast ratios cross over.
inline uint32_t ink(const std::string &s) {
  long rgb;
  if (!parse(s, rgb)) {
    return INK_LIGHT;
  }
  auto lin = [](int v) {
    float c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
  };
  float luminance = 0.2126f * lin((rgb >> 16) & 0xFF) + 0.7152f * lin((rgb >> 8) & 0xFF) +
                    0.0722f * lin(rgb & 0xFF);
  return luminance > 0.179f ? INK_DARK : INK_LIGHT;
}

}  // namespace tray
