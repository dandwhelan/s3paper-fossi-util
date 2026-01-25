#include "Renderer.h"
#include <algorithm>
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define vTaskDelay(t)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#endif

Renderer::~Renderer() {
  // No helpers to delete
}

// Image methods are now pure virtual and handled by M5GFXRenderer

void Renderer::draw_text_box(const std::string &text, int x, int y, int width,
                             int height, bool bold, bool italic) {
  int length = text.length();
  // fit the text into the box
  int start = 0;
  int end = 1;
  int ypos = 0;
  while (start < length && ypos + get_line_height() < height) {
    while (end < length &&
           get_text_width(text.substr(start, end - start).c_str(), bold,
                          italic) < width) {
      end++;
    }
    if (get_text_width(text.substr(start, end - start).c_str(), bold, italic) >
        width) {
      end--;
    }
    draw_text(x, y + ypos, text.substr(start, end - start).c_str(), bold,
              italic);
    ypos += get_line_height();
    start = end;
    end = start + 1;
  }
}
