#pragma once
#include "Renderer/Renderer.h"
#include <M5Unified.h>
#include <cstdint>

class M5GFXRenderer : public Renderer {
public:
  M5GFXRenderer();
  virtual ~M5GFXRenderer();

  // Drawing primitives
  void draw_pixel(int x, int y, uint8_t color) override;
  void draw_rect(int x, int y, int width, int height,
                 uint8_t color = 0) override;
  void fill_rect(int x, int y, int width, int height,
                 uint8_t color = 0) override;
  void draw_circle(int x, int y, int r, uint8_t color = 0) override;
  void fill_circle(int x, int y, int r, uint8_t color = 0) override;
  void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                     uint8_t color) override;
  void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                     uint8_t color) override;

  // Text
  int get_text_width(const char *text, bool bold = false,
                     bool italic = false) override;
  void draw_text(int x, int y, const char *text, bool bold = false,
                 bool italic = false) override;

  // Images
  void draw_image(const std::string &filename, const uint8_t *data,
                  size_t data_size, int x, int y, int width,
                  int height) override;
  bool get_image_size(const std::string &filename, const uint8_t *data,
                      size_t data_size, int *width, int *height) override;

  // Gray/EPD specific (Mapped to no-ops or simple flushes)
  void needs_gray(uint8_t color) override;
  bool has_gray() override;
  void show_busy() override;
  void show_img(int x, int y, int width, int height,
                const uint8_t *img_buffer) override;
  void clear_screen() override;
  void flush_display() override;
  void flush_area(int x, int y, int width, int height) override;

  // Metrics
  int get_page_width() override;
  int get_page_height() override;
  int get_space_width() override;
  int get_line_height() override;

  void setLineSpacing(int spacing);

  void reset() override;
  void setFontSize(float size);
  void setFont(const m5gfx::IFont *font);

private:
  uint16_t _grayTo565(uint8_t gray);
  int _lineSpacing = 0;
  float _fontSize = 1.0;
  const m5gfx::IFont *_currentFont;
};
