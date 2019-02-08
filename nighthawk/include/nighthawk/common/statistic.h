#pragma once

#include <cstdint>

#include "envoy/common/pure.h"

namespace Nighthawk {

/**
 * Interface for tracking a statistic, encapsulating
 */
class Statistic {
public:
  virtual ~Statistic() = default;
  /**
   * Method for adding a sample value.
   * @param value the value of the sample to add
   */
  virtual void AddSample(int64_t sample_value) PURE;
};

}