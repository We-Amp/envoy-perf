#include "nighthawk/source/common/statistic_impl.h"

#include <cmath>
#include <stdio.h>

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

std::unique_ptr<StreamingStatistic> StreamingStatistic::combine(const StreamingStatistic& b) {
  const StreamingStatistic& a = *this;
  auto combined = std::make_unique<StreamingStatistic>();

  combined->count_ = a.count() + b.count();
  combined->mean_ = ((a.count() * a.mean()) + (b.count() * b.mean())) / combined->count_;
  combined->sum_of_squares_ =
      a.sum_of_squares_ + b.sum_of_squares_ +
      pow(a.mean() - b.mean(), 2) * a.count() * b.count() / combined->count();
  return combined;
}

void StreamingStatistic::dumpToStdOut(std::string header) { ENVOY_LOG(info, "{}", header); }

InMemoryStatistic::InMemoryStatistic() : streaming_stats_(std::make_unique<StreamingStatistic>()) {}

void InMemoryStatistic::addValue(int64_t sample_value) {
  samples_.push_back(sample_value);
  streaming_stats_->addValue(sample_value);
}

uint64_t InMemoryStatistic::count() const {
  ASSERT(streaming_stats_->count() == samples_.size());
  return streaming_stats_->count();
}
double InMemoryStatistic::mean() const { return streaming_stats_->mean(); }
double InMemoryStatistic::variance() const { return streaming_stats_->variance(); }
double InMemoryStatistic::stdev() const { return streaming_stats_->stdev(); }

std::unique_ptr<InMemoryStatistic> InMemoryStatistic::combine(const InMemoryStatistic& b) {
  auto combined = std::make_unique<InMemoryStatistic>();

  combined->samples_.insert(combined->samples_.end(), this->samples_.begin(), this->samples_.end());
  combined->samples_.insert(combined->samples_.end(), b.samples_.begin(), b.samples_.end());
  combined->streaming_stats_ = this->streaming_stats_->combine(*b.streaming_stats_);
  return combined;
}

void InMemoryStatistic::dumpToStdOut(std::string header) { ENVOY_LOG(info, "{}", header); }

HdrStatistic::HdrStatistic() : histogram_(nullptr) {
  // Upper bound of 60 seconds (tracking in nanoseconds).
  const int64_t max_latency = 1000L * 1000 * 1000 * 60;

  int status =
      hdr_init(1 /* min trackable value */, max_latency, 3 /* significant digits */, &histogram_);
  if (status != 0) {
    ENVOY_LOG(error, "Failed to intialize HdrHistogram.");
    histogram_ = nullptr;
  }
}

HdrStatistic::~HdrStatistic() {
  if (histogram_ != nullptr) {
    hdr_close(histogram_);
    histogram_ = nullptr;
  }
}

void HdrStatistic::addValue(int64_t value) {
  if (histogram_ != nullptr) {
    if (!hdr_record_value(histogram_, value)) {
      ENVOY_LOG(warn, "Failed to record value into HdrHistogram.");
    }
  }
}

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
  if (histogram_ == nullptr) {
    return 0;
  }
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

std::unique_ptr<HdrStatistic> HdrStatistic::combine(const HdrStatistic& b) {
  auto combined = std::make_unique<HdrStatistic>();

  if (this->histogram_ == nullptr || b.histogram_ == nullptr) {
    return combined;
  }

  int dropped;
  dropped = hdr_add(combined->histogram_, this->histogram_);
  dropped += hdr_add(combined->histogram_, b.histogram_);
  if (dropped > 0) {
    ENVOY_LOG(warn, "Combining HdrHistograms dropped values.");
  }
  return combined;
}

std::unique_ptr<HdrStatistic> HdrStatistic::getCorrected(Frequency frequency) {
  auto h = std::make_unique<HdrStatistic>();
  if (this->histogram_ == nullptr) {
    return h;
  }
  int dropped = hdr_add_while_correcting_for_coordinated_omission(
      h->histogram_, this->histogram_,
      std::chrono::duration_cast<std::chrono::microseconds>(frequency.interval()).count());
  if (dropped > 0) {
    ENVOY_LOG(warn, "Dropped values while getting the corrected HdrStatistics.");
  }
  return h;
}

void HdrStatistic::dumpToStdOut(std::string header) {
  if (histogram_ == nullptr) {
    ENVOY_LOG(warn, "HdrHistogram latencies could not be printed.");
    return;
  }
  ENVOY_LOG(info, "{}", header);
  ENVOY_LOG(info, "{:>12} {:>14} (us)", "Percentile", "Latency");

  std::vector<double> percentiles{50.0, 75.0, 90.0, 99.0, 99.9, 99.99, 99.999, 100.0};
  for (uint64_t i = 0; i < percentiles.size(); i++) {
    double p = percentiles[i];
    int64_t n = hdr_value_at_percentile(histogram_, p);

    // We scale from nanoseconds to microseconds in the output.
    ENVOY_LOG(info, "{:>12}% {:>14}", p, n / 1000.0);
  }
}

void HdrStatistic::percentilesToProto(nighthawk::client::Output& output, bool corrected) {
  struct hdr_iter iter;
  struct hdr_iter_percentiles* percentiles;
  hdr_iter_percentile_init(&iter, histogram_, 5 /*ticks_per_half_distance*/);

  percentiles = &iter.specifics.percentiles;
  while (hdr_iter_next(&iter)) {
    nighthawk::client::Percentile* percentile;

    if (corrected) {
      percentile = output.add_corrected_percentiles();
    } else {
      percentile = output.add_uncorrected_percentiles();
    }

    percentile->mutable_latency()->set_nanos(iter.highest_equivalent_value);
    percentile->set_percentile(percentiles->percentile / 100.0);
    percentile->set_count(iter.cumulative_count);
    percentile->set_inverted_percentile(1.0 / (1.0 - (percentiles->percentile / 100.0)));
  }
}

} // namespace Nighthawk