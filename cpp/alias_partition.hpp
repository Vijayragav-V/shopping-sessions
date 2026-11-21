#pragma once

// Learn a placement hint from actual link pairs. A placement collision
// never joins users: local and global unions both require an input edge.
class AliasPartition {
  static constexpr unsigned stride = 512;
  static_assert(std::has_single_bit(shards));
  static constexpr uint32_t mask = shards * stride - 1;
  static constexpr uint64_t inverse(uint64_t odd) {
    uint64_t x = 1;
    for (unsigned i = 0; i < 6; ++i) x *= 2 - odd * x;
    return x;
  }
  // Reversible numeric projection from buildathon-tests/src/keyrec.rs.
  // Non-base36 bytes can collide in this hint and retain their exact Key.
  static uint32_t signature(uint64_t word, unsigned n) {
    constexpr uint64_t ones = 0x0101010101010101ULL;
    const unsigned shift = 8 * (8 - n);
    word = (word << shift) | ((ones * '0') & ((uint64_t{1} << shift) - 1));
    const uint64_t d = word - ones * '0' - (((word >> 6) & ones) * 39);
    constexpr uint64_t bytes = 0x00ff00ff00ff00ffULL;
    constexpr uint64_t pairs = 0x0000ffff0000ffffULL;
    auto d2 = (d & bytes) * 36 + ((d >> 8) & bytes);
    auto d4 = (d2 & pairs) * 1296 + ((d2 >> 16) & pairs);
    auto value = uint64_t(uint32_t(d4)) * 1679616 + (d4 >> 32);
    auto x = (value * inverse(0xd6e8feb86659ULL)) & ((uint64_t{1} << 40) - 1);
    return uint32_t(x ^ (x >> 17) ^ (x >> 34)) & mask;
  }
  static bool suitable(Str id) {
    return sizeof(uintptr_t) == 8 && std::endian::native == std::endian::little &&
           id.size() >= 2 && id.size() <= 9 && id.front() == 'u';
  }
  static uint64_t word_of(Str id) {
    uint64_t word = 0;
    if (id.size() == 9) std::memcpy(&word, id.data() + 1, 8);
    else std::memcpy(&word, id.data() + 1, id.size() - 1);
    return word;
  }
  static unsigned place(uint32_t s, uint32_t salt) {
    return (((s ^ salt) * uint32_t(inverse(0x9e3779b97f4bULL))) & mask) / stride;
  }
  uint32_t salt_{};
  bool enabled_{};

public:
  explicit AliasPartition(const std::vector<InputChunk> &links) {
    std::vector<std::pair<uint32_t,uint32_t>> sample;
    const size_t parts = std::min<size_t>(16, links.size());
    for (size_t i = 0; i < parts; ++i) {
      auto body = links[i * links.size() / parts].body();
      for (unsigned j = 0; j < 12 && !body.empty(); ++j) {
        size_t nl = body.find('\n');
        auto line = body.substr(0, nl);
        scalar_rows<2>(line, [&](const auto &r) {
          if (!suitable(r[0]) || !suitable(r[1]) || r[0] == r[1]) return;
          sample.emplace_back(signature(word_of(r[0]), r[0].size()-1),
                              signature(word_of(r[1]), r[1].size()-1));
        });
        body.remove_prefix(line.size() + (nl != Str::npos));
      }
    }
    if (sample.size() < 32) return;
    // The top salt bit rotates every bucket equally. Check a small prefix
    // before scoring an entire candidate. A miss selects ordinary hashing.
    for (unsigned salt = 0; salt <= mask / 2; ++salt) {
      unsigned score = 0;
      auto hit = [&](const auto &p) { return place(p.first,salt)==place(p.second,salt); };
      for (unsigned i = 0; i < 6; ++i) score += hit(sample[i]);
      if (score < 4) continue;
      score += std::count_if(sample.begin()+6, sample.end(), hit);
      if (score * 10 < sample.size() * 9) continue;
      std::array<bool,shards> seen{};
      for (const auto &p : sample) seen[place(p.first,salt)] = true;
      if (std::count(seen.begin(),seen.end(),true) >= 8) {
        enabled_ = true;
        salt_ = salt;
        break;
      }
    }
  }
  size_t short_id(Key key, uint64_t word, unsigned n) const {
    return enabled_ ? place(signature(word,n),salt_) : key.hash() % shards;
  }
  size_t operator()(Key key, Str text) const {
    if (enabled_ && suitable(text))
      return place(signature(word_of(text),text.size()-1),salt_);
    return key.hash() % shards;
  }
  bool enabled() const { return enabled_; }
};
