#include <chrono>
#include <memory>

#include "gmock/gmock.h"

#include "nighthawk/test/mocks.h"

using namespace std::chrono_literals;

namespace Nighthawk {

SimulatedTimeAwarePlatformUtil::SimulatedTimeAwarePlatformUtil() : time_source_(nullptr) {}

SimulatedTimeAwarePlatformUtil::~SimulatedTimeAwarePlatformUtil() = default;

void SimulatedTimeAwarePlatformUtil::yieldCurrentThread() const {
  ASSERT(time_source_ != nullptr);
  time_source_->setMonotonicTime(time_source_->monotonicTime() + TimeResolution);
}
void SimulatedTimeAwarePlatformUtil::setTimeSystem(Envoy::Event::SimulatedTimeSystem& time_source) {
  time_source_ = &time_source;
}

MockPlatformUtil::MockPlatformUtil() { delegateToSimulatedTimeAwarePlatformUtil(); }

MockPlatformUtil::~MockPlatformUtil() = default;

void MockPlatformUtil::yieldWithSimulatedTime() const {
  SimulatedTimeAwarePlatformUtil::yieldCurrentThread();
}

void MockPlatformUtil::delegateToSimulatedTimeAwarePlatformUtil() {}

MockRateLimiter::MockRateLimiter() = default;

MockRateLimiter::~MockRateLimiter() = default;

FakeSequencerTarget::FakeSequencerTarget() = default;

FakeSequencerTarget::~FakeSequencerTarget() = default;

MockSequencerTarget::MockSequencerTarget() = default;

MockSequencerTarget::~MockSequencerTarget() = default;

} // namespace Nighthawk