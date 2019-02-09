#pragma once

#include <memory>
#include <vector>

#include "nighthawk/hdrhistogram_c/src/hdr_histogram.h"

#include <memory>

#include "nighthawk/common/statistic.h"

namespace Nighthawk {

class StreamingStatistic : public Statistic<StreamingStatistic> {
public:
  StreamingStatistic();
  void addValue(int64_t value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;
  StreamingStatistic combine(const StreamingStatistic& a) override;
  std::string toString() override;

private:
  uint64_t count_;
  double mean_;
  double sum_of_squares_;
};

class InMemoryStatistic : public Statistic<InMemoryStatistic> {
public:
  InMemoryStatistic() = default;
  void addValue(int64_t sample_value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;
  InMemoryStatistic combine(const InMemoryStatistic& a) override;
  std::string toString() override;

private:
  std::vector<int64_t> samples_;
  StreamingStatistic streaming_stats_;
};

class HdrStatistic : public Statistic<HdrStatistic> {
public:
  HdrStatistic();
  virtual ~HdrStatistic() override;
  void addValue(int64_t sample_value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;
  HdrStatistic combine(const HdrStatistic& a) override;
  virtual bool is_high_precision() override { return false; }
  std::string toString() override;

private:
  struct hdr_histogram* histogram_;
};

} // namespace Nighthawk