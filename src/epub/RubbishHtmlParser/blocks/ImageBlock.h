#pragma once
#include "../../EpubList/Epub.h"
#include "../../Renderer/Renderer.h"
#include "Block.h"
#include <Arduino.h>

#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGD(args...)
#define ESP_LOGW(args...)
#endif

class ImageBlock : public Block {
public:
  // the src attribute from the image element
  std::string m_src;
  int y_pos;
  int x_pos;
  int width;
  int height;

  // Cached image data (loaded during layout, used during render)
  uint8_t *m_cached_data = nullptr;
  size_t m_cached_size = 0;

  ImageBlock(std::string src) : m_src(src) {}

  ~ImageBlock() {
    // Free cached data on destruction
    if (m_cached_data) {
      free(m_cached_data);
      m_cached_data = nullptr;
    }
  }

  virtual bool isEmpty() { return m_src.empty(); }
  void layout(Renderer *renderer, Epub *epub, int max_width = -1) {
    // Load and CACHE image data during layout (runs in background task with
    // 32KB stack)
    m_cached_data = epub->get_item_contents(m_src, &m_cached_size);
    if (!m_cached_data) {
      Serial.printf("ImageBlock: Failed to load %s\n", m_src.c_str());
      width = 0;
      height = 0;
      return;
    }

    renderer->get_image_size(m_src, m_cached_data, m_cached_size, &width,
                             &height);
    if (width > renderer->get_page_width() ||
        height > renderer->get_page_height()) {
      float scale = std::min(
          float(max_width != -1 ? max_width : renderer->get_page_width()) /
              float(width),
          float(renderer->get_page_height()) / float(height));
      width *= scale;
      height *= scale;
    }
    // horizontal center
    x_pos = (renderer->get_page_width() - width) / 2;
    // Note: Keep m_cached_data for render(), don't free here
  }
  void render(Renderer *renderer, Epub *epub, int y_pos) {
    // Use cached image data instead of reloading from ZIP
    if (!m_cached_data || m_cached_size == 0) {
      Serial.println("ImageBlock::render() - No cached data!");
      return;
    }
    // Draw a square to remove text remainings before printing image
    renderer->fill_rect(x_pos, y_pos, width, height, 255);
    renderer->flush_area(x_pos, y_pos, width, height);
    renderer->draw_image(m_src, m_cached_data, m_cached_size, x_pos, y_pos,
                         width, height);
    // Note: Don't free here - might need to re-render. Freed in destructor.
  }
  virtual void dump() { printf("ImageBlock: %s\n", m_src.c_str()); }
  virtual BlockType getType() { return IMAGE_BLOCK; }
};
