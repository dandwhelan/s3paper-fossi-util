#pragma once

class Epub;
class Renderer;
class RubbishHtmlParser;

#include "./State.h"

class EpubReader {
private:
  EpubListItem &state;
  Epub *epub = nullptr;
  Renderer *renderer = nullptr;
  RubbishHtmlParser *parser = nullptr;

public:
  EpubReader(EpubListItem &state, Renderer *renderer)
      : state(state), renderer(renderer) {};
  ~EpubReader(); // Defined in EpubReader.cpp
  bool load();
  void parse_and_layout_current_section(); // Now public for background task
  void next();
  void prev();
  void render();
  void set_state_section(uint16_t current_section);
  void invalidateLayout();
  bool isLayoutValid() const { return parser != nullptr; }
};