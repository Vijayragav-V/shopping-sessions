#pragma once

constexpr Time session_gap = 1'800'000;

struct SessionBins {
  struct Bin {
    Time first, last;
    int64_t count{}, revenue;
    size_t purchases;
  };
  struct Purchase {
    int category, price;
    size_t previous;
  };
  Time origin{};
  std::vector<Bin> cells;
  std::vector<uint64_t> occupied;
  std::vector<Purchase> purchases;

  void configure(Window window) {
    origin = window.start;
    uint64_t width = uint64_t(window.end) - uint64_t(origin);
    auto n = 1 + (width - 1) / session_gap;
    // Bound each worker's scratch space. Wide windows use the normal sort.
    if (n <= 8192) {
      cells.resize(n);
      occupied.resize((n + 63) / 64);
    }
  }

  template <class T, class Begin, class Spend, class End, class Layout>
  void scan(std::span<T> events, Begin begin, Spend spend, End end,
            Layout layout) {
    for (auto word : events) {
      const auto e = layout.unpack(word);
      size_t index = (uint64_t(e.ts) - uint64_t(origin)) / session_gap;
      auto &cell = cells[index];
      if (cell.count++ == 0) {
        occupied[index / 64] |= uint64_t{1} << (index % 64);
        cell.first = cell.last = e.ts;
        cell.purchases = 0;
        cell.revenue = 0;
      } else {
        cell.first = std::min(cell.first, e.ts);
        cell.last = std::max(cell.last, e.ts);
      }
      if (e.price) {
        cell.revenue += e.price;
        purchases.push_back({e.category, e.price, cell.purchases});
        cell.purchases = purchases.size();
      }
    }
    Time start{}, last{};
    int64_t count = 0, revenue = 0;
    for (size_t word = 0; word < occupied.size(); ++word) {
      uint64_t bits = occupied[word];
      occupied[word] = 0;
      while (bits) {
        auto &cell = cells[word * 64 + std::countr_zero(bits)];
        bits &= bits - 1;
        // All times within one half-open bin are less than one gap apart.
        if (!count || cell.first - last > session_gap) {
          if (count)
            end(start, count, revenue);
          start = cell.first;
          begin(start);
          count = revenue = 0;
        }
        for (size_t i = cell.purchases; i; i = purchases[i - 1].previous)
          spend(purchases[i - 1].category, purchases[i - 1].price);
        count += cell.count;
        revenue += cell.revenue;
        last = cell.last;
        cell.count = 0;
      }
    }
    end(start, count, revenue);
    purchases.clear();
  }
};
