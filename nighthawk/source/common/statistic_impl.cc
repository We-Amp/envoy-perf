#include "nighthawk/source/common/statistic_impl.h"

#include <cmath>

#include "common/common/assert.h"

namespace Nighthawk {

StreamingStatistic::StreamingStatistic() : count_(0), mean_(0), sum_of_squares_(0) {}

void StreamingStatistic::addValue(int64_t value) {
  double delta, delta_n;
  count_++;
  delta = value - mean_;
  delta_n = delta / count_;
  mean_ += delta_n;
  sum_of_squares_ += delta * delta_n * (count_ - 1);
}

uint64_t StreamingStatistic::count() const { return count_; }

double StreamingStatistic::mean() const { return mean_; }

double StreamingStatistic::variance() const { return sum_of_squares_ / (count_ - 1.0); }

double StreamingStatistic::stdev() const { return sqrt(variance()); }

StreamingStatistic StreamingStatistic::combine(const StreamingStatistic& b) {
  const StreamingStatistic& a = *this;
  StreamingStatistic combined;

  combined.count_ = a.count() + b.count();
  combined.mean_ = ((a.count() * a.mean()) + (b.count() * b.mean())) / combined.count_;
  combined.sum_of_squares_ = a.sum_of_squares_ + b.sum_of_squares_ +
                             pow(a.mean() - b.mean(), 2) * a.count() * b.count() / combined.count();
  return combined;
}

void InMemoryStatistic::addValue(int64_t sample_value) {
  samples_.push_back(sample_value);
  streaming_stats_.addValue(sample_value);
}

uint64_t InMemoryStatistic::count() const {
  ASSERT(streaming_stats_.count() == samples_.size());
  return streaming_stats_.count();
}
double InMemoryStatistic::mean() const { return streaming_stats_.mean(); }
double InMemoryStatistic::variance() const { return streaming_stats_.variance(); }
double InMemoryStatistic::stdev() const { return streaming_stats_.stdev(); }

HdrStatistic::HdrStatistic() : histogram_(nullptr) {
  int status = hdr_init(1, INT64_C(1000) * 1000 * 1000 * 60, 5, &histogram_);
  ASSERT(!status);
}

HdrStatistic::~HdrStatistic() {
  hdr_close(histogram_);
  histogram_ = nullptr;
}

void HdrStatistic::addValue(int64_t value) { ASSERT(hdr_record_value(histogram_, value)); }

uint64_t HdrStatistic::count() const { return histogram_->total_count; }
double HdrStatistic::mean() const { return hdr_mean(histogram_); }
double HdrStatistic::variance() const {
  return stdev() * stdev();
  ;
}
double HdrStatistic::stdev() const { return hdr_stddev(histogram_); }

} // namespace Nighthawk