#pragma once

// Each worker gives up its revision storage after all duplicate winners are known.
// The storage remains mapped until every linked-person timeline is finished.
struct TimelineRegion {
  std::byte *base{};
  size_t bytes{};
  std::atomic<size_t> offset{};
  void *take(size_t need) {
    if (!base || !need || need > SIZE_MAX - 63)
      return nullptr;
    size_t aligned = (need + 63) & ~size_t(63),
           start = offset.load(std::memory_order_relaxed);
    while (aligned <= bytes - start) {
      if (offset.compare_exchange_weak(start, start + aligned,
                                       std::memory_order_relaxed))
        return base + start;
    }
    return nullptr;
  }
};

// All survivors are written exactly once before any timeline is read.
// Placement array new starts every element's lifetime in the previously
// allocated revision pages. Only trivial destructors are supported here.
template <class T> struct TimelineArray {
  static_assert(std::is_trivially_destructible_v<T> && alignof(T) <= 64);
  T *data{};
  size_t size{};
  std::unique_ptr<T[]> owned;
  void prepare(size_t count, std::span<TimelineRegion> regions,
               unsigned owner) {
    size = count;
    if (!count)
      return;
    if (count > SIZE_MAX / sizeof(T))
      throw std::bad_array_new_length();
    void *memory = nullptr;
#if SHOP_REUSE_REGION
    memory = regions[owner].take(count * sizeof(T));
#if SHOP_LEND_REGION
    for (unsigned j = 1; !memory && j < regions.size(); ++j)
      memory = regions[(owner + j) % regions.size()].take(count * sizeof(T));
#endif
#else
    (void)regions;
    (void)owner;
#endif
    if (memory)
      data = ::new (memory) T[count];
    else {
      owned = std::make_unique_for_overwrite<T[]>(count);
      data = owned.get();
    }
  }
  T &operator[](size_t i) { return data[i]; }
  std::span<T> view() { return {data, size}; }
};
