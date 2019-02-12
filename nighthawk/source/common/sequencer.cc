#include "nighthawk/common/exception.h"

#include "common/common/assert.h"

#include "nighthawk/source/common/sequencer.h"

using namespace std::chrono_literals;

namespace Nighthawk {

const std::chrono::milliseconds Sequencer::ENVOY_TIMER_MIN_RES = 1ms;

Sequencer::Sequencer(Envoy::Event::Dispatcher& dispatcher, Envoy::TimeSource& time_source,
                     RateLimiter& rate_limiter, SequencerTarget& target,
                     std::chrono::microseconds duration, std::chrono::microseconds grace_timeout)
    : dispatcher_(dispatcher), time_source_(time_source), rate_limiter_(rate_limiter),
      target_(target), duration_(duration), grace_timeout_(grace_timeout),
      start_(time_source.monotonicTime().min()), targets_initiated_(0), targets_completed_(0) {
  if (target_ == nullptr) {
    throw NighthawkException("Sequencer must be constructed with a SequencerTarget.");
  }
  periodic_timer_ = dispatcher_.createTimer([this]() { run(true); });
  incidental_timer_ = dispatcher_.createTimer([this]() { run(false); });
}

void Sequencer::start() {
  start_ = time_source_.monotonicTime();
  run(true);
}

void Sequencer::scheduleRun() { periodic_timer_->enableTimer(ENVOY_TIMER_MIN_RES); }

void Sequencer::stop() {
  periodic_timer_->disableTimer();
  incidental_timer_->disableTimer();
  dispatcher_.exit();
}

void Sequencer::run(bool from_timer) {
  const auto now = time_source_.monotonicTime();

  if ((now - start_) > (duration_)) {
    auto rate = completions_per_second();

    if (targets_completed_ == targets_initiated_) {
      stop();
      ENVOY_LOG(trace,
                "Sequencer done processing {} operations in {} ms. (completion rate {} per second)",
                targets_completed_,
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count(), rate);
      return;
    } else {
      // We wait until all due responses are in or the grace period times out.
      if (((now - start_) - duration_) > grace_timeout_) {
        stop();
        ENVOY_LOG(warn,
                  "Sequencer timeout waiting for due responses. Initiated: {} / Completed: {}. "
                  "(completion ~ rate {} per second.)",
                  targets_initiated_, targets_completed_, rate);
        return;
      }
      if (from_timer) {
        scheduleRun();
      }
    }
    return;
  }

  while (rate_limiter_.tryAcquireOne()) {
    bool ok = target_([this, now]() {
      auto dur = time_source_.monotonicTime() - now;
      latency_statistic_.addValue(dur.count());
      targets_completed_++;
      incidental_timer_->enableTimer(0ms);
    });
    if (ok) {
      targets_initiated_++;
    } else {
      // TODO(oschaaf): this is where we run into coordinated omission.
      rate_limiter_.releaseOne();
      break;
    }
  }

  if (!from_timer) {
    if (targets_initiated_ == targets_completed_) {
      // We saturated the rate limiter, and there's no outstanding work.
      // That means it looks like we are idle. Spin this event to improve
      // accuracy.
      // TODO(oschaaf): ideally we would have much finer grained timers instead.
      pthread_yield();
      incidental_timer_->enableTimer(0ms);
    }
  } else {
    scheduleRun();
  }
}

void Sequencer::waitForCompletion() { dispatcher_.run(Envoy::Event::Dispatcher::RunType::Block); }

} // namespace Nighthawk
