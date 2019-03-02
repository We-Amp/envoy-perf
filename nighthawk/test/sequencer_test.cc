#include <chrono>
#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "common/api/api_impl.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "nighthawk/test/mocks.h"

#include "nighthawk/common/exception.h"
#include "nighthawk/common/platform_util.h"

#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {

class SequencerTestBase : public testing::Test {
public:
  SequencerTestBase()
      : api_(Envoy::Thread::ThreadFactorySingleton::get(), store_, time_source_),
        dispatcher_(std::make_unique<Envoy::Event::MockDispatcher>()), callback_test_count_(0),
        frequency_(10_Hz),
        interval_(std::chrono::duration_cast<std::chrono::milliseconds>(frequency_.interval())),
        test_number_of_intervals_(5), sequencer_target_(std::bind(&SequencerTestBase::callback_test,
                                                                  this, std::placeholders::_1)),
        clock_updates_(0) {
    platform_util_.setTimeSystem(this->time_source_);
  }

  virtual ~SequencerTestBase() = default;

  void moveClockForwardOneInterval() {
    time_source_.setMonotonicTime(time_source_.monotonicTime() + interval_);
    clock_updates_++;
  }

  bool callback_test(std::function<void()> f) {
    callback_test_count_++;
    f();
    return true;
  }

  MockPlatformUtil platform_util_;
  Envoy::Stats::IsolatedStoreImpl store_;
  Envoy::Event::SimulatedTimeSystem time_source_;
  Envoy::Api::Impl api_;
  std::unique_ptr<Envoy::Event::MockDispatcher> dispatcher_;
  int callback_test_count_;
  const Frequency frequency_;
  const std::chrono::milliseconds interval_;
  const uint64_t test_number_of_intervals_;
  std::unique_ptr<RateLimiter> rate_limiter_;
  SequencerTarget sequencer_target_;
  uint64_t clock_updates_;
};

class SequencerTest : public SequencerTestBase {
public:
  SequencerTest() { rate_limiter_ = std::make_unique<MockRateLimiter>(); }
  virtual MockRateLimiter& getRateLimiter() const {
    return dynamic_cast<MockRateLimiter&>(*rate_limiter_);
  }
};

// Test for defined behaviour with bad input
TEST_F(SequencerTest, EmptyCallbackAsserts) {
  SequencerTarget callback_empty;

  ASSERT_DEATH(SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, getRateLimiter(),
                                       callback_empty, 1s, 1s),
               "No SequencerTarget");
}

// As today the Sequencer supports a single run only, we cannot start twice.
TEST_F(SequencerTest, SingleShotStartingTwiceAsserts) {
  EXPECT_CALL(getRateLimiter(), tryAcquireOne());
  EXPECT_CALL(*dispatcher_, createTimer_(_)).Times(2);

  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, getRateLimiter(),
                          sequencer_target_, 1s, 1s);
  sequencer.start();
  ASSERT_DEATH(sequencer.start(), "");
}

// Waiting on a sequencer flow that isn't started.
TEST_F(SequencerTest, WaitWithoutStartAsserts) {
  EXPECT_CALL(getRateLimiter(), tryAcquireOne()).Times(0);
  EXPECT_CALL(*dispatcher_, createTimer_(_)).Times(2);

  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, getRateLimiter(),
                          sequencer_target_, 1s, 1s);
  ASSERT_DEATH(sequencer.waitForCompletion(), "");
}

class SequencerTestWithTimerEmulation : public SequencerTest {
public:
  SequencerTestWithTimerEmulation() : timer1_set_(false), timer2_set_(false), stopped_(false) {
    setupDispatcherTimerEmulation();
  }

  // the Sequencer implementation is effectively driven by two timers. We set us up for emulating
  // those timers firing and moving simulated time forward in simulateTimerloop() below.
  void setupDispatcherTimerEmulation() {
    timer1_ = new testing::NiceMock<Envoy::Event::MockTimer>();
    timer2_ = new testing::NiceMock<Envoy::Event::MockTimer>();
    EXPECT_CALL(*dispatcher_, createTimer_(_))
        .WillOnce(Invoke([&](Envoy::Event::TimerCb cb) {
          timer_cb_1_ = cb;
          return timer1_;
        }))
        .WillOnce(Invoke([&](Envoy::Event::TimerCb cb) {
          timer_cb_2_ = cb;
          return timer2_;
        }));
    EXPECT_CALL(*timer1_, disableTimer()).WillOnce(Invoke([&]() { timer1_set_ = false; }));
    EXPECT_CALL(*timer2_, disableTimer()).WillOnce(Invoke([&]() { timer2_set_ = false; }));
    EXPECT_CALL(*timer1_, enableTimer(_)).WillRepeatedly(Invoke([&](std::chrono::milliseconds) {
      timer1_set_ = true;
    }));
    EXPECT_CALL(*timer2_, enableTimer(_)).WillRepeatedly(Invoke([&](std::chrono::milliseconds) {
      timer2_set_ = true;
    }));
    EXPECT_CALL(*dispatcher_, exit()).WillOnce(Invoke([&]() { stopped_ = true; }));
    EXPECT_CALL(*dispatcher_, run(_))
        .WillOnce(Invoke([&](Envoy::Event::DispatcherImpl::RunType type) {
          assert(type == Envoy::Event::DispatcherImpl::RunType::Block);
          simulateTimerLoop();
        }));
  }

  // Moves time forward 1ms, and runs the ballbacks of set timers.
  void simulateTimerLoop() {
    while (!stopped_) {
      time_source_.setMonotonicTime(time_source_.monotonicTime() + EnvoyTimerMinResolution);

      // TODO(oschaaf): This can be implemented more accurately, by keeping track of timer
      // enablement preserving ordering of which timer should fire first. For now this seems to
      // suffice for the tests that we have in here.
      if (timer1_set_) {
        timer1_set_ = false;
        timer_cb_1_();
      }

      if (timer2_set_) {
        timer2_set_ = false;
        timer_cb_2_();
      }
    }
  }

private:
  testing::NiceMock<Envoy::Event::MockTimer>* timer1_; // not owned
  testing::NiceMock<Envoy::Event::MockTimer>* timer2_; // not owned
  Envoy::Event::TimerCb timer_cb_1_;
  Envoy::Event::TimerCb timer_cb_2_;
  bool timer1_set_;
  bool timer2_set_;
  bool stopped_;
};

// Basic rate limiter interaction test.
TEST_F(SequencerTestWithTimerEmulation, RateLimiterInteraction) {
  MockSequencerTarget target;

  SequencerTarget callback =
      std::bind(&MockSequencerTarget::callback, &target, std::placeholders::_1);
  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, *rate_limiter_, callback,
                          test_number_of_intervals_ * interval_ /* Sequencer run time.*/,
                          1ms /* Sequencer timeout. */);
  // Have the mock rate limiter gate two calls, and block everything else.
  EXPECT_CALL(getRateLimiter(), tryAcquireOne())
      .Times(testing::AtLeast(3))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(true))
      .WillRepeatedly(testing::Return(false));

  EXPECT_CALL(target, callback(testing::_))
      .Times(2)
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(true));

  sequencer.start();
  sequencer.waitForCompletion();
}

// Saturated rate limiter interaction test.
TEST_F(SequencerTestWithTimerEmulation, RateLimiterSaturatedTargetInteraction) {
  MockSequencerTarget target;

  SequencerTarget callback =
      std::bind(&MockSequencerTarget::callback, &target, std::placeholders::_1);
  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, *rate_limiter_, callback,
                          test_number_of_intervals_ * interval_ /* Sequencer run time.*/,
                          0ms /* Sequencer timeout. */);

  EXPECT_CALL(getRateLimiter(), tryAcquireOne())
      .Times(testing::AtLeast(3))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(true))
      .WillRepeatedly(testing::Return(false));

  EXPECT_CALL(target, callback(testing::_))
      .Times(2)
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(false));

  // The sequencer should call RateLimiter::releaseOne() when the target returns false.
  EXPECT_CALL(getRateLimiter(), releaseOne()).Times(1);

  sequencer.start();
  sequencer.waitForCompletion();
}

// The integration tests use a LinearRateLimiter.
class SequencerIntegrationTest : public SequencerTestWithTimerEmulation {
public:
  SequencerIntegrationTest() {
    rate_limiter_ = std::make_unique<LinearRateLimiter>(time_source_, frequency_);
  }

  bool timeout_test(std::function<void()> /* f */) {
    callback_test_count_++;
    // We don't call f(); which will cause the sequencer to think there is in-flight work.
    return true;
  }
  bool saturated_test(std::function<void()> /* f */) { return false; }
};

TEST_F(SequencerIntegrationTest, TheHappyFlow) {
  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, *rate_limiter_,
                          sequencer_target_, test_number_of_intervals_ * interval_, 1s);

  EXPECT_CALL(platform_util_, yieldCurrentThread()).Times(testing::AtLeast(1));

  EXPECT_EQ(0, callback_test_count_);
  EXPECT_EQ(0, sequencer.latencyStatistic().count());

  sequencer.start();
  sequencer.waitForCompletion();

  EXPECT_EQ(test_number_of_intervals_, callback_test_count_);
  EXPECT_EQ(test_number_of_intervals_, sequencer.latencyStatistic().count());
  EXPECT_EQ(0, sequencer.blockedStatistic().count());
}

// Test an always saturated sequencer target. A concrete example would be a http benchmark client
// not being able to start any requests, for example due to misconfiguration or system conditions.
TEST_F(SequencerIntegrationTest, AlwaysSaturatedTargetTest) {
  SequencerTarget callback =
      std::bind(&SequencerIntegrationTest::saturated_test, this, std::placeholders::_1);
  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, *rate_limiter_, callback,
                          test_number_of_intervals_ * interval_ /* Sequencer run time.*/,
                          1ms /* Sequencer timeout. */);

  sequencer.start();
  sequencer.waitForCompletion();

  EXPECT_EQ(0, sequencer.latencyStatistic().count());
  EXPECT_EQ(1, sequencer.blockedStatistic().count());
}

// Test the (grace-)-timeout feature of the Sequencer. The used sequencer target
// (SequencerIntegrationTest::timeout_test()) will never call back, effectively simulated a
// stalled benchmark client. Implicitly we test  that we get past
// sequencer.waitForCompletion(), which would only hold when sequencer enforces the the timeout.
TEST_F(SequencerIntegrationTest, GraceTimeoutTest) {
  auto grace_timeout = 12345ms;

  SequencerTarget callback =
      std::bind(&SequencerIntegrationTest::timeout_test, this, std::placeholders::_1);
  SequencerImpl sequencer(platform_util_, *dispatcher_, time_source_, *rate_limiter_, callback,
                          test_number_of_intervals_ * interval_ /* Sequencer run time.*/,
                          grace_timeout);

  auto pre_timeout = time_source_.monotonicTime();
  sequencer.start();
  sequencer.waitForCompletion();

  auto diff = time_source_.monotonicTime() - pre_timeout;

  auto expected_duration =
      (test_number_of_intervals_ * interval_) + grace_timeout + EnvoyTimerMinResolution;
  EXPECT_EQ(expected_duration, diff);

  // the test itself should have seen all callbacks...
  EXPECT_EQ(5, callback_test_count_);
  // ... but they ought to have not arrived at the Sequencer.
  EXPECT_EQ(0, sequencer.latencyStatistic().count());
  EXPECT_EQ(0, sequencer.blockedStatistic().count());
}

} // namespace Nighthawk
