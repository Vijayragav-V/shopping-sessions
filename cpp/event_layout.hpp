#pragma once
#include "input.hpp"
struct Event {
  Time ts;
  int32_t price, category;
};
struct WideLayout {
  using E = Event;
  struct C {
    Key id;
    Time ts;
    int campaign;
  };

  E pack(Event raw) const { return raw; }
  Event unpack(E e) const { return e; }
  Time time(E e) const { return e.ts; }
  Time click_time(C c) const { return c.ts; }
  C click(Key id, Time ts, int campaign) const { return {id, ts, campaign}; }
  bool less(E a, E b) const { return a.ts < b.ts; }
};
// Used only when the manifest and catalog bounds fit every field. The high
// word holds exact millisecond offsets; the low word fits a valid purchase
// (at most 10,000,000 cents) and its category, including unknown.
struct WordLayout {
  using E = uint64_t;
  struct C {
    Key id;
    uint32_t ts;
    int campaign;
  };
  Time origin;
  E pack(Event raw) const {
    return ((uint64_t(raw.ts) - uint64_t(origin)) << 32) |
           (uint64_t(uint32_t(raw.price)) << 8) | uint32_t(raw.category);
  }
  Time time(E e) const { return origin + Time(e >> 32); }
  Event unpack(E e) const {
    return {time(e), int32_t(uint32_t(e) >> 8), int32_t(e & 255)};
  }
  Time click_time(C c) const { return origin - reach + Time(c.ts); }
  C click(Key id, Time ts, int campaign) const {
    return {id, uint32_t(uint64_t(ts) - uint64_t(origin) + reach), campaign};
  }
  bool less(E a, E b) const { return a < b; }
};
