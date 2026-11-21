#pragma once
#include "metrics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using Str = std::string_view;
using Time = int64_t;
constexpr Time day_ms = 86400000, reach = 7 * day_ms, bad = INT64_MIN;
template <class V> using Dict = std::unordered_map<Str, V>;

struct Input {
  std::vector<Str> mappings;
  ~Input() {
    for (auto s : mappings)
      munmap(const_cast<char *>(s.data()), s.size());
  }
  Str map(const fs::path &p) {
    int fd = open(p.c_str(), O_RDONLY);
    if (fd < 0)
      throw std::runtime_error("open " + p.string());
    struct stat st{};
    if (fstat(fd, &st)) {
      close(fd);
      throw std::runtime_error("stat " + p.string());
    }
    if (!st.st_size) {
      close(fd);
      return {};
    }
    auto ptr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED)
      throw std::runtime_error("mmap " + p.string());
    mappings.emplace_back(static_cast<const char *>(ptr), st.st_size);
    return mappings.back();
  }
  std::vector<Str> parts(const fs::path &dir) {
    std::vector<Str> chunks;
    for (auto &file : fs::directory_iterator(dir)) {
      if (file.path().extension() != ".csv")
        continue;
      Str data = map(file.path());
      auto nl = data.find('\n');
      if (nl == Str::npos)
        continue;
      data.remove_prefix(nl + 1);
      while (!data.empty()) {
        auto split = data.find('\n', 512 * 1024);
        auto n = split == Str::npos ? data.size() : split + 1;
        chunks.push_back(data.substr(0, n));
        data.remove_prefix(n);
      }
    }
    return chunks;
  }
};

template <size_t N, class F> void scalar_rows(Str data, F &&callback) {
  std::array<Str, N> fields;
  while (!data.empty()) {
    size_t end = data.find('\n');
    Str line = data.substr(0, end);
    data.remove_prefix(line.size() + (end != Str::npos));
    for (size_t i = 0; i < N; ++i) {
      auto comma = line.find(',');
      if ((comma == Str::npos) != (i + 1 == N))
        break;
      Str s = line.substr(0, comma);
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s.remove_prefix(1);
        s.remove_suffix(1);
      }
      if (s.find('"') != Str::npos)
        break;
      fields[i] = s;
      if (i + 1 == N)
        callback(fields);
      else
        line.remove_prefix(comma + 1);
    }
  }
}

#if defined(__SSE2__)
#include <emmintrin.h>
inline std::pair<unsigned, unsigned> delimiters(const char *p) {
  auto v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
  auto ends = _mm_or_si128(_mm_cmpeq_epi8(v, _mm_set1_epi8(',')),
                           _mm_cmpeq_epi8(v, _mm_set1_epi8('\n')));
  return {_mm_movemask_epi8(ends),
          _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_set1_epi8('"')))};
}
#elif defined(__aarch64__)
#include <arm_neon.h>
inline unsigned mask(uint8x16_t matches) {
  constexpr uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                   1, 2, 4, 8, 16, 32, 64, 128};
  auto bits = vandq_u8(matches, vld1q_u8(weights));
  return vaddv_u8(vget_low_u8(bits)) |
         (unsigned(vaddv_u8(vget_high_u8(bits))) << 8);
}
inline std::pair<unsigned, unsigned> delimiters(const char *p) {
  auto v = vld1q_u8(reinterpret_cast<const uint8_t *>(p));
  auto ends =
      vorrq_u8(vceqq_u8(v, vdupq_n_u8(',')), vceqq_u8(v, vdupq_n_u8('\n')));
  return {mask(ends), mask(vceqq_u8(v, vdupq_n_u8('"')))};
}
#endif
template <size_t N, class F> void parse_rows(Str data, F &&callback) {
#if defined(__SSE2__) || defined(__aarch64__)
  if (data.empty())
    return;
  std::array<Str, N> fields;
  size_t column = 0, actual_quotes = 0;
  std::array<const char*,N> ends_of_fields;
  const char *row_start = data.data();
  auto finish_row = [&] {
    if(column!=N) return;
    const char* start=row_start;
    size_t expected=0;
    for(size_t i=0;i<N;++i) {
      const char* end=ends_of_fields[i];
      size_t len=end-start;
      bool quoted=len>=2 && ((start[0]=='"') & (end[-1]=='"'));
      fields[i]=Str(start+quoted,len-2*quoted);
      expected+=2*quoted;
      if(i+1<N) start=end+1;
    }
    if(expected==actual_quotes) callback(fields);
  };
  for (size_t off = 0; off < data.size(); off += 16) {
    const char *base = data.data() + off;
    char tail[16]{};
    const char *block = base;
    if (data.size() - off < 16) {
      std::memcpy(tail, base, data.size() - off);
      block = tail;
    }
    auto [ends, quotes] = delimiters(block);
    while (ends) {
      unsigned at = std::countr_zero(ends);
      ends &= ends - 1;
      if(column<N) ends_of_fields[column]=base+at;
      ++column;
      if (base[at] == '\n') {
        unsigned before = (1u << at) - 1;
        actual_quotes += std::popcount(quotes & before);
        quotes &= ~before;
        finish_row();
        row_start=base+at+1;
        column = actual_quotes = 0;
      }
    }
    actual_quotes += std::popcount(quotes);
  }
  if (data.back() != '\n') {
    if(column<N) ends_of_fields[column]=data.data()+data.size();
    ++column;
    finish_row();
  }
#else
  scalar_rows<N>(data, callback);
#endif
}
template <size_t N, class F> void rows(Str data, F &&callback) {
#ifdef SHOPPING_DIAGNOSTICS
  // Instrument all callbacks in one of every 32 chunks on each worker.
  // Report the sample itself so estimates can be scaled by input coverage.
  static thread_local unsigned chunk_count = 0;
  metrics::local.csv_bytes += data.size();
  if (chunk_count++ % 32 == 0) {
    auto start = metrics::now();
    int64_t callbacks = 0;
    parse_rows<N>(data, [&](auto &r) {
      auto entry = metrics::now();
      callback(r);
      callbacks += metrics::now() - entry;
    });
    metrics::local.csv_sample += data.size();
    metrics::local.csv_ns += metrics::now() - start - callbacks;
    return;
  }
#endif
  parse_rows<N>(data, callback);
}

inline Time number(Str s, Time limit) {
  Time n = 0;
  if (s.empty())
    return bad;
  if (s.size() <= 18) {
    for (unsigned char c : s) {
      unsigned d = unsigned(c) - '0';
      if (d > 9)
        return bad;
      n = n * 10 + d;
    }
    return n <= limit ? n : bad;
  }
  for (unsigned char c : s) {
    unsigned d = unsigned(c) - '0';
    if (d > 9 || n > (limit - static_cast<Time>(d)) / 10)
      return bad;
    n = n * 10 + d;
  }
  return n;
}
inline int pair_digits(const char *p) {
  unsigned a = unsigned(static_cast<unsigned char>(p[0])) - '0';
  unsigned b = unsigned(static_cast<unsigned char>(p[1])) - '0';
  return (a | b) < 16 && a < 10 && b < 10 ? int(10 * a + b) : -1000;
}
inline Time midnight(const char *p) {
  struct CachedDay {
    uint64_t prefix{};
    uint16_t suffix{};
    Time result = bad;
  };
  static thread_local std::array<CachedDay, 64> cache{};
  uint64_t prefix;
  uint16_t suffix;
  std::memcpy(&prefix, p, 8);
  std::memcpy(&suffix, p + 8, 2);
  auto &hit = cache[(unsigned(uint8_t(p[8])) * 11 + uint8_t(p[9])) % 64];
  if (hit.prefix == prefix && hit.suffix == suffix)
    return hit.result;
  int century = pair_digits(p), yy = pair_digits(p + 2), m = pair_digits(p + 5),
      d = pair_digits(p + 8);
  int y = century * 100 + yy;
  if (p[4] != '-' || p[7] != '-' || century < 0 || yy < 0 || m < 1 || m > 12 ||
      d < 1)
    return bad;
  constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (d >
      lengths[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
    return bad;
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400, year = y - era * 400;
  int days = era * 146097 + year * 365 + year / 4 - year / 100 +
             (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1 - 719468;
  hit = {prefix, suffix, days * day_ms};
  return hit.result;
}
inline Time timestamp(Str s) {
  if ((s.size() != 20 && s.size() != 25) || s[4] != '-')
    return number(s, INT64_MAX);
  auto p = s.data();
  if (s[10] != 'T' || s[13] != ':' || s[16] != ':')
    return bad;
  int h = pair_digits(p + 11), minute = pair_digits(p + 14),
      sec = pair_digits(p + 17);
  if (unsigned(h) > 23 || unsigned(minute) > 59 || unsigned(sec) > 59)
    return bad;
  int offset = 0;
  if (s.size() == 25) {
    int oh = pair_digits(p + 20), om = pair_digits(p + 23);
    if ((p[19] != '+' && p[19] != '-') || p[22] != ':' || unsigned(oh) > 23 ||
        unsigned(om) > 59)
      return bad;
    offset = (oh * 60 + om) * (p[19] == '+' ? 1 : -1);
  } else if (p[19] != 'Z')
    return bad;
  auto day = midnight(p);
  return day == bad ? bad
                    : day + ((h * 60 + minute - offset) * 60 + sec) * 1000LL;
}
inline Time ingest_time(Str s) {
  if (s.size() != 20)
    return timestamp(s);
  // The contract guarantees F1 for ingest_ts.
  auto pair = [&](size_t i) { return (s[i] - '0') * 10 + s[i + 1] - '0'; };
  auto day = midnight(s.data());
  return day == bad
             ? bad
             : day + ((pair(11) * 60 + pair(14)) * 60 + pair(17)) * 1000LL;
}
// Common identifiers contain a prefix and up to eight suffix bytes. Encode
// them losslessly inline. Other byte strings refer to stable, borrowed views.
// Domains (event IDs, user IDs) never compare keys from different prefixes.
struct Key {
  uintptr_t bits{};
  bool indirect() const { return bits & 1; }
  Str view() const { return *reinterpret_cast<const Str *>(bits - 1); }
  bool operator==(Key b) const {
    return bits == b.bits || (indirect() && b.indirect() && view() == b.view());
  }
  size_t hash() const {
    if (indirect())
      return std::hash<Str>{}(view());
    uint64_t n = bits;
    n ^= n >> 30;
    n *= 0xbf58476d1ce4e5b9ULL;
    n ^= n >> 27;
    n *= 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
  }
  Str user_text(std::array<char, 9> &buffer) const {
    if (indirect())
      return view();
    uint64_t n = bits >> 1;
    buffer[0] = 'u';
    std::memcpy(buffer.data() + 1, &n, 8);
    return {buffer.data(), size_t(1 + (std::bit_width(n) + 7) / 8)};
  }
};
struct KeyHash {
  size_t operator()(Key k) const { return k.hash(); }
};
struct KeyPool {
  std::deque<Str> other;
  Key encode(Str s, char prefix) {
    if constexpr (sizeof(uintptr_t) == 8 &&
                  std::endian::native == std::endian::little) {
      if (s.size() >= 2 && s.size() <= 9 && s.front() == prefix &&
          s.back() != 0) {
        uint64_t n = 0;
        if (s.size() == 9)
          std::memcpy(&n, s.data() + 1, 8);
        else
          std::memcpy(&n, s.data() + 1, s.size() - 1);
        if (!(n >> 63))
          return {uintptr_t(n << 1)};
      }
    }
    other.push_back(s);
    static_assert(alignof(Str) >= 2);
    return {reinterpret_cast<uintptr_t>(&other.back()) | 1};
  }
};
struct Identities {
  struct Slot {
    Key key;
    uintptr_t value{};
  };
  std::vector<Slot> slots = std::vector<Slot>(256);
  std::vector<int> parent, sizes;
  std::vector<Str> names;
  std::vector<Key> canonical;
  KeyPool pool;
  size_t locate(Key k) const {
    auto i = k.hash() & (slots.size() - 1);
    while (slots[i].key.bits && !(slots[i].key == k))
      i = (i + 1) & (slots.size() - 1);
    return i;
  }
  int id(Str s) {
    if (parent.size() * 3 >= slots.size() * 2) {
      auto old = std::move(slots);
      slots.resize(old.size() * 2);
      for (auto slot : old)
        if (slot.key.bits)
          slots[locate(slot.key)] = slot;
    }
    auto key = pool.encode(s, 'u');
    auto &slot = slots[locate(key)];
    if (!slot.key.bits) {
      slot = {key, parent.size()};
      parent.push_back(slot.value);
      sizes.push_back(1);
      names.push_back(s);
      canonical.push_back(key);
    }
    return slot.value;
  }
  int root(int x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  }
  void join(Str a, Str b) {
    if (a.empty() || b.empty())
      return;
    int x = root(id(a)), y = root(id(b));
    if (x == y)
      return;
    if (sizes[x] < sizes[y])
      std::swap(x, y);
    parent[y] = x;
    sizes[x] += sizes[y];
    if (names[y] < names[x]) {
      names[x] = names[y];
      canonical[x] = canonical[y];
    }
  }
  void finish() {
    for (auto &slot : slots)
      if (slot.key.bits)
        slot.value = canonical[root(slot.value)].bits;
    std::vector<int>().swap(parent);
    std::vector<int>().swap(sizes);
    std::vector<Str>().swap(names);
    std::vector<Key>().swap(canonical);
  }
  Key resolve(Key key) const {
    auto slot = slots[locate(key)];
    return slot.key.bits ? Key{slot.value} : key;
  }
};

template <class F> void parallel(size_t jobs, unsigned threads, F &&f) {
  std::atomic<size_t> next{0};
  std::exception_ptr error;
  std::mutex lock;
  std::vector<std::jthread> workers;
  for (unsigned t = 0; t < std::min<size_t>(threads, jobs); ++t)
    workers.emplace_back([&, t] {
      try {
        for (size_t i;
             (i = next.fetch_add(1, std::memory_order_relaxed)) < jobs;)
          f(i, t);
      } catch (...) {
        std::lock_guard guard(lock);
        error = std::current_exception();
        next.store(jobs);
      }
    });
  workers.clear();
  if (error)
    std::rethrow_exception(error);
}
