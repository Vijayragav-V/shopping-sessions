#pragma once
#include <span>
template <class T> struct PartitionBucket {
  static_assert(std::is_trivially_destructible_v<T>);
  T *data{};
  size_t used{}, limit{};
  std::vector<T> tail;
  size_t size() const { return used + tail.size(); }
  size_t capacity() const { return limit + tail.capacity(); }
  T &operator[](size_t i) { return i < used ? data[i] : tail[i - used]; }
  auto spans() {
    return std::array{std::span<T>(data, used), std::span<T>(tail)};
  }
  template <class... Args> void emplace_back(Args &&...args) {
    if (used < limit)
      std::construct_at(data + used++, std::forward<Args>(args)...);
    else
      tail.emplace_back(std::forward<Args>(args)...);
  }
  void push_back(T v) { emplace_back(v); }
  void release() { std::vector<T>().swap(tail); }
};
template <class T> struct PartitionSlab {
  std::array<PartitionBucket<T>, shards> buckets;
  void *region = MAP_FAILED;
  size_t bytes{};
  PartitionSlab() = default;
  PartitionSlab(const PartitionSlab &) = delete;
  void reserve(size_t count) {
    if (!count)
      return;
    size_t page = 4096;
    size_t stride = (count * sizeof(T) + page - 1) / page * page;
    bytes = stride * shards;
    region = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED)
      return;
#ifdef MADV_HUGEPAGE
    madvise(region, bytes, MADV_HUGEPAGE);
#endif
    for (size_t i = 0; i < shards; ++i) {
      buckets[i].data =
          reinterpret_cast<T *>(static_cast<char *>(region) + i * stride);
      buckets[i].limit = stride / sizeof(T);
    }
  }
  void release() {
    if (region != MAP_FAILED) {
      munmap(region, bytes);
      region = MAP_FAILED;
    }
  }
  ~PartitionSlab() { release(); }
  auto &operator[](size_t i) { return buckets[i]; }
  const auto &operator[](size_t i) const { return buckets[i]; }
  auto begin() const { return buckets.begin(); }
  auto end() const { return buckets.end(); }
};
