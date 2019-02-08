#pragma once

#include <vector>

#include "nighthawk/hdrhistogram_c/src/hdr_histogram.h"

#include "nighthawk/common/statistic.h"

namespace Nighthawk {

class InMemoryStatistic : Statistic {
public:
  InMemoryStatistic() = default;
  void AddSample(int64_t sample_value) override;

private:
  std::vector<int64_t> samples_;
};

class HdrStatistic : Statistic {
public:
  HdrStatistic() = default;
  void AddSample(int64_t sample_value) override;

private:
  struct hdr_histogram* histogram_;
};

} // namespace Nighthawk