#include "mjpeg_stream.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "driver/jpeg_decode.h"
#define MJPEG_HW_JPEG 1
#else
#define MJPEG_HW_JPEG 0
#endif

namespace esphome {
namespace mjpeg_stream {

static const char *const TAG = "mjpeg_stream";

static constexpr uint32_t RX_STACK = 6144;
static constexpr uint32_t DECODE_STACK = 6144;
static constexpr int HTTP_TIMEOUT_MS = 5000;
static constexpr uint32_t RECONNECT_DELAY_MS = 1000;
static constexpr size_t HEADER_LINE_MAX = 256;
static constexpr size_t align16(size_t v) { return (v + 15) & ~(size_t) 15; }

static void *psram_alloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr)
    p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
  return p;
}

#if MJPEG_HW_JPEG
// The P4 has a single JPEG codec block: one engine shared by all streams, serialized by a lock.
static jpeg_decoder_handle_t s_hw_dec = nullptr;
static SemaphoreHandle_t s_hw_lock = nullptr;

static bool hw_jpeg_init() {
  if (s_hw_dec != nullptr)
    return true;
  jpeg_decode_engine_cfg_t cfg{};
  cfg.timeout_ms = 200;
  if (jpeg_new_decoder_engine(&cfg, &s_hw_dec) != ESP_OK) {
    s_hw_dec = nullptr;
    return false;
  }
  s_hw_lock = xSemaphoreCreateMutex();
  return true;
}

// Buffers the JPEG engine reads from / writes to need its DMA alignment.
static void *hw_alloc(size_t n, bool input) {
  jpeg_decode_memory_alloc_cfg_t cfg{};
  cfg.buffer_direction = input ? JPEG_DEC_ALLOC_INPUT_BUFFER : JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  size_t got = 0;
  return jpeg_alloc_decoder_mem(n, &cfg, &got);
}
#endif

// JPEG input buffers and RGB565 framebuffers: DMA-aligned for the hardware decoder when
// there is one, plain PSRAM otherwise.
static void *alloc_buf(size_t n, bool jpeg_input) {
#if MJPEG_HW_JPEG
  if (s_hw_dec != nullptr) {
    if (void *p = hw_alloc(n, jpeg_input))
      return p;
  }
#endif
  return psram_alloc(n);
}

// ---------------------------------------------------------------------------
// Public control

void MjpegStream::set_url(const std::string &url) {
  if (this->lock_ != nullptr)
    xSemaphoreTake(this->lock_, portMAX_DELAY);
  this->url_ = url;
  this->url_gen_.fetch_add(1);
  if (this->lock_ != nullptr)
    xSemaphoreGive(this->lock_);
  if (this->rx_task_handle_ != nullptr)
    xTaskNotifyGive(this->rx_task_handle_);
}

void MjpegStream::start() {
  if (this->running_.load())
    return;
  if (this->lock_ != nullptr && !this->alloc_frames_()) {
    ESP_LOGE(TAG, "[%d,%d] not enough memory for the frame buffers; not starting", this->x_, this->y_);
    return;
  }
  if (this->running_.exchange(true))
    return;
  this->discard_frames_();  // never show a frame left over from before the last stop()
  this->url_gen_.fetch_add(1);
  if (this->rx_task_handle_ != nullptr)
    xTaskNotifyGive(this->rx_task_handle_);
}

void MjpegStream::stop() {
  if (!this->running_.exchange(false))
    return;
  this->url_gen_.fetch_add(1);
  this->discard_frames_();
}

bool MjpegStream::resize(int x, int y, int w, int h) {
  if (this->running_.load()) {
    ESP_LOGW(TAG, "resize() ignored while running; call stop() first");
    return false;
  }
  if (w > this->max_w_ || h > this->max_h_ || w <= 0 || h <= 0) {
    ESP_LOGW(TAG, "resize to %dx%d exceeds max window %dx%d", w, h, this->max_w_, this->max_h_);
    return false;
  }
  // A decode still in flight may use the old or new size; buffers hold the max window,
  // so either stays in bounds, and its frame is discarded by the next start().
  this->x_ = x;
  this->y_ = y;
  this->w_ = w;
  this->h_ = h;
  return true;
}

void MjpegStream::discard_frames_() {
  if (this->lock_ == nullptr)
    return;
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  this->front_ready_ = false;
  this->pending_full_ = false;
  xSemaphoreGive(this->lock_);
}

// ---------------------------------------------------------------------------
// Component

void MjpegStream::setup() {
  if (this->init_delay_ms_ > 0) {
    ESP_LOGI(TAG, "[%d,%d] delaying init by %u ms", this->x_, this->y_, (unsigned) this->init_delay_ms_);
    this->set_timeout(this->init_delay_ms_, [this]() { this->init_(); });
    return;
  }
  this->init_();
}

void MjpegStream::init_() {
  ESP_LOGI(TAG, "[%d,%d] init", this->x_, this->y_);
  this->max_w_ = std::max(this->max_w_, this->w_);
  this->max_h_ = std::max(this->max_h_, this->h_);
#if MJPEG_HW_JPEG
  if (!hw_jpeg_init())
    ESP_LOGW(TAG, "Hardware JPEG decoder unavailable; using software decoding");
#endif
  // Rows rounded up to 16: the hardware decoder always writes whole 16-row MCU blocks.
  const size_t fb_bytes = align16(this->max_w_) * align16(this->max_h_) * sizeof(uint16_t);
  this->fb_bytes_ = fb_bytes;
  {
    // Software JPEG decoder state. On the P4 the hardware decoder does the work, so keep this
    // rarely-used fallback in PSRAM and leave internal RAM to Wi-Fi (esp_hosted). On the S3 it
    // is the hot path, so keep it in fast internal RAM. 16-byte aligned for the S3 SIMD code.
#if MJPEG_HW_JPEG
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
    void *mem = heap_caps_aligned_alloc(16, sizeof(JPEGDEC), caps);
    if (mem != nullptr)
      this->jpeg_ = new (mem) JPEGDEC();
  }
  this->lock_ = xSemaphoreCreateMutex();
  if (this->push_rows_ > 0)
    this->dma_buf_ = (uint16_t *) heap_caps_malloc((size_t) this->max_w_ * this->push_rows_ * sizeof(uint16_t),
                                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  // release_when_stopped: the frame buffers wait for start()
  if (!this->jpeg_ || !this->lock_ || (this->push_rows_ > 0 && !this->dma_buf_) ||
      (!this->release_when_stopped_ && !this->alloc_frames_())) {
    ESP_LOGE(TAG, "Buffer allocation failed");
    this->mark_failed();
    return;
  }

  // Spread decoders across cores: one per core. Core 0 also runs Wi-Fi, but at a
  // higher priority. Decoders run at the main loop's priority (1): the loop sleeps between
  // ticks, so they get the core then, and time-slicing hands it back within a 1 ms tick.
  // (At priority 0 they would split the core 50/50 with the FreeRTOS idle task.)
  static int instance_count = 0;
  const BaseType_t decode_core = (instance_count++ % 2 == 0) ? 0 : 1;
  if (xTaskCreatePinnedToCore(&MjpegStream::rx_task_tramp_, "mjpeg_rx", RX_STACK, this, 5, &this->rx_task_handle_,
                              0) != pdPASS ||
      xTaskCreatePinnedToCore(&MjpegStream::decode_task_tramp_, "mjpeg_dec", DECODE_STACK, this, 1,
                              &this->decode_task_handle_, decode_core) != pdPASS) {
    ESP_LOGE(TAG, "Task creation failed");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "[%d,%d] setup done: internal free %u (largest %u), PSRAM free %u", this->x_, this->y_,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  this->last_stats_ms_ = millis();
  if (this->autostart_ && !this->url_.empty())
    this->start();
}

// The JPEG and frame buffers: allocated at setup, or (release_when_stopped) at each start()
// and handed back by loop() once a stop() has let both tasks go quiet.
bool MjpegStream::alloc_frames_() {
  if (this->show_fb_ != nullptr)
    return true;  // still held: never released, or not released yet
  this->rx_buf_ = (uint8_t *) alloc_buf(this->max_frame_size_, true);
  this->pending_buf_ = (uint8_t *) alloc_buf(this->max_frame_size_, true);
  this->work_buf_ = (uint8_t *) alloc_buf(this->max_frame_size_, true);
  this->back_fb_ = (uint16_t *) alloc_buf(this->fb_bytes_, false);
  this->front_fb_ = (uint16_t *) alloc_buf(this->fb_bytes_, false);
  this->show_fb_ = (uint16_t *) alloc_buf(this->fb_bytes_, false);
  if (!this->rx_buf_ || !this->pending_buf_ || !this->work_buf_ || !this->back_fb_ || !this->front_fb_ ||
      !this->show_fb_) {
    this->free_frames_();
    return false;
  }
  memset(this->show_fb_, 0, this->fb_bytes_);
  return true;
}

void MjpegStream::free_frames_() {
  heap_caps_free(this->rx_buf_);
  heap_caps_free(this->pending_buf_);
  heap_caps_free(this->work_buf_);
  heap_caps_free(this->back_fb_);
  heap_caps_free(this->front_fb_);
  heap_caps_free(this->show_fb_);
  this->rx_buf_ = this->pending_buf_ = this->work_buf_ = nullptr;
  this->back_fb_ = this->front_fb_ = this->show_fb_ = nullptr;
}

void MjpegStream::dump_config() {
  ESP_LOGCONFIG(TAG, "MJPEG stream:");
  ESP_LOGCONFIG(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGCONFIG(TAG, "  Window: %d,%d %dx%d", this->x_, this->y_, this->w_, this->h_);
  ESP_LOGCONFIG(TAG, "  Max frame size: %u", (unsigned) this->max_frame_size_);
}

void MjpegStream::loop() {
  if (this->lock_ == nullptr)
    return;  // not initialized yet (init_delay) or failed
  if (this->release_when_stopped_ && this->show_fb_ != nullptr && !this->running_.load() && this->rx_idle_.load()) {
    xSemaphoreTake(this->lock_, portMAX_DELAY);
    if (!this->dec_busy_) {  // otherwise a decode that began before stop() is still writing
      this->pending_full_ = false;
      this->front_ready_ = false;
      this->free_frames_();
      ESP_LOGD(TAG, "[%d,%d] frame buffers released", this->x_, this->y_);
    }
    xSemaphoreGive(this->lock_);
  }
  // Push the newest decoded frame, if any. Runs on the main loop so it is serialized
  // with any other drawing (e.g. LVGL) on the same display.
  bool have = false;
  uint32_t gen = 0;
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  // Only while running: after stop() the screen may belong to something else (e.g. another
  // stream in a different layout), and a late frame from the decoder must not overwrite it.
  if (this->front_ready_ && this->running_.load()) {
    std::swap(this->front_fb_, this->show_fb_);
    this->front_ready_ = false;
    have = true;
    gen = this->front_gen_;
  }
  xSemaphoreGive(this->lock_);

  if (have) {
    if (gen != this->shown_gen_) {
      this->shown_gen_ = gen;
      this->first_frame_callback_.call();
    }
    // The decoder waits for the screen to take each frame before decoding the next
    // (see decode_task_), so let it start on the newest pending frame now.
    xTaskNotifyGive(this->decode_task_handle_);
    const int64_t t0 = esp_timer_get_time();
    // Push in strips through a small internal DMA buffer. Handing the SPI driver the whole
    // PSRAM framebuffer makes it allocate a same-sized internal bounce buffer per transfer,
    // which fails (ESP_ERR_NO_MEM) once LVGL is also using internal RAM.
    if (this->push_rows_ == 0) {
      // Displays that copy into their own framebuffer (e.g. MIPI-DSI) take the frame in one call.
      this->display_->draw_pixels_at(this->x_, this->y_, this->w_, this->h_, (const uint8_t *) this->show_fb_,
                                     display::COLOR_ORDER_RGB, display::COLOR_BITNESS_565, this->big_endian_, 0, 0, 0);
    } else {
      for (int row = 0; row < this->h_; row += this->push_rows_) {
        const int n = std::min(this->push_rows_, this->h_ - row);
        memcpy(this->dma_buf_, this->show_fb_ + (size_t) row * this->w_, (size_t) n * this->w_ * sizeof(uint16_t));
        this->display_->draw_pixels_at(this->x_, this->y_ + row, this->w_, n, (const uint8_t *) this->dma_buf_,
                                       display::COLOR_ORDER_RGB, display::COLOR_BITNESS_565, this->big_endian_, 0, 0,
                                       0);
      }
    }
    const uint32_t dt = (uint32_t) (esp_timer_get_time() - t0);
    this->st_push_frames_++;
    this->st_push_us_ += dt;
    this->st_push_max_us_ = std::max(this->st_push_max_us_, dt);
  }

  const uint32_t now = millis();
  if (this->stats_interval_ms_ > 0 && now - this->last_stats_ms_ >= this->stats_interval_ms_) {
    this->log_stats_();
    this->last_stats_ms_ = now;
  }
}

void MjpegStream::log_stats_() {
  const float secs = (millis() - this->last_stats_ms_) / 1000.0f;
  const uint32_t rx = this->st_rx_frames_.exchange(0);
  const uint32_t bytes = this->st_rx_bytes_.exchange(0);
  const uint32_t rx_drops = this->st_rx_drops_.exchange(0);
  const uint32_t dec = this->st_dec_frames_.exchange(0);
  const uint32_t dec_us = this->st_dec_us_.exchange(0);
  const uint32_t dec_max = this->st_dec_max_us_.exchange(0);
  const uint32_t dec_fail = this->st_dec_fail_.exchange(0);
  const uint32_t dec_drops = this->st_dec_drops_.exchange(0);
  const uint32_t push = this->st_push_frames_;
  if (!this->running_.load() && rx == 0 && push == 0)
    return;
  ESP_LOGI(TAG,
           "[%d,%d] rx %.1f fps %.0f kB/s (avg %.1f kB) | decode avg %.1f max %.1f ms | push %.1f fps avg %.1f max "
           "%.1f ms | dropped rx %u dec %u | fail %u | connects %u | hw %u",
           this->x_, this->y_, rx / secs, bytes / 1024.0f / secs, rx ? bytes / 1024.0f / rx : 0.0f,
           dec ? dec_us / 1000.0f / dec : 0.0f, dec_max / 1000.0f, push / secs,
           push ? this->st_push_us_ / 1000.0f / push : 0.0f, this->st_push_max_us_ / 1000.0f, (unsigned) rx_drops,
           (unsigned) dec_drops, (unsigned) dec_fail, (unsigned) this->st_connects_.load(), (unsigned) this->st_hw_frames_.exchange(0));
  this->st_push_frames_ = 0;
  this->st_push_us_ = 0;
  this->st_push_max_us_ = 0;
}

// ---------------------------------------------------------------------------
// Receive task (core 0)

void MjpegStream::rx_task_tramp_(void *arg) { static_cast<MjpegStream *>(arg)->rx_task_(); }

void MjpegStream::rx_task_() {
  while (true) {
    if (!this->running_.load()) {
      this->rx_idle_.store(true);  // uses no buffer now; loop() may release them
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      this->rx_idle_.store(false);
      continue;
    }
    xSemaphoreTake(this->lock_, portMAX_DELAY);
    std::string url = this->url_;
    const uint32_t gen = this->url_gen_.load();
    xSemaphoreGive(this->lock_);

    if (url.empty()) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (!network::is_connected()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!this->stream_once_(url, gen)) {
      // Error: back off, but wake early if the URL changes or we are stopped.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
  }
}

namespace {
// Small buffered reader over esp_http_client so header lines can be parsed cheaply
// while JPEG payloads are read straight into the destination buffer.
struct Reader {
  esp_http_client_handle_t client;
  uint8_t buf[2048];
  size_t pos{0}, len{0};

  bool fill() {
    int n = esp_http_client_read(this->client, (char *) this->buf, sizeof(this->buf));
    if (n <= 0)
      return false;
    this->pos = 0;
    this->len = n;
    return true;
  }
  // Reads one line (without CR/LF). Returns false on connection error.
  bool line(char *out, size_t cap) {
    size_t n = 0;
    while (true) {
      if (this->pos >= this->len && !this->fill())
        return false;
      char c = (char) this->buf[this->pos++];
      if (c == '\n')
        break;
      if (c != '\r' && n + 1 < cap)
        out[n++] = c;
    }
    out[n] = 0;
    return true;
  }
  // Reads exactly n bytes into dst (dst may be null to discard).
  bool read(uint8_t *dst, size_t n) {
    while (n > 0) {
      if (this->pos < this->len) {
        size_t k = std::min(n, this->len - this->pos);
        if (dst) {
          memcpy(dst, this->buf + this->pos, k);
          dst += k;
        }
        this->pos += k;
        n -= k;
        continue;
      }
      if (dst && n >= sizeof(this->buf)) {
        int r = esp_http_client_read(this->client, (char *) dst, n);
        if (r <= 0)
          return false;
        dst += r;
        n -= r;
      } else if (!this->fill()) {
        return false;
      }
    }
    return true;
  }
};
}  // namespace

bool MjpegStream::stream_once_(const std::string &url, uint32_t gen) {
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.buffer_size = 4096;
  cfg.keep_alive_enable = true;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr)
    return false;

  bool ok = false;
  this->st_connects_++;
  ESP_LOGI(TAG, "Connecting to %s", url.c_str());
  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Connect failed");
    esp_http_client_cleanup(client);
    return false;
  }
  esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGW(TAG, "HTTP status %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  auto *rd = new Reader();
  rd->client = client;
  char line[HEADER_LINE_MAX];

  while (this->running_.load() && this->url_gen_.load() == gen) {
    // Part headers: skip boundary/blank lines until we see Content-Length, then the blank line.
    size_t content_len = 0;
    bool in_headers = false, err = false;
    while (true) {
      if (!rd->line(line, sizeof(line))) {
        err = true;
        break;
      }
      if (line[0] == 0) {
        if (in_headers)
          break;  // end of part headers
        continue;
      }
      if (line[0] == '-' && line[1] == '-') {
        in_headers = true;
        continue;
      }
      in_headers = true;
      if (strncasecmp(line, "Content-Length:", 15) == 0)
        content_len = strtoul(line + 15, nullptr, 10);
    }
    if (err)
      break;
    if (content_len == 0) {
      ESP_LOGW(TAG, "Part without Content-Length; unsupported stream format");
      break;
    }
    if (content_len > this->max_frame_size_) {
      ESP_LOGW(TAG, "Frame of %u bytes exceeds max_frame_size, skipping", (unsigned) content_len);
      if (!rd->read(nullptr, content_len))
        break;
      continue;
    }
    if (!rd->read(this->rx_buf_, content_len))
      break;
    this->st_rx_frames_++;
    this->st_rx_bytes_ += content_len;
    this->publish_frame_(content_len, gen);
  }
  ok = this->running_.load() == false || this->url_gen_.load() != gen;  // clean exit vs. error
  if (!ok)
    ESP_LOGW(TAG, "Stream ended");

  delete rd;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

void MjpegStream::publish_frame_(size_t len, uint32_t gen) {
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  std::swap(this->rx_buf_, this->pending_buf_);
  if (this->pending_full_)
    this->st_rx_drops_++;  // decoder hadn't taken the previous frame yet: replace it with the newer one
  this->pending_len_ = len;
  this->pending_gen_ = gen;
  this->pending_full_ = true;
  xSemaphoreGive(this->lock_);
  xTaskNotifyGive(this->decode_task_handle_);
}

// ---------------------------------------------------------------------------
// Decode task (core 1)

void MjpegStream::decode_task_tramp_(void *arg) { static_cast<MjpegStream *>(arg)->decode_task_(); }

void MjpegStream::decode_task_() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    size_t len = 0;
    uint32_t gen = 0;
    xSemaphoreTake(this->lock_, portMAX_DELAY);
    // Only decode once the previous frame has been taken by the screen: decoding a frame
    // that would be overwritten before display wastes CPU the main loop needs.
    if (this->pending_full_ && !this->front_ready_) {
      std::swap(this->pending_buf_, this->work_buf_);
      len = this->pending_len_;
      gen = this->pending_gen_;
      this->pending_full_ = false;
      this->dec_busy_ = true;  // until this frame is done with; cleared under the lock
    }
    xSemaphoreGive(this->lock_);
    if (len == 0)
      continue;

    const int64_t t0 = esp_timer_get_time();
    const bool ok = this->decode_into_(this->work_buf_, len, this->back_fb_);
    const uint32_t dt = (uint32_t) (esp_timer_get_time() - t0);
    if (!ok) {
      this->st_dec_fail_++;
      xSemaphoreTake(this->lock_, portMAX_DELAY);
      this->dec_busy_ = false;
      xSemaphoreGive(this->lock_);
      continue;
    }
    this->st_dec_frames_++;
    if (this->bench_countdown_ > 0 && --this->bench_countdown_ == 0)
      this->benchmark_(len);
    this->st_dec_us_ += dt;
    uint32_t prev = this->st_dec_max_us_.load();
    while (dt > prev && !this->st_dec_max_us_.compare_exchange_weak(prev, dt)) {
    }

    xSemaphoreTake(this->lock_, portMAX_DELAY);
    this->dec_busy_ = false;  // the swap below happens under the lock
    if (gen != this->url_gen_.load()) {
      // Received before a stop/start/URL change (e.g. a 320x180 frame finishing after the
      // switch to fullscreen): showing it would draw the wrong picture into the new layout.
      xSemaphoreGive(this->lock_);
      continue;
    }
    std::swap(this->back_fb_, this->front_fb_);
    this->front_gen_ = gen;
    if (this->front_ready_)
      this->st_dec_drops_++;  // main loop hadn't pushed the previous frame yet
    this->front_ready_ = true;
    xSemaphoreGive(this->lock_);
    App.wake_loop_threadsafe();  // push it now rather than at the next loop tick
  }
}

bool MjpegStream::decode_into_(const uint8_t *data, size_t len, uint16_t *fb) {
#if MJPEG_HW_JPEG
  if (s_hw_dec != nullptr) {
    // Hardware path: the engine writes rows of the image's (16-aligned) width straight into fb,
    // so it is only used when that matches the window width.
    jpeg_decode_picture_info_t info{};
    if (jpeg_decoder_get_info(data, len, &info) == ESP_OK && info.width == (uint32_t) this->w_ &&
        (info.width % 16) == 0) {
      const uint32_t out_size = info.width * align16(info.height) * sizeof(uint16_t);
      if (out_size <= this->fb_bytes_) {
        jpeg_decode_cfg_t cfg{};
        cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
        // BGR element order gives little-endian RGB565, which is what this panel takes.
        cfg.rgb_order = this->big_endian_ ? JPEG_DEC_RGB_ELEMENT_ORDER_RGB : JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
        cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
        uint32_t written = 0;
        xSemaphoreTake(s_hw_lock, portMAX_DELAY);
        const esp_err_t err =
            jpeg_decoder_process(s_hw_dec, &cfg, data, len, (uint8_t *) fb, out_size, &written);
        xSemaphoreGive(s_hw_lock);
        if (err == ESP_OK) {
          this->st_hw_frames_++;
          return true;
        }
        ESP_LOGV(TAG, "HW decode failed: %s", esp_err_to_name(err));
      }
    }
  }
#endif
  if (this->jpeg_ == nullptr)
    return false;
  if (!this->jpeg_->openRAM((uint8_t *) data, (int) len, &MjpegStream::jpeg_draw_cb_)) {
    ESP_LOGV(TAG, "openRAM failed: %d", this->jpeg_->getLastError());
    return false;
  }
  this->jpeg_->setUserPointer(this);
  this->jpeg_->setPixelType(this->big_endian_ ? RGB565_BIG_ENDIAN : RGB565_LITTLE_ENDIAN);
  this->jpeg_->setMaxOutputSize(8 * 2048);
  this->decode_target_ = fb;
  const int rc = this->jpeg_->decode(0, 0, 0);
  this->jpeg_->close();
  return rc != 0;
}

int MjpegStream::jpeg_draw_cb_(JPEGDRAW *p) {
  auto *self = static_cast<MjpegStream *>(p->pUser);
  // Copy the decoded block into the framebuffer, clipped to the window.
  const int x0 = p->x, y0 = p->y;
  if (self->skip_copy_)
    return 1;
  if (x0 >= self->w_ || y0 >= self->h_)
    return 1;
  const int w = std::min<int>(p->iWidth, self->w_ - x0);
  const int h = std::min<int>(p->iHeight, self->h_ - y0);
  const uint16_t *src = p->pPixels;
  uint16_t *dst = self->decode_target_ + (size_t) y0 * self->w_ + x0;
  for (int row = 0; row < h; row++) {
    memcpy(dst, src, w * sizeof(uint16_t));
    src += p->iWidth;
    dst += self->w_;
  }
  return 1;
}

}  // namespace mjpeg_stream
}  // namespace esphome

namespace esphome {
namespace mjpeg_stream {
// One-off timing of the decoder in isolation (high priority, same frame 10x).
void MjpegStream::benchmark_(size_t len) {
  const UBaseType_t prio = uxTaskPriorityGet(nullptr);
  vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 3);
  uint32_t t_copy = 0, t_nocopy = 0, t_internal = 0;
  for (int i = 0; i < 10; i++) {
    int64_t t0 = esp_timer_get_time();
    this->decode_into_(this->work_buf_, len, this->back_fb_);
    t_copy += esp_timer_get_time() - t0;
  }
  this->skip_copy_ = true;
  for (int i = 0; i < 10; i++) {
    int64_t t0 = esp_timer_get_time();
    this->decode_into_(this->work_buf_, len, this->back_fb_);
    t_nocopy += esp_timer_get_time() - t0;
  }
  this->skip_copy_ = false;
  uint8_t *ibuf = (uint8_t *) heap_caps_malloc(len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (ibuf != nullptr) {
    memcpy(ibuf, this->work_buf_, len);
    for (int i = 0; i < 10; i++) {
      int64_t t0 = esp_timer_get_time();
      this->decode_into_(ibuf, len, this->back_fb_);
      t_internal += esp_timer_get_time() - t0;
    }
    free(ibuf);
  }
  vTaskPrioritySet(nullptr, prio);
  ESP_LOGI(TAG, "[%d,%d] BENCH %u B frame on core %d: decode+copy %.1f ms, decode only %.1f ms, input in IRAM %.1f ms",
           this->x_, this->y_, (unsigned) len, xPortGetCoreID(), t_copy / 10000.0f, t_nocopy / 10000.0f,
           t_internal / 10000.0f);
}
}  // namespace mjpeg_stream
}  // namespace esphome
