#include "nighthawk/source/client/worker_impl.h"

#include "common/api/api_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/platform_util_impl.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

// TODO(oschaaf): Fix argument ordering here.
WorkerImpl::WorkerImpl(Envoy::ThreadLocal::Instance& tls, Envoy::Event::DispatcherPtr&& dispatcher,
                       Envoy::Thread::ThreadFactory& thread_factory, const Options& options,
                       int worker_number, uint64_t start_delay_usec,
                       std::unique_ptr<BenchmarkClient>&& benchmark_client)
    : tls_(tls), dispatcher_(std::move(dispatcher)), thread_factory_(thread_factory),
      worker_number_(worker_number), start_delay_usec_(start_delay_usec), options_(options),
      started_(false), completed_(false), benchmark_client_(std::move(benchmark_client)) {
  tls_.registerThread(*dispatcher_, false);
  runtime_ = std::make_unique<Envoy::Runtime::LoaderImpl>(generator_, store_, tls_);
}

WorkerImpl::~WorkerImpl() { tls_.shutdownThread(); }

void WorkerImpl::start() {
  ASSERT(!started_ && !completed_);
  started_ = true;
  thread_ = thread_factory_.createThread([this]() { work(); });
}
void WorkerImpl::work() {
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
  // which is rather coarse for our purpose here. Probably it would be better to provide an absolute
  // starting time and wait for that in the (spin loop of the) sequencer implementation for high
  // accuracy.
  usleep(start_delay_usec_);

  LinearRateLimiter rate_limiter(time_system_, Frequency(options_.requests_per_second()));
  SequencerTarget f =
      std::bind(&BenchmarkClient::tryStartOne, benchmark_client_.get(), std::placeholders::_1);
  sequencer_.reset(new SequencerImpl(platform_util, *dispatcher_, time_system_, rate_limiter, f,
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

  /*
    ENVOY_LOG(info,
              "> worker {}: {:.{}f}/second. Mean: {:.{}f}μs. pstdev: "
              "{:.{}f}μs. "
              "Connections good/bad/overflow: {}/{}/{}. Replies: good/fail:{}/{}. Stream "
              "resets: {}.\n {}",
              worker_number_, sequencer_->completionsPerSecond(), 2,
              sequencer_->latencyStatistic().mean() / 1000, 2,
              sequencer_->latencyStatistic().pstdev() / 1000, 2,
              store_.counter("nighthawk.upstream_cx_total").value(),
              store_.counter("nighthawk.upstream_cx_connect_fail").value(),
              benchmark_client_->pool_overflow_failures(),
              benchmark_client_->http_good_response_count(),
              benchmark_client_->http_bad_response_count(), benchmark_client_->stream_reset_count(),
              worker_percentiles);
  */

  // TOOD(oschaaf): Some of the benchmark_client members we call are specific to the
  // HttpBenchmarkClient. Generalize that in the interface, and re-enable after adjusting for that.

  benchmark_client_->terminate();
  dispatcher_->exit();
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

const BenchmarkClient& WorkerImpl::benchmark_client() const {
  // TODO(oschaaf): reconsider.
  ASSERT(started_ && completed_);
  return *benchmark_client_;
}

} // namespace Client
} // namespace Nighthawk