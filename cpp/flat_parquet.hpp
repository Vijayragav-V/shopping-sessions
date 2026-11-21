#pragma once
#include <span>

// Flat, uncompressed Parquet data pages with a Thrift Compact footer.
// https://parquet.apache.org/docs/file-format/

namespace flat_parquet {
using Bytes = std::string;
inline void varint(Bytes &out, uint64_t n) {
  while (n >= 128) {
    out.push_back(char((n & 127) | 128));
    n >>= 7;
  }
  out.push_back(char(n));
}
template <class T> void little(Bytes &out, T value) {
  if constexpr (std::endian::native == std::endian::little)
    out.append(reinterpret_cast<const char *>(&value), sizeof(value));
  else
    for (unsigned i = 0; i < sizeof(T); ++i)
      out.push_back(char(std::make_unsigned_t<T>(value) >> (8 * i)));
}
inline void thrift_string(Bytes &out, Str s) {
  varint(out, s.size());
  out.append(s);
}
enum Wire { Stop = 0, I32 = 5, I64 = 6, String = 8, List = 9, Struct = 12 };
inline void list(Bytes &out, size_t n, Wire type) {
  out.push_back(char((std::min<size_t>(15, n) << 4) | type));
  if (n >= 15)
    varint(out, n);
}
struct Fields {
  Bytes &out;
  unsigned previous{};
  void field(unsigned id, Wire type) {
    const unsigned delta = id - previous;
    if (delta && delta <= 15)
      out.push_back(char((delta << 4) | type));
    else {
      out.push_back(char(type));
      varint(out, 2u * id);
    }
    previous = id;
  }
  void integer(unsigned id, int64_t value, Wire type = I32) {
    field(id, type);
    varint(out, (uint64_t(value) << 1) ^ (uint64_t(0) - uint64_t(value < 0)));
  }
  void text(unsigned id, Str s) {
    field(id, String);
    thrift_string(out, s);
  }
  void sequence(unsigned id, size_t count, Wire item) {
    field(id, List);
    list(out, count, item);
  }
  void finish() { out.push_back(0); }
};
struct Column {
  Str name;
  int physical;
  int converted = -1;
  bool optional = false;
};
struct Page {
  uint64_t offset, bytes;
  int encoding;
};
struct Group {
  size_t rows{};
  uint64_t bytes{};
  std::vector<Page> pages;
};

class File {
  std::vector<char> buffer = std::vector<char>(1u << 20);
  std::ofstream stream;
  std::span<const Column> columns;
  std::vector<Group> groups;
  uint64_t position{}, total_rows{};
  void write(Str bytes) {
    stream.write(bytes.data(), std::streamsize(bytes.size()));
    position += bytes.size();
  }

public:
  File(const fs::path &path, std::span<const Column> cols) : columns(cols) {
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.rdbuf()->pubsetbuf(buffer.data(), buffer.size());
    stream.open(path, std::ios::binary | std::ios::trunc);
    write("PAR1");
  }
  void group(size_t n) {
    groups.emplace_back().rows = n;
    total_rows += n;
  }
  void page(Str data, int encoding = 0) {
    if (data.size() > INT32_MAX)
      throw std::length_error("Parquet data page exceeds int32 size");
    auto &g = groups.back();
    Bytes header;
    Fields ph{header};
    ph.integer(1, 0);
    ph.integer(2, data.size());
    ph.integer(3, data.size());
    ph.field(5, Struct);
    Fields dh{header};
    dh.integer(1, g.rows);
    dh.integer(2, encoding);
    dh.integer(3, 3);
    dh.integer(4, 3);
    dh.finish();
    ph.finish();
    uint64_t bytes = header.size() + data.size();
    g.pages.push_back({position, bytes, encoding});
    g.bytes += bytes;
    write(header);
    write(data);
  }
  void close() {
    Bytes footer;
    Fields file{footer};
    file.integer(1, 2);
    file.sequence(2, columns.size() + 1, Struct);
    Fields root{footer};
    root.text(4, "schema");
    root.integer(5, columns.size());
    root.finish();
    for (auto &c : columns) {
      Fields schema{footer};
      schema.integer(1, c.physical);
      schema.integer(3, c.optional ? 1 : 0);
      schema.text(4, c.name);
      if (c.converted >= 0) {
        schema.integer(6, c.converted);
        schema.field(10, Struct);
        Fields annotation{footer};
        annotation.field(c.converted == 0 ? 1u : 6u, Struct);
        footer.push_back(0);
        annotation.finish();
      }
      schema.finish();
    }
    file.integer(3, total_rows, I64);
    file.sequence(4, groups.size(), Struct);
    for (auto &g : groups) {
      Fields rg{footer};
      rg.sequence(1, columns.size(), Struct);
      for (size_t i = 0; i < columns.size(); ++i) {
        auto &p = g.pages.at(i);
        auto &c = columns[i];
        Fields chunk{footer};
        chunk.integer(2, 0, I64); // Column metadata is entirely in the footer.
        chunk.field(3, Struct);
        Fields md{footer};
        md.integer(1, c.physical);
        md.sequence(2, c.optional ? 2 : 1, I32);
        varint(footer, unsigned(p.encoding) * 2);
        if (c.optional)
          varint(footer, 6);
        md.sequence(3, 1, String);
        thrift_string(footer, c.name);
        md.integer(4, 0);
        md.integer(5, g.rows, I64);
        md.integer(6, p.bytes, I64);
        md.integer(7, p.bytes, I64);
        md.integer(9, p.offset, I64);
        md.finish();
        chunk.finish();
      }
      rg.integer(2, g.bytes, I64);
      rg.integer(3, g.rows, I64);
      rg.finish();
    }
    file.text(6, "shopping-sessions-cpp");
    file.finish();
    if (footer.size() > UINT32_MAX)
      throw std::length_error("Parquet footer exceeds uint32 size");
    uint32_t n = uint32_t(footer.size());
    little(footer, n);
    footer.append("PAR1");
    write(footer);
    stream.close();
  }
};
inline void plain_string(Bytes &out, Str s) {
  if (s.size() > INT32_MAX)
    throw std::length_error("Parquet byte array exceeds int32 size");
  little(out, uint32_t(s.size()));
  out.append(s);
}
template <class R, class Length>
size_t batch(std::span<const R> rows, Length length) {
  size_t n = 0, bytes = 0;
  while (n < rows.size() && n < 32768) {
    auto extra = length(rows[n]);
    if (n && extra > std::max<size_t>(bytes, 8u << 20) - bytes)
      break;
    bytes += extra;
    ++n;
  }
  return n;
}
template <class T> inline void delta_pack(Bytes &out, Str raw) {
  using Unsigned = std::make_unsigned_t<T>;
  const size_t count = raw.size() / sizeof(T);
  auto value = [&](size_t i) {
    std::make_unsigned_t<T> bits{};
    std::memcpy(&bits, raw.data() + i * sizeof(T), sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
      if constexpr (sizeof(T) == 8)
        bits = __builtin_bswap64(bits);
      else
        bits = __builtin_bswap32(bits);
    }
    return int64_t(std::bit_cast<T>(bits));
  };
  auto zigzag = [&](int64_t v) {
    varint(out, (uint64_t(v) << 1) ^ (uint64_t(0) - uint64_t(v < 0)));
  };
  varint(out, 128);
  varint(out, 4);
  varint(out, count);
  if (!count)
    return;
  int64_t prev = value(0);
  zigzag(prev);
  for (size_t pos = 1; pos < count;) {
    std::array<int64_t, 128> ds;
    std::array<uint64_t, 128> adjusted{};
    const unsigned n = unsigned(std::min<size_t>(128, count - pos));
    int64_t low = INT64_MAX;
    for (unsigned i = 0; i < n; ++i) {
      auto v = value(pos + i);
      ds[i] = std::bit_cast<T>(Unsigned(Unsigned(v) - Unsigned(prev)));
      low = std::min(low, ds[i]);
      prev = v;
    }
    zigzag(low);
    for (unsigned i = 0; i < n; ++i)
      adjusted[i] = Unsigned(Unsigned(ds[i]) - Unsigned(low));
    std::array<unsigned, 4> widths;
    for (unsigned j = 0; j < 4; ++j) {
      uint64_t any = 0;
      for (unsigned i = 0; i < 32; ++i)
        any |= adjusted[j * 32 + i];
      widths[j] = std::bit_width(any);
      out.push_back(char(widths[j]));
    }
    for (unsigned j = 0; j < 4; ++j) {
      unsigned width = widths[j];
      if (!width)
        continue;
      uint64_t acc = 0;
      unsigned used = 0;
      for (unsigned i = 0; i < 32; ++i) {
        uint64_t v = adjusted[j * 32 + i];
        acc |= v << used;
        used += width;
        if (used >= 64) {
          little(out, acc);
          used -= 64;
          acc = used ? v >> (width - used) : 0;
        }
      }
      for (unsigned at = 0; at < used; at += 8)
        out.push_back(char(acc >> at));
    }
    pos += n;
  }
}

// A single bit-packed hybrid run carries the optional string's definition
// levels.
inline size_t levels(Bytes &out, size_t n) {
  size_t bytes = (n + 7) / 8;
  Bytes run;
  varint(run, 2 * bytes + 1);
  little(out, uint32_t(run.size() + bytes));
  out += run;
  size_t first = out.size();
  out.resize(first + bytes, 0);
  return first;
}
} // namespace flat_parquet

class UserFile {
  using Bytes = flat_parquet::Bytes;
  static constexpr flat_parquet::Column columns[] = {
      {"user_id", 6, 0}, {"day", 1, 6},        {"sessions", 2},
      {"events", 2},     {"revenue_cents", 2}, {"top_category", 6, 0, true}};
  static constexpr size_t max_rows = 16384;
  static constexpr size_t max_text_bytes = 8u << 20;

  flat_parquet::File file;
  std::array<Bytes, 6> values;
  Bytes definitions, encoded;
  size_t rows{}, text_bytes{};
  std::array<char, 9> inline_name;
  Key previous_user{};
  Str user_name;

  void flush() {
    if (!rows)
      return;
    MEASURE(parquet_write);
    file.group(rows);
    file.page(values[0]);
    for (size_t column = 1; column < 5; ++column) {
      encoded.clear();
      if (column == 1)
        flat_parquet::delta_pack<int32_t>(encoded, values[column]);
      else
        flat_parquet::delta_pack<int64_t>(encoded, values[column]);
      file.page(encoded, 5);
    }
    // Optional string pages place definition levels before the present values.
    encoded.clear();
    const size_t first = flat_parquet::levels(encoded, rows);
    std::memcpy(encoded.data() + first, definitions.data(), definitions.size());
    encoded.append(values[5]);
    file.page(encoded);
    for (auto &column : values)
      column.clear();
    definitions.clear();
    rows = text_bytes = 0;
  }

public:
  explicit UserFile(const fs::path &path) : file(path, columns) {
    for (auto &column : values)
      column.reserve(max_rows * 8);
    definitions.reserve(max_rows / 8);
    encoded.reserve(max_rows * 4);
  }

  void append(const UserDay &row) {
    if (previous_user.bits != row.user.bits) {
      user_name = row.user.user_text(inline_name);
      previous_user = row.user;
    }
    const size_t extra =
        user_name.size() + (row.category ? row.category->size() : 0);
    if (rows && (rows == max_rows ||
                 extra > std::max(max_text_bytes, text_bytes) - text_bytes))
      flush();
    text_bytes += extra;
    flat_parquet::plain_string(values[0], user_name);
    flat_parquet::little(values[1], row.day);
    flat_parquet::little(values[2], row.sessions);
    flat_parquet::little(values[3], row.events);
    flat_parquet::little(values[4], row.revenue);
    if (rows % 8 == 0)
      definitions.push_back(0);
    if (row.category) {
      definitions.back() = char(static_cast<unsigned char>(definitions.back()) |
                                (1u << (rows % 8)));
      flat_parquet::plain_string(values[5], *row.category);
    }
    ++rows;
  }

  void close() {
    flush();
    file.close();
  }
};

inline void write_campaigns(const fs::path &path,
                            const std::vector<CampaignDay> &data) {
  using namespace flat_parquet;
  static constexpr Column cols[] = {
      {"campaign_id", 6, 0},   {"channel", 6, 0},          {"day", 1, 6},
      {"sessions", 2},         {"purchasing_sessions", 2}, {"revenue_cents", 2},
      {"assisted_sessions", 2}};
  File file(path, cols);
  std::span<const CampaignDay> rows(data);
  std::array<Bytes, 7> pages;
  while (!rows.empty()) {
    size_t n = batch(
        rows, [](auto &r) { return r.campaign.size() + r.channel.size(); });
    for (auto &p : pages) {
      p.clear();
      p.reserve(n * 12);
    }
    for (const auto &r : rows.first(n)) {
      plain_string(pages[0], r.campaign);
      plain_string(pages[1], r.channel);
      little(pages[2], r.day);
      little(pages[3], r.counts.sessions);
      little(pages[4], r.counts.purchases);
      little(pages[5], r.counts.revenue);
      little(pages[6], r.counts.assists);
    }
    file.group(n);
    Bytes packed;
    for (size_t col = 0; col < pages.size(); ++col) {
      if (cols[col].physical == 6)
        file.page(pages[col]);
      else {
        packed.clear();
        packed.reserve(n * 4);
        if (cols[col].physical == 1)
          delta_pack<int32_t>(packed, pages[col]);
        else
          delta_pack<int64_t>(packed, pages[col]);
        file.page(packed, 5);
      }
    }
    rows = rows.subspan(n);
  }
  file.close();
}
