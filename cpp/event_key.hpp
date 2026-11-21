#pragma once
// Adapted from buildathon-tests 4c136a5, src/keyrec.rs. Event equality needs
// no person inference, population limits or knowledge of a generator seed.
inline Key compact_event_key(uint64_t word, unsigned width) {
  constexpr uint64_t ones = 0x0101010101010101ULL, high = ones * 128;
  constexpr uint64_t mask40 = (uint64_t{1} << 40) - 1;
  if (width > 1 && uint8_t(word) == '0')
    return {};
  const unsigned shift = 8 * (8 - width);
  word = (word << shift) | ((ones * '0') & ((uint64_t{1} << shift) - 1));
  if (word & high)
    return {};
  auto ge = [&](unsigned c) { return (word + ones * (128 - c)) & high; };
  const auto letters = ge('a');
  if (((ge('0') & ~ge(':')) | (letters & ~ge('{'))) != high)
    return {};
  const auto digits = word - ones * '0' - (letters >> 7) * 39;
  constexpr uint64_t bytes = 0x00ff00ff00ff00ffULL, pairs = 0x0000ffff0000ffffULL;
  const auto d2 = (digits & bytes) * 36 + ((digits >> 8) & bytes);
  const auto d4 = (d2 & pairs) * 1296 + ((d2 >> 16) & pairs);
  const uint64_t value = uint64_t(uint32_t(d4)) * 1679616 + (d4 >> 32);
  if (value > mask40)
    return {};
  constexpr uint64_t inverse = [] {
    uint64_t x = 1;
    for (int i = 0; i < 6; ++i) x *= 2 - 0xd6e8feb86659ULL * x;
    return x;
  }();
  const auto x = (value * inverse) & mask40;
  const auto code = x ^ (x >> 17) ^ (x >> 34);
  // The low 40 bits are bijective; retain independent high bits for tags.
  return {uintptr_t(((code | ((value & 0x3fffff) << 40)) + 1) << 1)};
}

inline Key event_key(KeyPool &pool, Str id) {
  if constexpr (sizeof(uintptr_t) == 8 && std::endian::native == std::endian::little) {
    if (id.size() >= 2 && id.size() <= 9 && id.front() == 'e') {
      uint64_t word = 0;
      std::memcpy(&word, id.data() + 1, id.size() - 1);
      if (Key k = compact_event_key(word, unsigned(id.size() - 1)); k.bits)
        return k;
    }
  }
  pool.other.push_back({id, std::hash<Str>{}(id)});
  return {reinterpret_cast<uintptr_t>(&pool.other.back()) | 1};
}
