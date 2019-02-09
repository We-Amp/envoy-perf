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

  virtual T combine(const T& a) PURE;
  virtual bool is_high_precision() { return true; }
  virtual std::string toString() PURE;
};

} // namespace Nighthawk