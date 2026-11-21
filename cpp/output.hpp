#pragma once
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

struct UserDay { Str user; int32_t day; int64_t sessions, events, revenue; std::optional<Str> category; };
struct Counts { int64_t sessions{}, purchases{}, revenue{}, assists{}; };
struct CampaignDay { Str campaign, channel; int32_t day; Counts counts; };

inline void checked(arrow::Status s) { if (!s.ok()) throw std::runtime_error(s.ToString()); }
template<class T> T checked(arrow::Result<T> r) {
    if (!r.ok()) throw std::runtime_error(r.status().ToString());
    return std::move(r).ValueUnsafe();
}
inline void write_table(const fs::path& path, std::vector<std::string> names, arrow::ArrayVector columns) {
    arrow::FieldVector fields;
    for (size_t i=0;i<columns.size();++i) fields.push_back(arrow::field(names[i],columns[i]->type()));
    auto table=arrow::Table::Make(arrow::schema(fields),columns);
    auto file=checked(arrow::io::FileOutputStream::Open(path.string()));
    auto props=parquet::WriterProperties::Builder().disable_dictionary()->build();
    checked(parquet::arrow::WriteTable(*table,arrow::default_memory_pool(),file,65536,props));
    checked(file->Close());
}
inline void write_users(const fs::path& file, const std::vector<UserDay>& rows) {
    arrow::StringBuilder user, category;
    arrow::Date32Builder day;
    arrow::Int64Builder sessions,events,revenue;
    for (auto& r:rows) {
        checked(user.Append(r.user)); checked(day.Append(r.day)); checked(sessions.Append(r.sessions));
        checked(events.Append(r.events)); checked(revenue.Append(r.revenue));
        checked(r.category ? category.Append(*r.category) : category.AppendNull());
    }
    write_table(file,{"user_id","day","sessions","events","revenue_cents","top_category"},
        {checked(user.Finish()),checked(day.Finish()),checked(sessions.Finish()),checked(events.Finish()),checked(revenue.Finish()),checked(category.Finish())});
}
inline void write_campaigns(const fs::path& file, const std::vector<CampaignDay>& rows) {
    arrow::StringBuilder id,channel;
    arrow::Date32Builder day;
    arrow::Int64Builder sessions,purchases,revenue,assists;
    for (auto& r:rows) {
        checked(id.Append(r.campaign)); checked(channel.Append(r.channel)); checked(day.Append(r.day));
        checked(sessions.Append(r.counts.sessions)); checked(purchases.Append(r.counts.purchases));
        checked(revenue.Append(r.counts.revenue)); checked(assists.Append(r.counts.assists));
    }
    write_table(file,{"campaign_id","channel","day","sessions","purchasing_sessions","revenue_cents","assisted_sessions"},
        {checked(id.Finish()),checked(channel.Finish()),checked(day.Finish()),checked(sessions.Finish()),checked(purchases.Finish()),checked(revenue.Finish()),checked(assists.Finish())});
}
