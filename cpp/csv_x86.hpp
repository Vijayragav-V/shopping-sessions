#pragma once
// Called only after runtime ISA detection. Batches end on row boundaries.
// The delimiter and line-end indexes let the second pass decode fixed-width
// rows without a branch on the delimiter type for each individual field.
#if (defined(__x86_64__) || defined(__i386__)) &&                              \
    (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define SHOPPING_X86_CSV 1
inline bool wide_csv_supported() {
  return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi") &&
         __builtin_cpu_supports("popcnt");
}

template <size_t N, class F>
__attribute__((target("avx2,bmi,popcnt"))) void wide_csv_rows(Str data,
                                                              F &callback) {
  constexpr size_t batch = 8192;
  struct Line {
    uint16_t end, quotes;
  };
  uint16_t stops[batch + 1];
  Line lines[batch + 1];
  std::array<Str, N> fields;
  while (!data.empty()) {
    size_t n = std::min(data.size(), batch);
    if (n < data.size()) {
      while (n && data[n - 1] != '\n')
        --n;
      if (!n) {
        const size_t newline = data.find('\n', batch);
        const size_t bytes = newline == Str::npos ? data.size() : newline + 1;
        scalar_rows<N>(data.substr(0, bytes), callback);
        data.remove_prefix(bytes);
        continue;
      }
    }
    unsigned used = 0, line_count = 0, quote_total = 0;
    for (size_t at = 0; at < n; at += 32) {
      const char *block = data.data() + at;
      alignas(32) char tail[32]{};
      if (n - at < 32) {
        std::memcpy(tail, block, n - at);
        block = tail;
      }
      auto v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(block));
      auto line_ends = _mm256_cmpeq_epi8(v, _mm256_set1_epi8('\n'));
      unsigned lf = _mm256_movemask_epi8(line_ends);
      unsigned ends = _mm256_movemask_epi8(_mm256_or_si256(
          line_ends, _mm256_cmpeq_epi8(v, _mm256_set1_epi8(','))));
      unsigned quotes =
          _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8('"')));
      while (ends) {
        stops[used++] = static_cast<uint16_t>(at + __builtin_ctz(ends));
        ends &= ends - 1;
      }
      while (lf) {
        unsigned pos = __builtin_ctz(lf);
        lines[line_count++] = {
            static_cast<uint16_t>(at + pos),
            static_cast<uint16_t>(
                quote_total + __builtin_popcount(quotes & ((1u << pos) - 1)))};
        lf &= lf - 1;
      }
      quote_total += __builtin_popcount(quotes);
    }
    if (data[n - 1] != '\n') {
      stops[used++] = static_cast<uint16_t>(n);
      lines[line_count++] = {static_cast<uint16_t>(n),
                             static_cast<uint16_t>(quote_total)};
    }
    unsigned cut = 0, start = 0, previous_quotes = 0;
    for (unsigned r = 0; r < line_count; ++r) {
      auto line = lines[r];
      unsigned actual = line.quotes - previous_quotes;
      previous_quotes = line.quotes;
      if (used - cut >= N && stops[cut + N - 1] == line.end) {
        unsigned syntactic = 0;
        for (size_t col = 0; col < N; ++col) {
          unsigned end = stops[cut + col];
          const char *p = data.data() + start;
          unsigned len = end - start;
          bool quoted = len >= 2 && ((p[0] == '"') & (p[len - 1] == '"'));
          fields[col] = Str(p + quoted, len - 2 * quoted);
          syntactic += 2 * quoted;
          start = end + 1;
        }
        if (actual == syntactic)
          callback(fields);
        cut += N;
      } else {
        while (stops[cut++] != line.end) {
        }
        start = line.end + 1;
      }
    }
    data.remove_prefix(n);
  }
}
#endif
