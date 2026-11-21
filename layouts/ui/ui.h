#pragma once
// The panel layout's C++ helpers: weather icons and range bars, the agenda renderer and its
// calendar settings, the photo crossfade.
//
// Both pages get their data from Home Assistant action responses (weather.get_forecasts,
// calendar.get_events) whose response_template renders compact text, so this file only
// splits lines and builds LVGL objects; all date maths happens in Home Assistant.
//
// Agenda text, one record per line, fields separated by '|':
//   C|<entity id>|<name>                                      a calendar, sorted by name
//   D|<day offset>|<WEEKDAY>|<day of month>|<MON>|<week key>   a day, today = 0; the week
//                                                              key (its Sunday, YYYYMMDD) starts a new week
//   E|<day offset>|<calendar index>|<time range>|<end, unix time>|<title>   an event on that day
// C lines list every calendar the panel may show (switched off or not); an event's
// calendar index is its position there, which picks its colour. Every day in the range
// gets a D line (days without events render compactly); E lines follow their day.
//
// Visual design follows the agenda card from espcontrol PR #1246 (written for that
// project by the same author): date column, today dot, events tinted with their
// calendar's colour and an accent bar, week separators.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "esphome/core/helpers.h"
#include "lvgl.h"

namespace ui {

// ---------------------------------------------------------------------------- weather

// Material Design Icons glyph (UTF-8) for a Home Assistant weather condition.
inline const char *weather_icon(const std::string &c) {
  if (c == "sunny") return "\U000F0599";
  if (c == "clear-night") return "\U000F0594";
  if (c == "partlycloudy") return "\U000F0595";
  if (c == "cloudy") return "\U000F0590";
  if (c == "fog") return "\U000F0591";
  if (c == "hail") return "\U000F0592";
  if (c == "lightning") return "\U000F0593";
  if (c == "lightning-rainy") return "\U000F067E";
  if (c == "pouring") return "\U000F0596";
  if (c == "rainy") return "\U000F0597";
  if (c == "snowy") return "\U000F0598";
  if (c == "snowy-rainy") return "\U000F067F";
  if (c == "windy") return "\U000F059D";
  if (c == "windy-variant") return "\U000F059E";
  return "\U000F05D6";  // exceptional / unknown
}

// Icon colour for a Home Assistant weather condition.
inline lv_color_t weather_color(const std::string &c) {
  if (c == "sunny") return lv_color_hex(0xFFC107);              // amber
  if (c == "clear-night") return lv_color_hex(0xC5CAE9);        // pale moonlight
  if (c == "partlycloudy") return lv_color_hex(0xFFD54F);       // soft amber
  if (c == "cloudy") return lv_color_hex(0xB0BEC5);             // grey
  if (c == "fog") return lv_color_hex(0x90A4AE);                // darker grey
  if (c == "rainy" || c == "pouring") return lv_color_hex(0x4FC3F7);  // blue
  if (c == "snowy") return lv_color_hex(0xE1F5FE);              // icy white
  if (c == "snowy-rainy" || c == "hail") return lv_color_hex(0x81D4FA);  // icy blue
  if (c == "lightning" || c == "lightning-rainy") return lv_color_hex(0xFFEB3B);  // yellow
  if (c == "windy" || c == "windy-variant") return lv_color_hex(0x80CBC4);  // teal
  return lv_color_hex(0xFF7043);                                // exceptional / unknown
}

inline std::string weather_label(const std::string &c) {
  if (c == "sunny") return "Sunny";
  if (c == "clear-night") return "Clear";
  if (c == "partlycloudy") return "Partly cloudy";
  if (c == "cloudy") return "Cloudy";
  if (c == "fog") return "Fog";
  if (c == "hail") return "Hail";
  if (c == "lightning") return "Thunderstorms";
  if (c == "lightning-rainy") return "Thunderstorms";
  if (c == "pouring") return "Heavy rain";
  if (c == "rainy") return "Rain";
  if (c == "snowy") return "Snow";
  if (c == "snowy-rainy") return "Sleet";
  if (c == "windy" || c == "windy-variant") return "Windy";
  if (c.empty() || c == "unavailable" || c == "unknown") return "--";
  return c;
}

// ---------------------------------------------------------------------------- parsing

inline std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t pos = s.find(sep, start);
    out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
    if (pos == std::string::npos)
      break;
    start = pos + 1;
  }
  return out;
}

// ---------------------------------------------------------------------------- forecast

// One forecast row's widgets on the Weather page.
struct ForecastRow {
  lv_obj_t *row, *day, *icon, *drop, *rain, *lo, *track, *fill, *hi;
};

// Colour for a temperature in °C, interpolated between stops (cold blue -> warm red).
inline lv_color_t temperature_color(float t) {
  static const float temps[] = {-15, 0, 10, 18, 26, 34};
  static const uint32_t colors[] = {0x3F51B5, 0x42A5F5, 0x26A69A, 0xFFCA28, 0xFF7043, 0xE53935};
  const int n = sizeof(temps) / sizeof(temps[0]);
  if (t <= temps[0])
    return lv_color_hex(colors[0]);
  for (int i = 1; i < n; i++) {
    if (t <= temps[i]) {
      const float f = (t - temps[i - 1]) / (temps[i] - temps[i - 1]);
      return lv_color_mix(lv_color_hex(colors[i]), lv_color_hex(colors[i - 1]), (uint8_t) (f * 255));
    }
  }
  return lv_color_hex(colors[n - 1]);
}

// Fills up to `count` rows from the forecast text (one line per day:
// label|condition|high|low|precip %), hides unused rows, and puts today's high/low into
// `today_hilo`.
//
// Each row's range bar spans that day's low..high on a scale shared by every row shown:
// the lowest low across the days is the left end of the track, the highest high the
// right end. Home Assistant drops today's high once the day is over; `now_temp` (the
// current temperature, NAN if unknown) then stands in for it.
// The forecast's shared temperature scale, kept so the "now" marker can move between fetches.
struct ForecastScale {
  float min_t = NAN, span = 1.0f;
  float today_lo = NAN, today_hi = NAN;  // today's range, for the screensaver's copy of the row
};
inline ForecastScale &forecast_scale() {
  static ForecastScale s;
  return s;
}

// Places the dot marking the current temperature on today's bar, kept inside the bar's
// filled range clear of its rounded ends. The dot is a sibling of the track (`fill` is
// the track's child), so it is placed by screen position rather than as a child.
inline void place_now_marker(lv_obj_t *marker, lv_obj_t *track, lv_obj_t *fill, float now_temp) {
  if (marker == nullptr)
    return;
  const ForecastScale &sc = forecast_scale();
  if (std::isnan(now_temp) || std::isnan(sc.min_t)) {
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // settle the layout first: text set just before (the temperatures beside the bar) can
  // move the track, and the dot is placed against where the track is now
  lv_obj_update_layout(track);
  const int32_t track_w = lv_obj_get_style_width(track, LV_PART_MAIN);
  const int32_t size = lv_obj_get_style_width(marker, LV_PART_MAIN);
  const float f = std::max(0.0f, std::min(1.0f, (now_temp - sc.min_t) / sc.span));
  const int32_t track_h = lv_obj_get_style_height(track, LV_PART_MAIN);
  const int32_t fx = lv_obj_get_style_x(fill, LV_PART_MAIN);
  const int32_t fw = lv_obj_get_style_width(fill, LV_PART_MAIN);
  const int32_t lo = fx + track_h / 2, hi = std::max(lo, fx + fw - track_h / 2);
  const int32_t cx = std::max(lo, std::min(hi, (int32_t) lroundf(f * track_w)));
  lv_obj_align_to(marker, track, LV_ALIGN_LEFT_MID, cx - size / 2, 0);
  lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
}

// Draws a day's lo..hi range as the coloured fill inside its track, on the shared scale.
inline void set_range_fill(lv_obj_t *track, lv_obj_t *fill, float lo, float hi) {
  const ForecastScale &sc = forecast_scale();
  if (std::isnan(lo) || std::isnan(sc.min_t)) {
    lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const int32_t track_w = lv_obj_get_style_width(track, LV_PART_MAIN);
  const int32_t track_h = lv_obj_get_style_height(track, LV_PART_MAIN);
  int32_t x0 = (int32_t) lroundf((lo - sc.min_t) / sc.span * track_w);
  int32_t x1 = (int32_t) lroundf((hi - sc.min_t) / sc.span * track_w);
  if (x1 - x0 < track_h) {  // never thinner than a dot
    x1 = std::min(track_w, x0 + track_h);
    x0 = x1 - track_h;
  }
  lv_obj_set_pos(fill, x0, 0);
  lv_obj_set_width(fill, x1 - x0);
  lv_obj_set_style_bg_color(fill, temperature_color(lo), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(fill, temperature_color(hi), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_HIDDEN);
}

// Called after each forecast (set by the screensaver to redraw its Today row).
inline std::function<void()> &forecast_updated() {
  static std::function<void()> f;
  return f;
}

inline void render_forecast(const std::string &text, ForecastRow *rows, int count, lv_obj_t *today_hilo,
                            float now_temp = NAN, lv_obj_t *now_marker = nullptr) {
  struct Day {
    std::vector<std::string> p;
    float lo, hi;
  };
  std::vector<Day> days;
  for (const std::string &ln : split(text, '\n')) {
    std::vector<std::string> p = split(ln, '|');
    if (p.size() < 5 || (int) days.size() >= count)
      continue;
    float hi = p[2].empty() ? NAN : (float) atof(p[2].c_str());
    float lo = p[3].empty() ? NAN : (float) atof(p[3].c_str());
    if (days.empty() && std::isnan(hi) && !std::isnan(now_temp))
      hi = std::isnan(lo) ? now_temp : std::max(lo, now_temp);
    if (std::isnan(lo))
      lo = hi;
    if (std::isnan(hi))
      hi = lo;
    days.push_back({p, lo, hi});
  }
  float min_t = NAN, max_t = NAN;
  for (const Day &d : days) {
    if (std::isnan(d.lo))
      continue;
    min_t = std::isnan(min_t) ? d.lo : std::min(min_t, d.lo);
    max_t = std::isnan(max_t) ? d.hi : std::max(max_t, d.hi);
  }
  const float span = (!std::isnan(min_t) && max_t > min_t) ? max_t - min_t : 1.0f;
  forecast_scale() = {min_t, span, days.empty() ? NAN : days[0].lo, days.empty() ? NAN : days[0].hi};

  int n = 0;
  for (const Day &d : days) {
    const ForecastRow &r = rows[n];
    const auto &p = d.p;
    lv_label_set_text(r.day, p[0].c_str());
    lv_label_set_text(r.icon, weather_icon(p[1]));
    lv_obj_set_style_text_color(r.icon, weather_color(p[1]), LV_PART_MAIN);
    const bool wet = !p[4].empty() && p[4] != "0";
    lv_label_set_text(r.rain, wet ? (p[4] + "%").c_str() : "");
    if (wet)
      lv_obj_remove_flag(r.drop, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(r.drop, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(r.lo, std::isnan(d.lo) ? "--" : (std::to_string((int) lroundf(d.lo)) + "°").c_str());
    lv_label_set_text(r.hi, std::isnan(d.hi) ? "--" : (std::to_string((int) lroundf(d.hi)) + "°").c_str());

    set_range_fill(r.track, r.fill, d.lo, d.hi);

    if (n == 0 && today_hilo != nullptr) {
      std::string hl = p[2].empty() ? std::string() : "High " + p[2] + "°   ";
      if (!p[3].empty())
        hl += "Low " + p[3] + "°";
      lv_label_set_text(today_hilo, hl.c_str());
    }
    lv_obj_remove_flag(r.row, LV_OBJ_FLAG_HIDDEN);
    n++;
  }
  for (int i = n; i < count; i++)
    lv_obj_add_flag(rows[i].row, LV_OBJ_FLAG_HIDDEN);
  if (count > 0)
    place_now_marker(now_marker, rows[0].track, rows[0].fill, n > 0 ? now_temp : NAN);
  if (forecast_updated())
    forecast_updated()();
}

// ---------------------------------------------------------------------------- agenda

struct AgendaFonts {
  const lv_font_t *weekday;  // small caps line: "FRI", "NOV"
  const lv_font_t *day_num;  // big day of month
  const lv_font_t *title;    // event title
  const lv_font_t *detail;   // time range, countdown, "No events"
  const lv_font_t *icon;     // MDI clock
  int32_t date_w = 62;       // width of the date column
};

// Set once at boot (main.yaml), shared by the agenda, its settings page and overlays.
inline AgendaFonts &agenda_fonts() {
  static AgendaFonts fonts{};
  return fonts;
}

// The calendars the panel may show, as (entity id, name), sorted by name; filled from
// the C lines of each agenda reply.
inline std::vector<std::pair<std::string, std::string>> &agenda_calendars() {
  static std::vector<std::pair<std::string, std::string>> cals;
  return cals;
}

// Calendars switched off on this panel: fnv1 hashes of their entity ids, 0 = free slot.
// Fixed size so it can live in a restorable ESPHome global.
using HiddenCalendars = std::array<uint32_t, 32>;

inline uint32_t calendar_hash(const std::string &entity) {
  return esphome::fnv1_hash(entity) | 1u;  // never 0, which marks a free slot
}

inline bool calendar_hidden(const HiddenCalendars &hidden, const std::string &entity) {
  const uint32_t h = calendar_hash(entity);
  for (uint32_t x : hidden)
    if (x == h)
      return true;
  return false;
}

inline void set_calendar_hidden(HiddenCalendars &hidden, const std::string &entity, bool hide) {
  const uint32_t h = calendar_hash(entity);
  for (uint32_t &x : hidden)
    if (x == h) {
      if (!hide)
        x = 0;
      return;
    }
  if (hide)
    for (uint32_t &x : hidden)
      if (x == 0) {
        x = h;
        return;
      }
}

// The calendars to fetch, comma-separated: every known calendar that isn't switched off.
// Empty before the first reply (Home Assistant then uses every eligible calendar) and
// when all are switched off (the caller checks agenda_calendars() to tell those apart).
inline std::string visible_calendars(const HiddenCalendars &hidden) {
  std::string out;
  for (const auto &c : agenda_calendars()) {
    if (calendar_hidden(hidden, c.first))
      continue;
    if (!out.empty())
      out += ",";
    out += c.first;
  }
  return out;
}

// Accent colours by position in calendars: green, magenta, blue, red, amber, cyan; more
// calendars repeat.
static const uint32_t AGENDA_COLORS[] = {0x66BB6A, 0xE040FB, 0x42A5F5, 0xEF5350, 0xFFB300, 0x26C6DA};
static const int AGENDA_COLOR_COUNT = sizeof(AGENDA_COLORS) / sizeof(AGENDA_COLORS[0]);
static const uint32_t AGENDA_TODAY = 0x4FC3F7;

// A bare container: no theme background, border, padding or scrolling.
inline lv_obj_t *agenda_box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);  // let swipes reach the scrolling list
  return o;
}

inline lv_obj_t *agenda_label(lv_obj_t *parent, const std::string &text, const lv_font_t *font,
                              lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text.c_str());
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_text_opa(l, opa, 0);
  return l;
}

inline lv_obj_t *agenda_column(lv_obj_t *parent, int32_t pad_row) {
  lv_obj_t *c = agenda_box(parent);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, pad_row, 0);
  return c;
}

// Whether clocks show 24-hour time (the header's 24-hour switch); set before rendering.
inline bool &clock_24h() {
  static bool h24 = false;
  return h24;
}

// The agenda sends times as 24-hour "HH:MM"; shows them as "6:10 PM" unless clock_24h().
inline std::string agenda_times(const std::string &range) {
  if (clock_24h())
    return range;
  std::string out;
  for (size_t i = 0; i < range.size();) {
    if (i + 5 <= range.size() && isdigit(range[i]) && isdigit(range[i + 1]) && range[i + 2] == ':' &&
        isdigit(range[i + 3]) && isdigit(range[i + 4])) {
      const int h = (range[i] - '0') * 10 + (range[i + 1] - '0');
      out += std::to_string((h + 11) % 12 + 1) + range.substr(i + 2, 3) + (h < 12 ? " AM" : " PM");
      i += 5;
    } else {
      out += range[i++];
    }
  }
  return out;
}

// One event: tinted box, accent bar, title, then clock + time range + countdown.
inline void agenda_event(lv_obj_t *col, int day, int cal, const std::string &range,
                         const std::string &title, const AgendaFonts &f) {
  const uint32_t color = AGENDA_COLORS[(cal < 0 ? 0 : cal) % AGENDA_COLOR_COUNT];
  lv_obj_t *box = agenda_column(col, 2);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_20, 0);
  lv_obj_set_style_radius(box, 6, 0);
  lv_obj_set_style_pad_top(box, 9, 0);
  lv_obj_set_style_pad_bottom(box, 9, 0);
  lv_obj_set_style_pad_left(box, 14, 0);
  lv_obj_set_style_pad_right(box, 10, 0);

  lv_obj_t *bar = agenda_box(box);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(bar, 4, lv_pct(100));
  lv_obj_align(bar, LV_ALIGN_LEFT_MID, -10, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bar, 2, 0);

  lv_obj_t *t = agenda_label(box, title, f.title);
  lv_obj_set_width(t, lv_pct(100));
  lv_obj_set_height(t, lv_font_get_line_height(f.title));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);

  lv_obj_t *line = agenda_box(box);
  lv_obj_set_width(line, lv_pct(100));
  lv_obj_set_height(line, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(line, 6, 0);
  agenda_label(line, "\U000F0150", f.icon, LV_OPA_40);  // clock-outline
  lv_obj_t *r = agenda_label(line, agenda_times(range), f.detail, LV_OPA_60);
  lv_obj_set_flex_grow(r, 1);
  if (day >= 2)
    agenda_label(line, "in " + std::to_string(day) + " days", f.detail, LV_OPA_50);
}

// The last agenda reply, kept so the screensaver can draw its own copy without a fetch.
inline std::string &agenda_text() {
  static std::string s;
  return s;
}

// Called after each agenda reply (set by the screensaver to redraw its copy).
inline std::function<void()> &agenda_updated() {
  static std::function<void()> f;
  return f;
}

// Rebuilds `list` (a scrollable flex column) from the agenda text.
// Compact (max_days > 0, for the screensaver): only the first max_days days, days without
// events left out, and at most max_events events (0 = no limit). With `now` (unix time), events
// that have ended are left out too.
inline void render_agenda(lv_obj_t *list, const std::string &text, const AgendaFonts &f, int max_days = 0,
                          int max_events = 0, time_t now = 0) {
  const bool compact = max_days > 0;
  lv_obj_clean(list);
  lv_obj_t *events = nullptr;  // the current day's event column
  bool day_has_events = false;
  std::vector<std::string> day_line;  // the D line of the day being read; its row is built
                                      // straight away, or (compact) with its first event
  std::string week;
  int shown = 0;
  int ended = 0;  // events left out because they were over
  auto close_day = [&]() {
    if (events != nullptr && !day_has_events) {
      lv_obj_t *none = agenda_label(events, "No events", f.detail, LV_OPA_40);
      lv_obj_set_style_pad_top(none, 2, 0);
    }
  };
  auto open_day = [&](const std::vector<std::string> &p) {
    const int day = atoi(p[1].c_str());
    if (!week.empty() && p[5] != week) {  // week separator
      lv_obj_t *sep = agenda_box(list);
      lv_obj_set_size(sep, lv_pct(100), 1);
      lv_obj_set_style_bg_color(sep, lv_color_hex(AGENDA_TODAY), 0);
      lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);
    }
    week = p[5];
    lv_obj_t *row = agenda_box(list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t *date = agenda_column(row, 0);
    lv_obj_set_size(date, f.date_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(date, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    agenda_label(date, p[2], f.weekday, LV_OPA_60);
    agenda_label(date, p[3], f.day_num);
    agenda_label(date, p[4], f.weekday, LV_OPA_60);
    if (day == 0) {  // today dot
      lv_obj_t *dot = agenda_box(date);
      lv_obj_add_flag(dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_size(dot, 8, 8);
      lv_obj_align(dot, LV_ALIGN_LEFT_MID, -2, 0);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(dot, lv_color_hex(AGENDA_TODAY), 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }

    events = agenda_column(row, 6);
    lv_obj_set_flex_grow(events, 1);
    lv_obj_set_height(events, LV_SIZE_CONTENT);
    day_has_events = false;
  };
  std::vector<std::pair<std::string, std::string>> calendars;
  for (const std::string &raw : split(text, '\n')) {
    std::string ln = raw;
    while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' '))
      ln.pop_back();
    if (ln.size() < 2)
      continue;
    std::vector<std::string> p = split(ln, '|');
    if (p[0] == "C" && p.size() >= 3) {
      calendars.emplace_back(p[1], p[2]);
      continue;
    }
    if (p[0] == "D" && p.size() >= 6) {
      if (!compact)
        close_day();
      events = nullptr;
      day_line = p;
      if (compact && atoi(p[1].c_str()) >= max_days)
        day_line.clear();  // past the window: its events are skipped
      else if (!compact)
        open_day(p);
    } else if (p[0] == "E" && p.size() >= 6 && !day_line.empty()) {
      if (max_events > 0 && shown >= max_events)
        continue;
      const long end = atol(p[4].c_str());  // when the event ends (unix time)
      if (now > 0 && end > 0 && end <= now) {
        ended++;  // over already
        continue;
      }
      if (events == nullptr)
        open_day(day_line);
      // a title may itself contain '|' only if HA didn't replace it; rejoin to be safe
      std::string title = p[5];
      for (size_t i = 6; i < p.size(); i++)
        title += "|" + p[i];
      agenda_event(events, atoi(p[1].c_str()), atoi(p[2].c_str()), p[3], title, f);
      day_has_events = true;
      shown++;
    }
  }
  if (!calendars.empty())
    agenda_calendars() = std::move(calendars);
  if (!compact)
    close_day();
  if (lv_obj_get_child_count(list) == 0) {
    lv_obj_t *none = agenda_label(list, compact ? (max_days == 1 ? (ended > 0 ? "No more events today" : "No events today") : "Nothing coming up") : "Agenda unavailable", f.detail, LV_OPA_60);
    if (compact) {
      lv_obj_set_width(none, lv_pct(100));
      lv_obj_set_style_text_align(none, LV_TEXT_ALIGN_CENTER, 0);
    }
  }
}

// Makes everything inside `obj` ignore taps, so they land on `obj` itself.
inline void clear_clickable(lv_obj_t *obj) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    lv_obj_t *c = lv_obj_get_child(obj, i);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    clear_clickable(c);
  }
}

// Shows `show` over `hide` (two photos stacked at the bottom of the same page), fading it
// in over `ms` (0 = at once). `hide` is hidden when the fade ends, so the next photo can be
// loaded into it unseen.
inline void photo_crossfade(lv_obj_t *show, lv_obj_t *hide, uint32_t ms) {
  lv_anim_delete(show, nullptr);
  lv_anim_delete(hide, nullptr);
  lv_obj_move_to_index(show, 1);  // above the other photo, below everything else
  lv_obj_remove_flag(show, LV_OBJ_FLAG_HIDDEN);
  if (ms == 0 || lv_obj_has_flag(hide, LV_OBJ_FLAG_HIDDEN)) {  // nothing to fade from
    lv_obj_set_style_opa(show, LV_OPA_COVER, 0);
    lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_set_style_opa(hide, LV_OPA_COVER, 0);
  lv_obj_set_style_opa(show, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, show);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *) o, (lv_opa_t) v, 0); });
  lv_anim_set_user_data(&a, hide);
  lv_anim_set_completed_cb(&a, [](lv_anim_t *a) {
    lv_obj_add_flag((lv_obj_t *) lv_anim_get_user_data(a), LV_OBJ_FLAG_HIDDEN);
  });
  lv_anim_start(&a);
}

// ---------------------------------------------------------------------------- agenda settings

// What the settings switches edit, and who to tell; set by render_agenda_settings.
struct AgendaSettingsState {
  HiddenCalendars *hidden = nullptr;
  std::function<void()> changed;
};

inline AgendaSettingsState &agenda_settings_state() {
  static AgendaSettingsState s;
  return s;
}

inline void agenda_setting_toggled(lv_event_t *e) {
  lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  AgendaSettingsState &st = agenda_settings_state();
  if (st.hidden == nullptr || i >= agenda_calendars().size())
    return;
  set_calendar_hidden(*st.hidden, agenda_calendars()[i].first, !lv_obj_has_state(sw, LV_STATE_CHECKED));
  if (st.changed)
    st.changed();
}

// Tapping anywhere on a row flips its switch.
inline void agenda_setting_row_clicked(lv_event_t *e) {
  lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    lv_obj_remove_state(sw, LV_STATE_CHECKED);
  else
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
}

// Rebuilds `list` with one row per calendar: colour dot, name, on/off switch. `keep`
// (e.g. a Done button that is a child of `list`) survives the rebuild and ends up last.
inline void render_agenda_settings(lv_obj_t *list, HiddenCalendars *hidden, std::function<void()> changed,
                                   lv_obj_t *keep = nullptr) {
  AgendaSettingsState &st = agenda_settings_state();
  st.hidden = hidden;
  st.changed = std::move(changed);
  const AgendaFonts &f = agenda_fonts();
  for (int32_t i = (int32_t) lv_obj_get_child_count(list) - 1; i >= 0; i--) {
    lv_obj_t *child = lv_obj_get_child(list, i);
    if (child != keep)
      lv_obj_delete(child);
  }
  const auto &cals = agenda_calendars();
  if (cals.empty()) {
    lv_obj_t *msg = agenda_label(list, "Loading calendars...", f.detail, LV_OPA_60);
    if (keep != nullptr)
      lv_obj_move_to_index(msg, 0);
    return;
  }
  for (size_t i = 0; i < cals.size(); i++) {
    lv_obj_t *row = agenda_box(list);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, lv_pct(100), 64);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);

    lv_obj_t *dot = agenda_box(row);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(AGENDA_COLORS[i % AGENDA_COLOR_COUNT]), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_t *name = agenda_label(row, cals[i].second, f.title);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_height(name, lv_font_get_line_height(f.title));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 64, 34);
    if (!calendar_hidden(*hidden, cals[i].first))
      lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, agenda_setting_toggled, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(i));
    lv_obj_add_event_cb(row, agenda_setting_row_clicked, LV_EVENT_CLICKED, sw);
  }
  if (keep != nullptr)
    lv_obj_move_to_index(keep, -1);
}

}  // namespace ui
