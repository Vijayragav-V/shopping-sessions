#pragma once
template <class Value> struct ControlIndex {
  static constexpr size_t width = 16;
  std::vector<uint8_t> tags;
  std::unique_ptr<Value[]> values;
  explicit ControlIndex(size_t size) { reset(size); }
  void reset(size_t n) {
    n = std::max(n, width);
    tags.assign(n, 0);
    values = std::make_unique_for_overwrite<Value[]>(n);
  }
  static uint8_t fingerprint(uint64_t h) {
    return uint8_t(1 + ((h ^ (h >> 33)) >> 47 & 127));
  }
  static std::pair<unsigned, unsigned> masks(const uint8_t *base, uint8_t tag) {
#if defined(__SSE2__)
    auto group = _mm_loadu_si128(reinterpret_cast<const __m128i *>(base));
    return {unsigned(_mm_movemask_epi8(
                _mm_cmpeq_epi8(group, _mm_set1_epi8(char(tag))))),
            unsigned(
                _mm_movemask_epi8(_mm_cmpeq_epi8(group, _mm_setzero_si128())))};
#elif defined(__aarch64__)
    auto group = vld1q_u8(base);
    return {mask(vceqq_u8(group, vdupq_n_u8(tag))),
            mask(vceqq_u8(group, vdupq_n_u8(0)))};
#else
    unsigned hit = 0, empty = 0;
    for (unsigned i = 0; i < width; ++i) {
      hit |= unsigned(base[i] == tag) << i;
      empty |= unsigned(!base[i]) << i;
    }
    return {hit, empty};
#endif
  }
  template <class Match>
  std::pair<size_t, bool> locate(uint64_t hash, Match equal) const {
    size_t group = ((hash / shards) & (tags.size() / width - 1)) * width;
    uint8_t tag = fingerprint(hash);
    for (;;) {
      auto [matches, empty] = masks(tags.data() + group, tag);
      while (matches) {
        size_t slot = group + std::countr_zero(matches);
        if (equal(values[slot]))
          return {slot, true};
        matches &= matches - 1;
      }
      if (empty)
        return {group + std::countr_zero(empty), false};
      group = (group + width) & (tags.size() - 1);
    }
  }
  void insert(size_t slot, uint64_t hash, uint64_t value) {
    values[slot] = value;
    tags[slot] = fingerprint(hash);
  }
};
