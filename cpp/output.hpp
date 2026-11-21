#pragma once
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

struct UserDay {
  Key user;
  int32_t day;
  int64_t sessions, events, revenue;
  std::optional<Str> category;
};
struct Counts {
  int64_t sessions{}, purchases{}, revenue{}, assists{};
};
struct CampaignDay {
  Str campaign, channel;
  int32_t day;
  Counts counts;
};

inline void checked(arrow::Status s) {
  if (!s.ok())
    throw std::runtime_error(s.ToString());
}
template <class T> T checked(arrow::Result<T> r) {
  if (!r.ok())
    throw std::runtime_error(r.status().ToString());
  return std::move(r).ValueUnsafe();
}
inline void write_table(const fs::path &path, std::vector<std::string> names,
                        arrow::ArrayVector columns) {
  arrow::FieldVector fields;
  for (size_t i = 0; i < columns.size(); ++i)
    fields.push_back(arrow::field(names[i], columns[i]->type()));
  auto table = arrow::Table::Make(arrow::schema(fields), columns);
  auto file = checked(arrow::io::FileOutputStream::Open(path.string()));
  parquet::WriterProperties::Builder builder;
  builder.disable_dictionary()->disable_statistics();
  for (size_t i = 0; i < names.size(); ++i)
    if (columns[i]->type_id() == arrow::Type::DATE32 ||
        columns[i]->type_id() == arrow::Type::INT64)
      builder.encoding(names[i], parquet::Encoding::DELTA_BINARY_PACKED);
  auto props = builder.build();
  checked(parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), file,
                                     65536, props));
  checked(file->Close());
}
inline void write_users(const fs::path &file,
                        const std::vector<UserDay> &rows) {
  MEASURE(parquet_write);
  arrow::StringBuilder user, category;
  arrow::Date32Builder day;
  arrow::Int64Builder sessions, events, revenue;
  const auto n = static_cast<int64_t>(rows.size());
  checked(day.Reserve(n));
  checked(sessions.Reserve(n));
  checked(events.Reserve(n));
  checked(revenue.Reserve(n));
  checked(user.Reserve(n));
  checked(category.Reserve(n));
  checked(user.ReserveData(9 * n));
  for (auto &r : rows) {
    std::array<char, 9> buffer;
    checked(user.Append(r.user.user_text(buffer)));
    day.UnsafeAppend(r.day);
    sessions.UnsafeAppend(r.sessions);
    events.UnsafeAppend(r.events);
    revenue.UnsafeAppend(r.revenue);
    checked(r.category ? category.Append(*r.category) : category.AppendNull());
  }
  write_table(
      file,
      {"user_id", "day", "sessions", "events", "revenue_cents", "top_category"},
      {checked(user.Finish()), checked(day.Finish()),
       checked(sessions.Finish()), checked(events.Finish()),
       checked(revenue.Finish()), checked(category.Finish())});
}
inline void write_campaigns(const fs::path &file,
                            const std::vector<CampaignDay> &rows) {
  arrow::StringBuilder id, channel;
  arrow::Date32Builder day;
  arrow::Int64Builder sessions, purchases, revenue, assists;
  for (auto &r : rows) {
    checked(id.Append(r.campaign));
    checked(channel.Append(r.channel));
    checked(day.Append(r.day));
    checked(sessions.Append(r.counts.sessions));
    checked(purchases.Append(r.counts.purchases));
    checked(revenue.Append(r.counts.revenue));
    checked(assists.Append(r.counts.assists));
  }
  write_table(file,
              {"campaign_id", "channel", "day", "sessions",
               "purchasing_sessions", "revenue_cents", "assisted_sessions"},
              {checked(id.Finish()), checked(channel.Finish()),
               checked(day.Finish()), checked(sessions.Finish()),
               checked(purchases.Finish()), checked(revenue.Finish()),
               checked(assists.Finish())});
}
