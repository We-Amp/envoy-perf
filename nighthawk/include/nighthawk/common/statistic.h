#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Nighthawk {

/**
 * Abstract interface for a statistic.
 */
template <class T> class Statistic {
public:
  virtual ~Statistic() = default;
  /**
   * Method for adding a sample value.
   * @param value the value of the sample to add
   */
  virtual void addValue(int64_t sample_value) PURE;

  virtual uint64_t count() const PURE;
  virtual double mean() const PURE;
  virtual double variance() const PURE;
  virtual double stdev() const PURE;

  /**
   * Combines two Statistics into one, and returns a new, merged, Statistic.
   * This is useful for computing results from multiple workers into a
   * single global view.
   * @param a The Statistic that should be combined with this instance.
   * @return T Merged Statistic instance.
   */
  virtual std::unique_ptr<T> combine(const T& a) PURE;

  /**
   * Only used in tests to match expectations to the right precision level.
   * @return true Computed values should be considered as high precision in tests.
   * @return false Computed values should be considered as less precise in tests.
   */
  virtual bool is_high_precision() { return true; }

  /**
   * Dumps a representation of the statistic in plain text to stdout.
   */
  virtual void dumpToStdOut(std::string header) PURE;
};

} // namespace Nighthawk