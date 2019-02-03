#pragma once

#include "common/common/logger.h"
#include "envoy/common/time.h"

#include "nighthawk/common/rate_limiter.h"

#include "common/frequency.h"

namespace Nighthawk {

class RateLimiter {
public:
  virtual ~RateLimiter() = default;
  virtual bool tryAcquireOne() PURE;
  virtual void releaseOne() PURE;
};

} // namespace Nighthawk