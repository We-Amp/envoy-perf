#include "nighthawk/source/client/worker_impl.h"

#include "envoy/thread_local/thread_local.h"

#include "common/runtime/runtime_impl.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/client/option_interpreter.h"

#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

WorkerImpl::WorkerImpl(Envoy::Api::Api& api, Envoy::ThreadLocal::Instance& tls,
                       Envoy::Stats::StorePtr&& store)
    : thread_factory_(api.threadFactory()), dispatcher_(api.allocateDispatcher()), tls_(tls),
      store_(std::move(store)), generator_(std::make_unique<Envoy::Runtime::RandomGeneratorImpl>()),
      time_source_(api.timeSource()), started_(false), completed_(false) {
  tls_.registerThread(*dispatcher_, false);
  runtime_ = std::make_unique<Envoy::Runtime::LoaderImpl>(*generator_, *store_, tls_);
}

WorkerImpl::~WorkerImpl() { tls_.shutdownThread(); }

void WorkerImpl::start() {
  ASSERT(!started_ && !completed_);
  started_ = true;
  thread_ = thread_factory_.createThread([this]() { work(); });
}

void WorkerImpl::waitForCompletion() {
  ASSERT(started_ && !completed_);
  completed_ = true;
  thread_->join();
}

WorkerClientImpl::WorkerClientImpl(OptionInterpreter& option_interpreter, Envoy::Api::Api& api,
                                   Envoy::ThreadLocal::Instance& tls,
                                   Envoy::Stats::StorePtr&& store, const Options& options,
                                   int worker_number, uint64_t start_delay_usec)
    : WorkerImpl(api, tls, std::move(store)), option_interpreter_(option_interpreter),
      options_(options), platform_util_(option_interpreter_.getPlatformUtil()),
      worker_number_(worker_number), start_delay_usec_(start_delay_usec) {
  benchmark_client_ = option_interpreter.createBenchmarkClient(api, *dispatcher_);

  RateLimiterPtr rate_limiter =
      std::make_unique<LinearRateLimiter>(time_source_, Frequency(options_.requests_per_second()));
  SequencerTarget sequencer_target =
      std::bind(&BenchmarkClient::tryStartOne, benchmark_client_.get(), std::placeholders::_1);

  sequencer_.reset(new SequencerImpl(
      *platform_util_, *dispatcher_, time_source_, std::move(rate_limiter), sequencer_target,
      option_interpreter_.createStatistic(), option_interpreter_.createStatistic(),
      options_.duration(), options_.timeout()));
}

void WorkerClientImpl::logResult() {
  std::string worker_percentiles = "{}\n{}";

  for (auto statistic : benchmark_client_->statistics()) {
    worker_percentiles =
        fmt::format(worker_percentiles, statistic.first, statistic.second->toString() + "\n{}\n{}");
  }
  for (auto statistic : sequencer_->statistics()) {
    worker_percentiles =
        fmt::format(worker_percentiles, statistic.first, statistic.second->toString() + "\n{}\n{}");
  }

  worker_percentiles = fmt::format(worker_percentiles, "", "");

  CounterFilter filter = [](std::string, uint64_t value) { return value > 0; };
  ENVOY_LOG(info, "> worker {}\n{}\n{}", worker_number_,
            benchmark_client_->countersToString(filter), worker_percentiles);
}

void WorkerClientImpl::simpleWarmup() {
  ENVOY_LOG(debug, "> worker {}: warming up.", worker_number_);

  for (int i = 0; i < 5; i++) {
    benchmark_client_->tryStartOne([this] { dispatcher_->exit(); });
  }

  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
}

void WorkerClientImpl::delayStart() {
  ENVOY_LOG(debug, "> worker {}: Delay start of worker for {} us.", worker_number_,
            start_delay_usec_);
  // TODO(oschaaf): We could use dispatcher to sleep, but currently it has a 1 ms resolution
  // which is rather coarse for our purpose here.
  // TODO(oschaaf): Instead of usleep, it would probably be better to provide an absolute
  // starting time and wait for that in the (spin loop of the) sequencer implementation for high
  // accuracy.
  usleep(start_delay_usec_);
}

void WorkerClientImpl::work() {
  benchmark_client_->initialize(*runtime_);

  simpleWarmup();

  benchmark_client_->setMeasureLatencies(true);

  delayStart();

  sequencer_->start();
  sequencer_->waitForCompletion();
  logResult();

  dispatcher_->exit();
}

StatisticPtrMap WorkerClientImpl::statistics() const {
  StatisticPtrMap statistics(benchmark_client_->statistics());

  for (auto stat : sequencer_->statistics()) {
    statistics[stat.first] = stat.second;
  }

  return statistics;
}

} // namespace Client
} // namespace Nighthawk