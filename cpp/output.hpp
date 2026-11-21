#pragma once
struct UserDay {
  Key user;
  int32_t day;
  int64_t sessions, events, revenue;
  const Str *category{};
};
struct Counts {
  int64_t sessions{}, purchases{}, revenue{}, assists{};
};
struct CampaignDay {
  Str campaign, channel;
  int32_t day;
  Counts counts;
};

#include "flat_parquet.hpp"
