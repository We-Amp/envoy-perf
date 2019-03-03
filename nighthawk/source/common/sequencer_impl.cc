#include "nighthawk/source/common/sequencer_impl.h"

#include "common/common/assert.h"

#include "nighthawk/common/exception.h"
#include "nighthawk/common/platform_util.h"

using namespace std::chrono_literals;

namespace Nighthawk {

SequencerImpl::SequencerImpl(PlatformUtil& platform_util, Envoy::Event::Dispatcher& dispatcher,
                             Envoy::TimeSource& time_source, RateLimiter& rate_limiter,
                             SequencerTarget& target, StatisticPtr&& latency_statistic,
                             StatisticPtr&& blocked_statistic, std::chrono::microseconds duration,
                             std::chrono::microseconds grace_timeout)
    : target_(target), platform_util_(platform_util), dispatcher_(dispatcher),
      time_source_(time_source), rate_limiter_(rate_limiter),
      latency_statistic_(std::move(latency_statistic)),
      blocked_statistic_(std::move(blocked_statistic)), duration_(duration),
      grace_timeout_(grace_timeout), start_(time_source.monotonicTime().min()),
      targets_initiated_(0), targets_completed_(0), running_(false), blocked_(false) {
  ASSERT(target_ != nullptr, "No SequencerTarget");
  periodic_timer_ = dispatcher_.createTimer([this]() { run(true); });
  spin_timer_ = dispatcher_.createTimer([this]() { run(false); });
  // TODO(oschaaf): wire in statistics factory and set up latency/blocked Statistic fields.
}

void SequencerImpl::start() {
  ASSERT(!running_);
  running_ = true;
  start_ = time_source_.monotonicTime();
  run(true);
}

void SequencerImpl::scheduleRun() { periodic_timer_->enableTimer(EnvoyTimerMinResolution); }

void SequencerImpl::stop(bool timed_out) {
  const double rate = completionsPerSecond();

  ASSERT(running_);
  running_ = false;
  periodic_timer_->disableTimer();
  spin_timer_->disableTimer();
  periodic_timer_.reset();
  spin_timer_.reset();
  dispatcher_.exit();
  updateStatisticOnUnblockIfNeeded(time_source_.monotonicTime());

  if (timed_out) {
    ENVOY_LOG(warn,
              "Timeout waiting for inbound completions. Initiated: {} / Completed: {}. "
              "(Completion rate was {} per second.)",
              targets_initiated_, targets_completed_, rate);

  } else {
    ENVOY_LOG(
        trace, "Processed {} operations in {} ms. ({} per second)", targets_completed_,
        std::chrono::duration_cast<std::chrono::milliseconds>(time_source_.monotonicTime() - start_)
            .count(),
        rate);
  }
}

void SequencerImpl::updateStatisticOnUnblockIfNeeded(const Envoy::MonotonicTime& now) {
  if (blocked_) {
    blocked_ = false;
    blocked_statistic_->addValue((now - blocked_start_).count());
  }
}

void SequencerImpl::updateStartBlockingTimeIfNeeded() {
  if (!blocked_) {
    blocked_ = true;
    blocked_start_ = time_source_.monotonicTime();
  }
}

void SequencerImpl::run(bool from_periodic_timer) {
  ASSERT(running_);
  const auto now = time_source_.monotonicTime();
  const auto running_duration = now - start_;

  // If we exceed the benchmark duration.
  if (running_duration > duration_) {
    if (targets_completed_ == targets_initiated_) {
      // All work has completed. Stop this sequencer.
      stop(false);
    } else {
      // After the benchmark duration has passed, we wait for a grace period for outstanding work
      // to wrap up. If that takes too long we warn about it and quit.
      if (running_duration - duration_ > grace_timeout_) {
        stop(true);
        return;
      }
      if (from_periodic_timer) {
        scheduleRun();
      }
    }
    return;
  }

  while (rate_limiter_.tryAcquireOne()) {
    // The rate limiter says it's OK to proceed and call the target. Let's see if the target is OK
    // with that as well.
    const bool target_could_start = target_([this, now]() {
      const auto dur = time_source_.monotonicTime() - now;
      latency_statistic_->addValue(dur.count());
      targets_completed_++;
      // Immediately schedule us to check again, as chances are we can get on with the next task.
      spin_timer_->enableTimer(0ms);
    });

    if (target_could_start) {
      updateStatisticOnUnblockIfNeeded(now);
      targets_initiated_++;
    } else {
      // This should only happen when we are running in closed-loop mode.The target wasn't able to
      // proceed. Update the rate limiter.
      updateStartBlockingTimeIfNeeded();
      rate_limiter_.releaseOne();
      // Retry later. When all target_ calls have completed we are going to spin until target_
      // stops returning false. Otherwise the periodic timer will wake us up to re-check.
      break;
    }
  }

  if (from_periodic_timer) {
    // Re-schedule the periodic timer if it was responsible for waking up this code.
    scheduleRun();
  } else {
    if (targets_initiated_ == targets_completed_) {
      // We saturated the rate limiter, and there's no outstanding work.
      // That means it looks like we are idle. Spin this event to improve
      // accuracy. As a side-effect, this may help prevent CPU frequency scaling
      // due to c-state changes. But on the other hand it may cause thermal throttling.
      // TODO(oschaaf): Ideally we would have much finer grained timers instead.
      // TODO(oschaaf): Optionize performing this spin loop.
      platform_util_.yieldCurrentThread();
      spin_timer_->enableTimer(0ms);
    }
  }
}

void SequencerImpl::waitForCompletion() {
  ASSERT(running_);
  dispatcher_.run(Envoy::Event::Dispatcher::RunType::Block);
  // We should guarantee the flow terminates, so:
  ASSERT(!running_);
}

StatisticPtrVector SequencerImpl::statistics() const {
  StatisticPtrVector statistics;
  statistics.push_back(latency_statistic_.get());
  statistics.push_back(blocked_statistic_.get());
  return statistics;
};

} // namespace Nighthawk
