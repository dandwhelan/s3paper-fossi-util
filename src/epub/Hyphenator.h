#ifndef HYPHENATOR_H
#define HYPHENATOR_H

#include <cstdint>
#include <string>
#include <vector>

class Hyphenator {
public:
  // Returns a list of indices where the word can be split.
  // e.g. "example" -> [2, 4] corresponding to "ex-am-ple"
  static std::vector<int> get_hyphens(const std::string &word);
};

#endif // HYPHENATOR_H
