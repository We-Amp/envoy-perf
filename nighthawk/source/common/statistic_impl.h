#pragma once

#include <memory>
#include <vector>

#include "nighthawk/hdrhistogram_c/src/hdr_histogram.h"

#include "nighthawk/common/statistic.h"

namespace Nighthawk {

class StreamingStatistic : Statistic {
public:
  StreamingStatistic();
  void addValue(int64_t value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;

  StreamingStatistic combine(const StreamingStatistic& a);

private:
  uint64_t count_;
  double mean_;
  double sum_of_squares_;
};

class InMemoryStatistic : Statistic {
public:
  InMemoryStatistic() = default;
  void addValue(int64_t sample_value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;

private:
  std::vector<int64_t> samples_;
  StreamingStatistic streaming_stats_;
};

class HdrStatistic : Statistic {
public:
  HdrStatistic();
  virtual ~HdrStatistic() override;
  void addValue(int64_t sample_value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;

private:
  struct hdr_histogram* histogram_;
};

} // namespace Nighthawk