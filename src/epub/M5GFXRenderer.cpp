#include "M5GFXRenderer.h"

M5GFXRenderer::M5GFXRenderer() {
  // Use DejaVu24 as default
  _currentFont = &fonts::DejaVu24;
  M5.Display.setFont(_currentFont);
  M5.Display.setTextSize(1);
  _lineSpacing = 2; // Default to tight/normal base
}

M5GFXRenderer::~M5GFXRenderer() {}

uint16_t M5GFXRenderer::_grayTo565(uint8_t gray) {
  // EPub uses 0=Black, 255=White usually, or vice versa?
  // Renderer.h says 0 is default color. In E-ink usually 0 is black.
  // M5GFX: 0 is BLACK, 0xFFFF is WHITE.
  // Let's assume input 'color' is 8-bit grayscale: 0=Black, 255=White.
  return M5.Display.color565(gray, gray, gray);
}

void M5GFXRenderer::draw_pixel(int x, int y, uint8_t color) {
  M5.Display.drawPixel(x + margin_left, y + margin_top, _grayTo565(color));
}

void M5GFXRenderer::draw_rect(int x, int y, int width, int height,
                              uint8_t color) {
  M5.Display.drawRect(x + margin_left, y + margin_top, width, height,
                      _grayTo565(color));
}

void M5GFXRenderer::fill_rect(int x, int y, int width, int height,
                              uint8_t color) {
  M5.Display.fillRect(x + margin_left, y + margin_top, width, height,
                      _grayTo565(color));
}

void M5GFXRenderer::draw_circle(int x, int y, int r, uint8_t color) {
  M5.Display.drawCircle(x + margin_left, y + margin_top, r, _grayTo565(color));
}

void M5GFXRenderer::fill_circle(int x, int y, int r, uint8_t color) {
  M5.Display.fillCircle(x + margin_left, y + margin_top, r, _grayTo565(color));
}

void M5GFXRenderer::draw_triangle(int x0, int y0, int x1, int y1, int x2,
                                  int y2, uint8_t color) {
  M5.Display.drawTriangle(x0 + margin_left, y0 + margin_top, x1 + margin_left,
                          y1 + margin_top, x2 + margin_left, y2 + margin_top,
                          _grayTo565(color));
}

void M5GFXRenderer::fill_triangle(int x0, int y0, int x1, int y1, int x2,
                                  int y2, uint8_t color) {
  M5.Display.fillTriangle(x0 + margin_left, y0 + margin_top, x1 + margin_left,
                          y1 + margin_top, x2 + margin_left, y2 + margin_top,
                          _grayTo565(color));
}

int M5GFXRenderer::get_text_width(const char *text, bool bold, bool italic) {
  // CRITICAL: Set state before querying metrics to avoid race with main thread
  M5.Display.setRotation(0); // Ensure Portrait for Reader
  M5.Display.setFont(_currentFont);
  M5.Display.setTextSize(_fontSize);
  return M5.Display.textWidth(text);
}

void M5GFXRenderer::draw_text(int x, int y, const char *text, bool bold,
                              bool italic) {
  // Simple text drawing.
  // Note: EPub renderer logic usually passes 'y' as the BASELINE or TOP?
  // Renderer.h says "ypos = y + get_line_height()".
  // M5GFX drawString uses top-left usually, or baseline depending on datum.
  // Default datum is top-left.

  // Apply state
  M5.Display.setRotation(0);
  M5.Display.setFont(_currentFont);
  M5.Display.setTextSize(_fontSize);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE); // Black text on white
  M5.Display.drawString(text, x + margin_left, y + margin_top);
}

void M5GFXRenderer::draw_image(const std::string &filename, const uint8_t *data,
                               size_t data_size, int x, int y, int width,
                               int height) {
  // Try to draw using M5GFX's native decoders
  // M5GFX automatically detects PNG/JPG headers if using drawPng/drawJpg
  // Note: 'data' is the raw file buffer

  // We ignore width/height scaling for now as M5GFX doesn't do arbitrary
  // scaling easily without sprite
  M5.Display.drawPng((uint8_t *)data, data_size, x + margin_left,
                     y + margin_top);
  // Fallback: drawJpg if fail? drawPng handles it?
  // Actually drawJpg handles JPG.
  if (data_size > 2 && data[0] == 0xFF && data[1] == 0xD8) {
    M5.Display.drawJpg((uint8_t *)data, data_size, x + margin_left,
                       y + margin_top);
  }
}

bool M5GFXRenderer::get_image_size(const std::string &filename,
                                   const uint8_t *data, size_t data_size,
                                   int *width, int *height) {
  // This is hard without a decoder.
  // Hack: Just return false or a dummy size so layout continues?
  // If we return false, Renderer.cpp logic defaulted to page width.
  // Let's rely on that fallback for now.
  // REAL FIX: Need a lightweight header parser for PNG/JPG.
  return false;
}

void M5GFXRenderer::needs_gray(uint8_t color) {
  // No-op for M5GFX auto-handling
}

bool M5GFXRenderer::has_gray() { return false; }

void M5GFXRenderer::show_busy() {
  // Optional: Draw hour glass
}

void M5GFXRenderer::show_img(int x, int y, int width, int height,
                             const uint8_t *img_buffer) {
  // Raw bitmap? Not used much in EPub logic usually, mostly for cover?
  // EPub logic usually calls draw_image (compressed).
}

void M5GFXRenderer::clear_screen() { M5.Display.fillScreen(TFT_WHITE); }

void M5GFXRenderer::flush_display() {
  // M5.Display handles updates automatically usually, or we can force push
  // For M5Paper, we might want to startWrite/endWrite or similar?
  // Actually, explicit update if needed:
  // M5.Display.display(); // (If using canvas)
  // On M5PaperS3 (IT8951), drawing commands usually queue/flush.
}

void M5GFXRenderer::flush_area(int x, int y, int width, int height) {
  // No-op
}

int M5GFXRenderer::get_page_width() {
  M5.Display.setRotation(0);
  return 540 - (margin_left + margin_right); // Use physical constant for safety
}

int M5GFXRenderer::get_page_height() {
  M5.Display.setRotation(0);
  return 960 - (margin_top + margin_bottom);
}

// ============================================================================
// SPACING CONFIGURATION
// ============================================================================
// To adjust WORD SPACING (width of space character):
// Modify the return value below. e.g. return M5.Display.textWidth(" ") * 1.5;
int M5GFXRenderer::get_space_width() {
  M5.Display.setRotation(0);
  M5.Display.setFont(_currentFont);
  M5.Display.setTextSize(_fontSize);
  return M5.Display.textWidth(" ");
}

// To adjust LINE SPACING:
// This is controlled by setLineSpacing() called from the UI.
// Use the READER_LINE_SPACING constants in ui_manager.cpp if available,
// or the _readerLineSpacing variable in UIManager.
// Base line height is fontHeight() + _lineSpacing.
int M5GFXRenderer::get_line_height() {
  M5.Display.setRotation(0);
  M5.Display.setFont(_currentFont);
  M5.Display.setTextSize(_fontSize);
  return M5.Display.fontHeight() + _lineSpacing;
}

void M5GFXRenderer::setLineSpacing(int spacing) { _lineSpacing = spacing; }

void M5GFXRenderer::setFontSize(float size) { _fontSize = size; }

void M5GFXRenderer::setFont(const m5gfx::IFont *font) { _currentFont = font; }

void M5GFXRenderer::reset() { clear_screen(); }
