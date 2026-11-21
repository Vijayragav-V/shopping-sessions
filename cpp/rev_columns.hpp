#pragma once
// Store the event IDs and revision payloads in separate arrays within each
// block. Only identical IDs require an ingest comparison or a raw event
// location.
template <size_t Width> struct RevisionColumns {
  struct Block {
    uintptr_t keys[Width];
    std::array<uint32_t, 2> data[Width];
  };
  struct Bin {
    Block *blocks{};
    size_t used{}, limit{};
    std::vector<Block> more;
    size_t size() const { return used; }
    size_t capacity() const { return limit + more.capacity() * Width; }
    const Block &block(size_t i) const {
      return i < limit ? blocks[i / Width] : more[(i - limit) / Width];
    }
    Key key(size_t i) const { return {block(i).keys[i % Width]}; }
    RevisionIndex operator[](size_t i) const {
      const auto &b = block(i);
      auto j = i % Width;
      return {Key{b.keys[j]}, b.data[j][0], b.data[j][1]};
    }
    void push_back(RevisionIndex r) {
      Block *p;
      size_t j = used % Width;
      if (used < limit) {
        p = &blocks[used / Width];
        if (!j)
          ::new (static_cast<void *>(p)) Block;
      } else {
        if (!j)
          more.emplace_back();
        p = &more.back();
      }
      p->keys[j] = r.id.bits;
      p->data[j] = {r.ingest, r.location};
      ++used;
    }
    void release() { std::vector<Block>().swap(more); }
  };
  PartitionSlab<Block> slab;
  std::array<Bin, shards> bins;
  void reserve(size_t count) {
    slab.reserve((count + Width - 1) / Width);
    for (size_t i = 0; i < bins.size(); ++i) {
      bins[i].blocks = slab[i].data;
      bins[i].limit = slab[i].limit * Width;
    }
  }
  void release() { slab.release(); }
  Bin &operator[](size_t i) { return bins[i]; }
  const Bin &operator[](size_t i) const { return bins[i]; }
  auto begin() const { return bins.begin(); }
  auto end() const { return bins.end(); }
};
