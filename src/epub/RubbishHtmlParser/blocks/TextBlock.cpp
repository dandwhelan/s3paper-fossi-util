#include "TextBlock.h"
#include <Arduino.h>
#include <cstring>
#include <esp_log.h>
#include <esp_system.h> // for esp_get_free_heap_size
#include <freertos/FreeRTOS.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static const char *TAG = "TextBlock";

#ifndef UNIT_TEST
// Headers already included above
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGD(args...)
#define ESP_LOGW(args...)
#endif

// TODO - is there any more whitespace we should consider?
static bool is_whitespace(char c) {
  return (c == ' ' || c == '\r' || c == '\n');
}

// move past anything that should be considered part of a work
static int skip_word(const char *text, int index, int length) {
  while (index < length && !is_whitespace(text[index])) {
    index++;
  }
  return index;
}

// skip past any white space characters
static int skip_whitespace(const char *html, int index, int length) {
  while (index < length && is_whitespace(html[index])) {
    index++;
  }
  return index;
}

void TextBlock::add_span(const char *span, bool is_bold, bool is_italic) {
  // adding a span to text block
  // make a copy of the text as we'll modify it
  int length = strlen(span);
  char *text = new char[length + 1];
  strcpy(text, span);
  spans.push_back(text);
  // work out where each word is in the span
  int index = 0;
  while (index < length) {
    // skip past any whitespace to the start of a word
    index = skip_whitespace(span, index, length);
    int word_start = index;
    // find the end of the word
    index = skip_word(span, index, length);
    int word_length = index - word_start;
    if (word_length > 0) {
      // null terminate the word
      text[word_start + word_length] = '\0';
      // store the information about the word for later
      words.push_back(text + word_start);
      // store the style for the word
      word_styles.push_back((is_bold ? BOLD_SPAN : 0) |
                            (is_italic ? ITALIC_SPAN : 0));
    }
  }
}
// given a renderer works out where to break the words into lines
#include "../../Hyphenator.h"

void TextBlock::layout(Renderer *renderer, Epub *epub, int max_width) {
  if (words.empty()) {
    return;
  }

  // 1. Build Atoms
  atoms.clear();
  int space_width = renderer->get_space_width();
  int hyphen_width = renderer->get_text_width("-", 0, 0); // approx hyphen width

  for (size_t i = 0; i < words.size(); i++) {
    std::string wordStr = words[i];
    int wlen = wordStr.length();
    uint8_t style = word_styles[i];

    std::vector<int> splits = Hyphenator::get_hyphens(wordStr);

    int last_split = 0;
    for (int split : splits) {
      if (split > last_split && split < wlen) {
        LayoutAtom atom;
        atom.textPtr = words[i] + last_split;
        atom.length = split - last_split;
        atom.style = style;
        atom.is_word_end = false;

        // Measure this chunk
        // Note: we need a temporary null-terminated string for measurement
        // or a version of get_text_width that takes length.
        // Assuming we need temp string.
        std::string chunk = wordStr.substr(last_split, split - last_split);
        atom.width = renderer->get_text_width(chunk.c_str(), style & BOLD_SPAN,
                                              style & ITALIC_SPAN);
        // Store x_pos later
        atom.x_pos = 0;

        atoms.push_back(atom);
        last_split = split;
      }
    }

    // Last chunk (or whole word)
    if (last_split < wlen) {
      LayoutAtom atom;
      atom.textPtr = words[i] + last_split;
      atom.length = wlen - last_split;
      atom.style = style;
      atom.is_word_end = true;

      std::string chunk = wordStr.substr(last_split, wlen - last_split);
      atom.width = renderer->get_text_width(chunk.c_str(), style & BOLD_SPAN,
                                            style & ITALIC_SPAN);
      atom.x_pos = 0;

      atoms.push_back(atom);
    }
  }

  // 2. Run DP Layout on Atoms
  int n = atoms.size();
  int page_width = (max_width > 0) ? max_width : renderer->get_page_width();

  std::vector<int> dp(n, INT_MAX);
  std::vector<int> ans(n, 0);

  dp[n - 1] = 0;
  ans[n - 1] = n - 1;

  for (int i = n - 2; i >= 0; i--) {
    if (i % 100 == 0)
      vTaskDelay(1);

    int currlen = -1;
    dp[i] = INT_MAX;
    ans[i] = i;

    for (int j = i; j < n; j++) {
      int atom_w = atoms[j].width;
      int spacing = 0;

      if (j > i) {
        // Spacing logic:
        // If previous atom was word end, add space width.
        // If previous atom was NOT word end (soft split), add 0.
        if (atoms[j - 1].is_word_end) {
          spacing = space_width;
        }
      }

      currlen += atom_w + spacing;

      // Width check with Hyphen consideration
      int actual_len = currlen;
      if (!atoms[j].is_word_end) {
        // If we break here, we need to add hyphen width
        actual_len += hyphen_width;
      }

      if (actual_len > page_width) {
        // Overflow
        if (j == i) {
          // Single atom too big
          ans[i] = j;
          int cost =
              (j == n - 1)
                  ? 0
                  : ((actual_len - page_width) * (actual_len - page_width) +
                     dp[j + 1]);
          if (cost < dp[i]) {
            dp[i] = cost;
            ans[i] = j;
          }
        }
        break;
      }

      int cost;
      if (j == n - 1) {
        cost = 0;
      } else {
        cost =
            (page_width - actual_len) * (page_width - actual_len) + dp[j + 1];
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;
      }
    }
  }

  // 3. Reconstruct Lines
  line_breaks.clear();
  int i = 0;
  while (i < n) {
    i = ans[i] + 1;
    line_breaks.push_back(i);
    if (line_breaks.size() > 2000)
      break; // Safety
  }

  // 4. Calculate X Positions
  int start_atom = 0;
  for (size_t l = 0; l < line_breaks.size(); l++) {
    int end_atom = line_breaks[l];
    int line_content_width = 0;
    int word_gaps = 0;

    for (int k = start_atom; k < end_atom; k++) {
      line_content_width += atoms[k].width;
      if (k > start_atom) {
        if (atoms[k - 1].is_word_end) {
          line_content_width += space_width;
          word_gaps++;
        }
      }
    }

    // If broken at soft hyphen, add hyphen width to line content
    bool broken_at_hyphen = (end_atom < n && !atoms[end_atom - 1].is_word_end);
    if (broken_at_hyphen) {
      line_content_width += hyphen_width;
    }

    float spare = page_width - line_content_width;
    float extra_spacing = 0;

    // Justify if not last line
    if (l != line_breaks.size() - 1 && style == JUSTIFIED && word_gaps > 0) {
      extra_spacing = spare / word_gaps;
    }

    // Position atoms
    float x = 0;
    if (style == CENTER_ALIGN)
      x = spare / 2;
    else if (style == RIGHT_ALIGN)
      x = spare;

    for (int k = start_atom; k < end_atom; k++) {
      if (k > start_atom) {
        if (atoms[k - 1].is_word_end) {
          x += space_width + extra_spacing;
        }
      }
      atoms[k].x_pos = (uint16_t)x;
      x += atoms[k].width;
    }

    start_atom = end_atom;
  }
}

void TextBlock::render(Renderer *renderer, int line_break_index, int x_pos,
                       int y_pos) {
  if (line_break_index >= line_breaks.size())
    return;

  int start = (line_break_index == 0) ? 0 : line_breaks[line_break_index - 1];
  int end = line_breaks[line_break_index];

  for (int i = start; i < end; i++) {
    LayoutAtom &atom = atoms[i];

    // We need to render substring
    // Using string allocation here is not ideal for performance but cleanest
    // given we have textptr + length.
    // Optimization: renderer->draw_text usually takes string.
    // We can create a temp buffer on stack.
    char buffer[64];
    int safe_len = (atom.length < 63) ? atom.length : 63;
    strncpy(buffer, atom.textPtr, safe_len);
    buffer[safe_len] = '\0';

    renderer->draw_text(x_pos + atom.x_pos, y_pos, buffer,
                        atom.style & BOLD_SPAN, atom.style & ITALIC_SPAN);

    // Draw hyphen if needed
    if (i == end - 1 && !atom.is_word_end) {
      // This is the last atom of line AND it's not a word end -> Soft Hyphen
      // split
      int hyphen_x = atom.x_pos + atom.width;
      renderer->draw_text(x_pos + hyphen_x, y_pos, "-", atom.style & BOLD_SPAN,
                          atom.style & ITALIC_SPAN);
    }
  }
}

void TextBlock::dump() {
  ESP_LOGI(TAG, "TextBlock Dump: %d atoms", atoms.size());
}
