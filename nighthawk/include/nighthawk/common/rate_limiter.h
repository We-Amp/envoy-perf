#pragma once

namespace Nighthawk {

class RateLimiter {
public:
  virtual ~RateLimiter() = default;
  virtual bool tryAcquireOne() PURE;
  virtual void releaseOne() PURE;
};

} // namespace Nighthawk