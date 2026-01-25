#include <string.h>
#ifndef UNIT_TEST
#include <esp_log.h>
#include <esp_system.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#define ESP_LOGD(args...)
#endif
#include "../Renderer/Renderer.h"
#include "../RubbishHtmlParser/RubbishHtmlParser.h"
#include "Epub.h"
#include "EpubReader.h"
#include <Arduino.h>

static const char *TAG = "EREADER";

EpubReader::~EpubReader() {
  // CRITICAL: Clean up allocated resources to prevent memory leaks
  // and allow opening another book
  Serial.println("EpubReader: Destructor called - cleaning up");
  if (parser) {
    delete parser;
    parser = nullptr;
  }
  if (epub) {
    delete epub;
    epub = nullptr;
  }
  Serial.println("EpubReader: Cleanup complete");
}

bool EpubReader::load() {
  ESP_LOGD(TAG, "Before epub load: %d", esp_get_free_heap_size());
  // do we need to load the epub?
  if (!epub || epub->get_path() != state.path) {
    renderer->show_busy();
    delete epub;
    delete parser;
    parser = nullptr;
    epub = new Epub(state.path);
    if (!epub->load()) {
      Serial.println("EREADER: Epub load failed!");
      return false;
    }

    // Debugging: Check spine and memory directly after load
    int spineCount = epub->get_spine_items_count();
    Serial.printf("EREADER: Epub load returned TRUE. Spine Items: %d\n",
                  spineCount);
    ESP_LOGD(TAG, "Heap after load: %d", esp_get_free_heap_size());

    if (spineCount > 0) {
      // Try to access the first item to see if it crashes or fails
      Serial.println("EREADER: Attempting to get first spine item...");
      try {
        std::string firstItem = epub->get_spine_item(0);
        Serial.printf("EREADER: First spine item: %s\n", firstItem.c_str());
      } catch (...) {
        Serial.println("EREADER: EXCEPTION accessing spine item 0");
      }
    } else {
      Serial.println("EREADER: ERROR - Spine is empty!");
    }

    ESP_LOGD(TAG, "After epub load verification: %d", esp_get_free_heap_size());
  }
  return true;
}

void EpubReader::parse_and_layout_current_section() {
  if (!parser) {
    renderer->show_busy();
    Serial.printf("EREADER: Parse and render section %d. Heap: %d\n",
                  state.current_section, ESP.getFreeHeap());
    ESP_LOGI(TAG, "Parse and render section %d", state.current_section);
    ESP_LOGD(TAG, "Before read html: %d", esp_get_free_heap_size());

    // Safety Check: Ensure section index is valid
    if (epub && state.current_section >= epub->get_spine_items_count()) {
      Serial.printf("EREADER: Error - Section %d out of bounds (Max: %d). "
                    "Resetting to 0.\n",
                    state.current_section, epub->get_spine_items_count());
      state.current_section = 0;
      state.current_page = 0;
    }

    std::string item = epub->get_spine_item(state.current_section);
    std::string base_path = item.substr(0, item.find_last_of('/') + 1);
    char *html = reinterpret_cast<char *>(epub->get_item_contents(item));
    Serial.printf("EREADER: HTML Content loaded. Size: %d. Heap: %d\n",
                  strlen(html), ESP.getFreeHeap());

    parser = new RubbishHtmlParser(html, strlen(html), base_path);
    free(html);
    Serial.printf("EREADER: Parser created. Heap: %d. Starting layout...\n",
                  ESP.getFreeHeap());

    parser->layout(renderer, epub);
    Serial.printf("EREADER: Layout finished. Heap: %d\n", ESP.getFreeHeap());
    state.pages_in_current_section = parser->get_page_count();
  }
}

void EpubReader::set_state_section(uint16_t current_section) {
  state.current_section = current_section;
}

void EpubReader::invalidateLayout() {
  if (parser) {
    delete parser;
    parser = nullptr;
  }
}

void EpubReader::next() {
  state.current_page++;
  if (state.current_page >= state.pages_in_current_section) {
    // Check if there IS a next section
    if (epub && state.current_section + 1 < epub->get_spine_items_count()) {
      state.current_section++;
      state.current_page = 0;
      delete parser;
      parser = nullptr;
    } else {
      // End of book - Start over or stay on last page?
      // Let's stay on last page
      state.current_page = state.pages_in_current_section - 1;
      Serial.println("EREADER: End of book reached.");
    }
  }
}

void EpubReader::prev() {
  if (state.current_page == 0) {
    if (state.current_section > 0) {
      delete parser;
      parser = nullptr;
      state.current_section--;
      ESP_LOGD(TAG, "Going to previous section %d", state.current_section);
      parse_and_layout_current_section();
      state.current_page = state.pages_in_current_section - 1;
      return;
    }
  }
  state.current_page--;
}

void EpubReader::render() {
  if (!parser) {
    parse_and_layout_current_section();
  }
  ESP_LOGD(TAG, "rendering page %d of %d", state.current_page,
           parser->get_page_count());
  parser->render_page(state.current_page, renderer, epub);
}