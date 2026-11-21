#include "../cpp/input.hpp"
#include <cassert>
#include <random>

int main() {
  // Exercise vector block boundaries, missing terminal LF, malformed quotes,
  // variable field counts and arbitrary bytes. Compare every accepted field.
  std::mt19937 rng(173);
  const char alphabet[] = {'a',  'u',        ' ',        '\0',
                           '\r', char(0xc3), char(0xa9), '1'};
  for (int trial = 0; trial < 200; ++trial) {
    std::string data;
    for (int line = 0; line < 120; ++line) {
      int fields = rng() % 4 == 0 ? rng() % 10 : 7;
      for (int i = 0; i < fields; ++i) {
        if (i)
          data += ',';
        bool quoted = rng() % 3 == 0;
        if (quoted)
          data += '"';
        for (unsigned n = rng() % 35; n; --n) {
          data += rng() % 80 == 0 ? '"' : alphabet[rng() % sizeof(alphabet)];
        }
        if (quoted)
          data += '"';
      }
      if (line < 119 || trial % 2 == 0)
        data += '\n';
    }
    std::vector<std::array<Str, 7>> expected, actual;
    scalar_rows<7>(data, [&](auto row) { expected.push_back(row); });
    rows<7>(data, [&](auto row) { actual.push_back(row); });
    assert(actual == expected);
  }
  // Two separate pools exercise indirect-key equality and hashing.
  std::deque<std::string> names;
  KeyPool first, second;
  std::unordered_map<Key, Str, KeyHash> seen;
  for (int i = 0; i < 12000; ++i) {
    std::string name = i % 4 ? "u" : "U";
    for (unsigned n = rng() % 20; n; --n)
      name += char(rng() % 256);
    auto &stable = names.emplace_back(name);
    auto key = first.encode(stable, 'u');
    std::array<char, 9> buf;
    assert(key.user_text(buf) == stable);
    auto [it, inserted] = seen.emplace(key, stable);
    assert(inserted || it->second == stable);
    assert(seen.at(second.encode(stable, 'u')) == stable);
  }
}
