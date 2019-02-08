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

// TODO(oschaaf): something more subtle then ASSERT.
HdrStatistic::HdrStatistic() : histogram_(nullptr) {
  // Upper bound of 60 seconds (tracking in nanoseconds).
  int status = hdr_init(1, INT64_C(1000) * 1000 * 1000 * 60, 5, &histogram_);
  ASSERT(!status);
}

HdrStatistic::~HdrStatistic() {
  hdr_close(histogram_);
  histogram_ = nullptr;
}

// TODO(oschaaf): something more subtle then ASSERT.
void HdrStatistic::addValue(int64_t value) { ASSERT(hdr_record_value(histogram_, value)); }

uint64_t HdrStatistic::count() const { return histogram_->total_count; }
double HdrStatistic::mean() const { return hdr_mean(histogram_); }
double HdrStatistic::variance() const {
  return stdev() * stdev();
  ;
}
double HdrStatistic::stdev() const {
  // HdrHistogram_c's stdev actually gives us the population standard deviation.
  // So we compute the sample standard deviation ourselves instead.
  // TODO(oschaaf): this fixes some of the test expectations, but figure out if
  // stdev or pstdev is preferrable. Looks like wrk2 uses pstdev which would produce
  // (slightly) better numbers, though that probably isn't a reason for us to decice
  // which one to use here. Switching to pstdev would get rid of having to do this
  // ourselves.
  double mean = hdr_mean(histogram_);
  double geometric_dev_total = 0.0;

  struct hdr_iter iter;
  hdr_iter_init(&iter, histogram_);

  while (hdr_iter_next(&iter)) {
    if (0 != iter.count) {
      double dev = (hdr_median_equivalent_value(histogram_, iter.value) * 1.0) - mean;
      geometric_dev_total += (dev * dev) * iter.count;
    }
  }

  return sqrt(geometric_dev_total / (histogram_->total_count - 1));
}

} // namespace Nighthawk