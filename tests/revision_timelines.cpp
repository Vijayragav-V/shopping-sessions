#include "../cpp/input.hpp"
#include <cassert>
#include <cstdio>
#include <span>

#define SHOP_REUSE_REGION 1
#define SHOP_LEND_REGION 1
#include "../cpp/timeline_reuse.hpp"

using EventFields = std::array<Str, 7>;

struct ComparedRows {
  std::vector<EventFields> expected;
  size_t consumed{}, direct{};

  void operator()(const EventFields &row) {
    assert(consumed < expected.size());
    assert(expected[consumed] == row);
    ++consumed;
  }

  bool common(const char *id, unsigned id_size, const char *user,
              unsigned user_size, Str timestamp, Str ingest) {
    assert(consumed < expected.size());
    const auto &row = expected[consumed++];
    assert(row[0] == Str(id, id_size) && row[1] == Str(user, user_size));
    assert(row[2] == timestamp && row[3] == ingest);
    assert((row[4] == "view" || row[4] == "cart") && !row[5].empty());
    ++direct;
    return true;
  }
};

void check_event_fields() {
  size_t direct = 0;
  for (auto id : {"e1234567", "e12345678"}) {
    for (auto user : {"u0123456", "u01234567"}) {
      for (auto time : {"0000000000001", "2026-01-31T23:59:59Z",
                        "2026-02-01T00:59:59+01:00"}) {
        std::string input;
        for (unsigned quotes = 0; quotes < 128; ++quotes) {
          std::array<Str, 7> fields{id, user, time, "2026-02-01T00:00:00Z",
                                    quotes % 2 ? "view" : "cart", "p0001", ""};
          std::string line;
          for (unsigned i = 0; i < fields.size(); ++i) {
            if (i) line += ',';
            bool wrap = (quotes >> i) & 1;
            if (wrap) line += '"';
            line += fields[i];
            if (wrap) line += '"';
          }
          input += line + '\n';
          for (size_t i = 0; i < line.size(); ++i) {
            char saved = line[i];
            for (char changed : {',', '"', '\0'}) {
              line[i] = changed;
              input += line + '\n';
            }
            line[i] = saved;
          }
        }
        input.pop_back();
        ComparedRows audit;
        scalar_rows<7>(input, [&](auto &row) { audit.expected.push_back(row); });
        rows<7>(input, audit);
        assert(audit.consumed == audit.expected.size());
        direct += audit.direct;
      }
    }
  }
#ifdef SHOPPING_X86_CSV
  if (wide_csv_supported()) assert(direct != 0);
#endif
  (void)direct;
}

void check_concurrent_timelines() {
  constexpr size_t workers = 8, batches = 101;
  alignas(64) std::array<std::array<std::byte, 4096>, workers / 2> backing;
  std::array<TimelineRegion, workers> regions;
  for (size_t i = 0; i < backing.size(); ++i) {
    regions[i].base = backing[i].data();
    regions[i].bytes = backing[i].size();
  }
  std::array<std::array<TimelineArray<uint64_t>, batches>, workers> timelines;
  {
    std::vector<std::jthread> threads;
    for (unsigned owner = 0; owner < workers; ++owner) {
      threads.emplace_back([&, owner] {
        for (size_t b = 0; b < batches; ++b) {
          auto &timeline = timelines[owner][b];
          size_t count = b + 1 == batches ? 513 : (b + owner) % 20;
          timeline.prepare(count, regions, owner);
          for (size_t i = 0; i < count; ++i)
            timeline[i] = ((owner * batches + b) << 16) + i;
        }
      });
    }
  }
  size_t reused = 0, allocated = 0;
  for (size_t owner = 0; owner < workers; ++owner) {
    for (size_t b = 0; b < batches; ++b) {
      auto &timeline = timelines[owner][b];
      for (size_t i = 0; i < timeline.view().size(); ++i)
        assert(timeline[i] == ((owner * batches + b) << 16) + i);
      (timeline.owned ? allocated : reused) += timeline.size;
    }
  }
  assert(reused && allocated);
}

int main() {
  check_event_fields();
  check_concurrent_timelines();
  std::puts("Direct event fields and concurrent timeline reuse matched.");
}
