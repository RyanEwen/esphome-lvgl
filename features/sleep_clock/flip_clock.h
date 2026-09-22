// Split-flap card animation for the sleep clock.
//
// A card is the obj built by card.yaml, and this file finds its parts by child
// index, so the two files change together:
//
//   0  top half     -- static, shows the NEW digit as soon as a flip starts
//   1  bottom half  -- static, keeps the OLD digit until the lower flap lands
//   2  upper flap   -- OLD digit's top half; folds down onto the hinge
//   3  lower flap   -- NEW digit's bottom half; unfolds from the hinge
//   4  hinge line
//
// Each half is a clipping obj holding a full-card-height obj, which holds the
// label. The top half shows the upper part of that, the bottom half is shifted
// up by half a card and shows the lower part, so one centred label reads as a
// digit split across the hinge. Folding is transform_scale_y about the hinge
// edge; LVGL renders a transformed obj through a layer, so the clipped label
// scales with it.
#pragma once

#include <cstring>
#include <string>

#include "lvgl.h"

namespace flip {

static const uint32_t HALF_MS = 160;  // per flap; a full flip is twice this

inline lv_obj_t *label_of(lv_obj_t *half) { return lv_obj_get_child(lv_obj_get_child(half, 0), 0); }

inline void set_half(lv_obj_t *half, const char *text) { lv_label_set_text(label_of(half), text); }

// A flap is hidden, never drawn, at scale 0. LVGL 9.5 leaks there:
// lv_obj_refr() creates a layer for a transformed obj, and lv_draw_layer()
// returns early for scale <= 0 without queueing the blend task that frees it.
// A visible flap at scale 0 leaked one layer (~24KB of PSRAM) per frame, which
// is what ran the panel out of memory a few hours into a night of flips.
inline void scale_cb(void *obj, int32_t v) {
  auto *flap = (lv_obj_t *) obj;
  if (v <= 0) {
    lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_set_style_transform_scale_y(flap, v, 0);
  lv_obj_remove_flag(flap, LV_OBJ_FLAG_HIDDEN);
}

inline void upper_done(lv_anim_t *a) { lv_obj_add_flag((lv_obj_t *) a->var, LV_OBJ_FLAG_HIDDEN); }

inline void lower_done(lv_anim_t *a) {
  lv_obj_t *flap = (lv_obj_t *) a->var;
  lv_obj_t *bottom = lv_obj_get_child(lv_obj_get_parent(flap), 1);
  set_half(bottom, lv_label_get_text(label_of(flap)));
  lv_obj_add_flag(flap, LV_OBJ_FLAG_HIDDEN);
}

// Shows `digit` -- one character, or "" for a blank card -- on `card`.
// A no-op when the card already shows it, so callers can set every card on
// every tick and only the ones that changed will move.
inline void set(lv_obj_t *card, const char *digit, bool animate) {
  lv_obj_t *top = lv_obj_get_child(card, 0);
  lv_obj_t *bottom = lv_obj_get_child(card, 1);
  lv_obj_t *upper = lv_obj_get_child(card, 2);
  lv_obj_t *lower = lv_obj_get_child(card, 3);

  std::string old = lv_label_get_text(label_of(top));  // copy: top is overwritten below
  if (old == digit) {
    return;
  }

  // a flip still running is cut short; its target is what `top` shows, so
  // settling the bottom on `old` leaves the card consistent either way
  lv_anim_delete(upper, scale_cb);
  lv_anim_delete(lower, scale_cb);
  set_half(bottom, old.c_str());

  set_half(top, digit);
  if (!animate) {
    set_half(bottom, digit);
    lv_obj_add_flag(upper, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lower, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  set_half(upper, old.c_str());
  set_half(lower, digit);
  lv_obj_update_layout(card);
  lv_obj_set_style_transform_pivot_y(upper, lv_obj_get_height(upper), 0);  // its bottom edge is the hinge
  lv_obj_set_style_transform_pivot_y(lower, 0, 0);                         // its top edge is the hinge
  scale_cb(upper, LV_SCALE_NONE);
  scale_cb(lower, 0);  // hidden until it starts to unfold -- see scale_cb

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, upper);
  lv_anim_set_exec_cb(&a, scale_cb);
  lv_anim_set_values(&a, LV_SCALE_NONE, 0);
  lv_anim_set_duration(&a, HALF_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&a, upper_done);
  lv_anim_start(&a);

  lv_anim_t b;
  lv_anim_init(&b);
  lv_anim_set_var(&b, lower);
  lv_anim_set_exec_cb(&b, scale_cb);
  lv_anim_set_values(&b, 0, LV_SCALE_NONE);
  lv_anim_set_delay(&b, HALF_MS);
  lv_anim_set_duration(&b, HALF_MS);
  lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&b, lower_done);
  lv_anim_start(&b);
}

// Recolours a card: the digit on all four halves, their face, and the card's
// outline. The hinge keeps the page's black.
inline void set_colors(lv_obj_t *card, uint32_t ink, uint32_t face, uint32_t edge) {
  for (uint32_t i = 0; i < 4; i++) {
    lv_obj_t *half = lv_obj_get_child(card, i);
    lv_obj_set_style_bg_color(half, lv_color_hex(face), 0);
    lv_obj_set_style_text_color(label_of(half), lv_color_hex(ink), 0);
  }
  lv_obj_set_style_outline_color(card, lv_color_hex(edge), 0);
}

}  // namespace flip
