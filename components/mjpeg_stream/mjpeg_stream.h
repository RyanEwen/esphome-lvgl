#pragma once

#include <atomic>
#include <string>

#include "esphome/components/display/display.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "JPEGDEC.h"

namespace esphome {
namespace mjpeg_stream {

class MjpegStream : public Component {
 public:
  void set_display(display::Display *d) { this->display_ = d; }
  void set_window(int x, int y, int w, int h) {
    this->x_ = x;
    this->y_ = y;
    this->w_ = w;
    this->h_ = h;
  }
  void set_autostart(bool a) { this->autostart_ = a; }
  void set_stats_interval(uint32_t ms) { this->stats_interval_ms_ = ms; }
  void set_max_frame_size(size_t s) { this->max_frame_size_ = s; }
  /// RGB565 byte order the display expects.
  void set_big_endian(bool b) { this->big_endian_ = b; }
  void set_release_when_stopped(bool r) { this->release_when_stopped_ = r; }
  /// Rows per draw call through an internal DMA strip (SPI panels); 0 = whole frame at once.
  void set_push_rows(int n) { this->push_rows_ = n; }
  /// Largest window this stream may be resized to at runtime (buffers are sized for it).
  void set_max_window(int w, int h) {
    this->max_w_ = w;
    this->max_h_ = h;
  }
  /// Move/resize the window. Only while stopped; fails if larger than the max window.
  bool resize(int x, int y, int w, int h);

  /// Change the stream URL. Safe to call any time; reconnects if running.
  void set_url(const std::string &url);
  /// Start streaming (connects and keeps reconnecting until stop()).
  void start();
  /// Stop streaming and disconnect. The last frame stays on screen.
  void stop();
  bool is_running() const { return this->running_.load(); }
  /// Called (main loop) just before the first frame of each new stream is drawn: after
  /// start(), or after set_url() while running. Use it to hide a loading placeholder.
  void add_on_first_frame_callback(std::function<void()> &&cb) { this->first_frame_callback_.add(std::move(cb)); }

  void setup() override;
  void set_init_delay(uint32_t ms) { this->init_delay_ms_ = ms; }
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  static void rx_task_tramp_(void *arg);
  static void decode_task_tramp_(void *arg);
  static int jpeg_draw_cb_(JPEGDRAW *p);
  void rx_task_();
  void decode_task_();
  /// Connects and reads frames until error, stop, or URL change. Returns false on error.
  bool stream_once_(const std::string &url, uint32_t gen);
  void publish_frame_(size_t len, uint32_t gen);
  bool decode_into_(const uint8_t *data, size_t len, uint16_t *fb);
  bool alloc_frames_();
  void free_frames_();
  void log_stats_();
  void init_();
  uint32_t init_delay_ms_{0};
  void discard_frames_();

  display::Display *display_{nullptr};
  int x_{0}, y_{0}, w_{0}, h_{0};
  int max_w_{0}, max_h_{0};  // buffer capacity in pixels (>= w_, h_)
  bool autostart_{true};
  uint32_t stats_interval_ms_{5000};
  size_t max_frame_size_{65536};
  bool big_endian_{true};
  int push_rows_{16};
  size_t fb_bytes_{0};

  std::string url_;
  std::atomic<uint32_t> url_gen_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> rx_idle_{false};  // receive task waiting while stopped: holds no buffer
  bool dec_busy_{false};              // decoder holds a frame (guarded by lock_)
  bool release_when_stopped_{false};

  TaskHandle_t rx_task_handle_{nullptr};
  TaskHandle_t decode_task_handle_{nullptr};
  SemaphoreHandle_t lock_{nullptr};

  // JPEG buffers: rx (being filled) -> pending (newest complete) -> work (being decoded)
  uint8_t *rx_buf_{nullptr};
  uint8_t *pending_buf_{nullptr};
  uint8_t *work_buf_{nullptr};
  size_t pending_len_{0};
  uint32_t pending_gen_{0};  // url_gen_ the pending frame was received under
  bool pending_full_{false};

  // RGB565 framebuffers: back (decode target) -> front (newest decoded) -> show (being pushed)
  uint16_t *back_fb_{nullptr};
  uint16_t *front_fb_{nullptr};
  uint16_t *show_fb_{nullptr};
  uint16_t *dma_buf_{nullptr};  // internal DMA strip used to push show_fb_ to the panel
  bool front_ready_{false};
  uint32_t front_gen_{0};                // url_gen_ of the frame in front_fb_
  uint32_t shown_gen_{UINT32_MAX};       // url_gen_ whose first frame has been drawn
  CallbackManager<void()> first_frame_callback_;

  JPEGDEC *jpeg_{nullptr};  // software decoder (~18 KB), allocated in init_()
  uint16_t *decode_target_{nullptr};
  bool skip_copy_{false};  // benchmark only
  uint32_t bench_countdown_{0};  // set >0 to run a one-off decode benchmark after N frames
  void benchmark_(size_t len);

  // stats (written from several tasks; approximate is fine)
  std::atomic<uint32_t> st_rx_frames_{0}, st_rx_bytes_{0}, st_rx_drops_{0};
  std::atomic<uint32_t> st_dec_frames_{0}, st_dec_us_{0}, st_dec_fail_{0}, st_dec_drops_{0};
  std::atomic<uint32_t> st_connects_{0};
  std::atomic<uint32_t> st_hw_frames_{0};
  uint32_t st_push_frames_{0}, st_push_us_{0}, st_push_max_us_{0};
  std::atomic<uint32_t> st_dec_max_us_{0};
  uint32_t last_stats_ms_{0};
};

class FirstFrameTrigger : public Trigger<> {
 public:
  explicit FirstFrameTrigger(MjpegStream *parent) {
    parent->add_on_first_frame_callback([this]() { this->trigger(); });
  }
};

}  // namespace mjpeg_stream
}  // namespace esphome
