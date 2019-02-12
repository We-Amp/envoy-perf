#pragma once

#include <memory>
#include <vector>

#include "nighthawk/hdrhistogram_c/src/hdr_histogram.h"

#include "common/common/logger.h"
#include "common/common/non_copyable.h"

#include "nighthawk/common/statistic.h"
#include "nighthawk/source/client/output.pb.h"
#include "nighthawk/source/common/frequency.h"

namespace Nighthawk {

class StreamingStatistic : public Statistic<StreamingStatistic>,
                           public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  StreamingStatistic();
  void addValue(int64_t value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;
  std::unique_ptr<StreamingStatistic> combine(const StreamingStatistic& a) override;
  void dumpToStdOut(std::string header) override;

private:
  uint64_t count_;
  double mean_;
  double sum_of_squares_;
};

class InMemoryStatistic : public Statistic<InMemoryStatistic>,
                          public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  InMemoryStatistic();
  void addValue(int64_t sample_value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;
  std::unique_ptr<InMemoryStatistic> combine(const InMemoryStatistic& a) override;
  void dumpToStdOut(std::string header) override;

private:
  std::vector<int64_t> samples_;
  std::unique_ptr<StreamingStatistic> streaming_stats_;
};

class HdrStatistic : public Statistic<HdrStatistic>,
                     Envoy::NonCopyable,
                     public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  HdrStatistic();
  virtual ~HdrStatistic() override;
  void addValue(int64_t sample_value) override;
  uint64_t count() const override;
  double mean() const override;
  double variance() const override;
  double stdev() const override;
  std::unique_ptr<HdrStatistic> combine(const HdrStatistic& a) override;
  std::unique_ptr<HdrStatistic> getCorrected(Frequency frequency);
  virtual bool is_high_precision() override { return false; }
  void dumpToStdOut(std::string header) override;
  void percentilesToProto(nighthawk::client::Output& output, bool corrected);

private:
  struct hdr_histogram* histogram_;
};

} // namespace Nighthawk