#include "nighthawk/source/client/worker_impl.h"

#include "common/stats/isolated_store_impl.h"

#include "nighthawk/client/benchmark_client.h"

#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/platform_util_impl.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

// TODO(oschaaf): probably want to pass in the time_source.
WorkerImpl::WorkerImpl(Envoy::Api::Api& api, Envoy::ThreadLocal::Instance& tls)
    : thread_factory_(api.threadFactory()), dispatcher_(api.allocateDispatcher()), tls_(tls),
      store_(std::make_unique<Envoy::Stats::IsolatedStoreImpl>()), time_source_(api.timeSource()),
      started_(false), completed_(false) {
  tls_.registerThread(*dispatcher_, false);
  runtime_ = std::make_unique<Envoy::Runtime::LoaderImpl>(generator_, *store_, tls_);
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
                                   Envoy::ThreadLocal::Instance& tls, const Options& options,
                                   int worker_number, uint64_t start_delay_usec)
    : WorkerImpl(api, tls), worker_number_(worker_number), start_delay_usec_(start_delay_usec),
      options_(options) {
  benchmark_client_ = option_interpreter.createBenchmarkClient(api, *dispatcher_);
}

void WorkerClientImpl::work() {
  PlatformUtilImpl platform_util;
  benchmark_client_->initialize(*runtime_);

  ENVOY_LOG(debug, "> worker {}: warming up.", worker_number_);

  for (int i = 0; i < 5; i++) {
    benchmark_client_->tryStartOne([this] { dispatcher_->exit(); });
  }

  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
  benchmark_client_->setMeasureLatencies(true);

  ENVOY_LOG(debug, "> worker {}: Delay start of worker for {} us.", worker_number_,
            start_delay_usec_);
  // TODO(oschaaf): We could use dispatcher to sleep, but currently it has a 1 ms resolution
  // which is rather coarse for our purpose here.
  // TODO(oschaaf): Instead of usleep, it would probably be better to provide an absolute
  // starting time and wait for that in the (spin loop of the) sequencer implementation for high
  // accuracy.
  usleep(start_delay_usec_);

  LinearRateLimiter rate_limiter(time_source_, Frequency(options_.requests_per_second()));
  SequencerTarget f =
      std::bind(&BenchmarkClient::tryStartOne, benchmark_client_.get(), std::placeholders::_1);
  sequencer_.reset(new SequencerImpl(platform_util, *dispatcher_, time_source_, rate_limiter, f,
                                     options_.duration(), options_.timeout()));

  sequencer_->start();
  sequencer_->waitForCompletion();

  // TODO(oschaaf): need this to be generic.
  const Statistic& connection_statistic = std::get<1>(benchmark_client_->statistics().front());
  const Statistic& response_statistic = std::get<1>(benchmark_client_->statistics().back());

  std::string worker_percentiles = fmt::format(
      "Internal plus connection setup latency percentiles:\n{}\nRequest/response latency "
      "percentiles:\n{}",
      connection_statistic.toString(), response_statistic.toString());

  CounterFilter filter = [](std::string, uint64_t value) { return value > 0; };
  ENVOY_LOG(info,
            "> worker {}: {:.{}f}/second. Mean: {:.{}f} μs. pstdev: {:.{}f} μs.\n{}\n"
            ".\n {}",
            worker_number_, sequencer_->completionsPerSecond(), 2,
            sequencer_->latencyStatistic().mean() / 1000, 2,
            sequencer_->latencyStatistic().pstdev() / 1000, 2,
            benchmark_client_->countersToString(filter), worker_percentiles);

  // TOOD(oschaaf): Some of the benchmark_client members we call are specific to the
  // HttpBenchmarkClient. Generalize that in the interface, and re-enable after adjusting for that.

  benchmark_client_->terminate();
  dispatcher_->exit();
}

const Sequencer& WorkerClientImpl::sequencer() const { return *sequencer_; }

const BenchmarkClient& WorkerClientImpl::benchmark_client() const { return *benchmark_client_; }

} // namespace Client
} // namespace Nighthawk