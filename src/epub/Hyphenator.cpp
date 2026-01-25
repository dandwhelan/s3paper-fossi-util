#include "Hyphenator.h"
#include <algorithm>
#include <cctype>
#include <cstring>


// Basic English patterns (simplified for embedded use)
// A full implementation would use Liang's algorithm with patterns.
// Here we use some simple heuristic rules + suffix matching.

bool is_vowel(char c) {
  c = tolower(c);
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

std::vector<int> Hyphenator::get_hyphens(const std::string &word) {
  std::vector<int> splits;
  int len = word.length();

  // Don't hyphenate short words
  if (len < 5)
    return splits;

  // Heuristic 1: Pre-defined suffixes
  const char *suffixes[] = {"tion", "ing",   "ment", "ness", "able",
                            "ible", "fully", "less", "ship"};

  for (const char *suffix : suffixes) {
    int slen = strlen(suffix);
    if (len > slen + 2) { // Ensure stem is at least 3 chars
      if (word.substr(len - slen) == suffix) {
        splits.push_back(len - slen);
        // We could continue processing the stem, but let's keep it simple for
        // now
        return splits;
      }
    }
  }

  // Heuristic 2: Double consonants (bat-ter, let-ter)
  for (int i = 2; i < len - 2; i++) {
    if (word[i] == word[i + 1] && !is_vowel(word[i])) {
      splits.push_back(i + 1);
    }
  }

  // Sort splits just in case we add more logic
  std::sort(splits.begin(), splits.end());

  // Remove duplicates
  splits.erase(std::unique(splits.begin(), splits.end()), splits.end());

  return splits;
}
