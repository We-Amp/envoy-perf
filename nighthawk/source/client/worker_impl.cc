#include "nighthawk/source/client/worker_impl.h"

#include "common/api/api_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "nighthawk/source/client/benchmark_http_client.h"
#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/platform_util_impl.h"
#include "nighthawk/source/common/rate_limiter_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

WorkerImpl::WorkerImpl(Envoy::Thread::ThreadFactoryImplPosix& thread_factory,
                       const Options& options, const int worker_number)
    : thread_factory_(thread_factory), worker_number_(worker_number), options_(options),
      started_(false), completed_(false) {}

WorkerImpl::~WorkerImpl() {}

void WorkerImpl::start() {
  ASSERT(!started_ && !completed_);
  started_ = true;
  thread_ = thread_factory_.createThread([this]() { work(); });
}
void WorkerImpl::work() {
  auto store_ = std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
  auto api_ = std::make_unique<Envoy::Api::Impl>(1000ms /*flush interval*/, thread_factory_,
                                                 *store_, time_system_);
  auto dispatcher_ = api_->allocateDispatcher();

  // TODO(oschaaf): propertly init tls_.
  Envoy::ThreadLocal::InstanceImpl tls_;
  Envoy::Runtime::LoaderImpl runtime_(generator_, *store_, tls_);
  PlatformUtilImpl platform_util;
  benchmark_http_client_ = std::make_unique<BenchmarkHttpClient>(
      *dispatcher_, *store_, time_system_, options_.uri(),
      std::make_unique<Envoy::Http::HeaderMapImpl>(), options_.h2());

  benchmark_http_client_->set_connection_timeout(options_.timeout());
  benchmark_http_client_->set_connection_limit(options_.connections());
  benchmark_http_client_->initialize(runtime_);

  ENVOY_LOG(debug, "> worker {}: warming up.", worker_number_);

  for (int i = 0; i < 5; i++) {
    benchmark_http_client_->tryStartOne([&dispatcher_] { dispatcher_->exit(); });
  }
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

  // We try to offset the start of each thread so that workers will execute tasks evenly spaced
  // in time.
  double rate = 1 / double(options_.requests_per_second()) / worker_number_;
  int64_t spread_us = static_cast<int64_t>(rate * worker_number_ * 1000000);
  ENVOY_LOG(debug, "> worker {}: Delay start of worker for {} us.", worker_number_, spread_us);
  if (spread_us) {
    // TODO(oschaaf): We could use dispatcher to sleep, but currently it has a 1 ms resolution
    // which is rather coarse for our purpose here.
    usleep(spread_us);
  }
  benchmark_http_client_->setMeasureLatencies(true);

  LinearRateLimiter rate_limiter(time_system_, Frequency(options_.requests_per_second()));
  SequencerTarget f = std::bind(&BenchmarkHttpClient::tryStartOne, benchmark_http_client_.get(),
                                std::placeholders::_1);
  sequencer_.reset(new SequencerImpl(platform_util, *dispatcher_, time_system_, rate_limiter, f,
                                     options_.duration(), options_.timeout()));

  sequencer_->start();
  sequencer_->waitForCompletion();

  std::string worker_percentiles = fmt::format(
      "Internal plus connection setup latency percentiles:\n{}\nRequest/response latency "
      "percentiles:\n{}",
      benchmark_http_client_->connectionStatistic().toString(),
      benchmark_http_client_->responseStatistic().toString());

  ENVOY_LOG(debug,
            "> worker {}: {:.{}f}/second. Mean: {:.{}f}μs. pstdev: "
            "{:.{}f}μs. "
            "Connections good/bad/overflow: {}/{}/{}. Replies: good/fail:{}/{}. Stream "
            "resets: {}.\n {}",
            worker_number_, sequencer_->completionsPerSecond(), 2,
            sequencer_->latencyStatistic().mean() / 1000, 2,
            sequencer_->latencyStatistic().pstdev() / 1000, 2,
            store_->counter("nighthawk.upstream_cx_total").value(),
            store_->counter("nighthawk.upstream_cx_connect_fail").value(),
            benchmark_http_client_->pool_overflow_failures(),
            benchmark_http_client_->http_good_response_count(),
            benchmark_http_client_->http_bad_response_count(),
            benchmark_http_client_->stream_reset_count(), worker_percentiles);
  // Drop everything that is outstanding by resetting the client.
  benchmark_http_client_.reset();
  // TODO(oschaaf): shouldn't be doing this here, properly init tls_
  tls_.shutdownGlobalThreading();
}

void WorkerImpl::waitForCompletion() {
  ASSERT(started_ && !completed_);
  completed_ = true;
  thread_->join();
}

const Sequencer& WorkerImpl::sequencer() const {
  // TODO(oschaaf): reconsider.
  ASSERT(started_ && completed_);
  return *sequencer_;
}

const BenchmarkHttpClient& WorkerImpl::benchmark_http_client() const {
  // TODO(oschaaf): reconsider.
  ASSERT(started_ && completed_);
  return *benchmark_http_client_;
}

} // namespace Client
} // namespace Nighthawk