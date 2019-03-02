#include "nighthawk/source/client/worker_impl.h"

#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "nighthawk/client/benchmark_client.h"

#include "nighthawk/source/client/option_interpreter_impl.h"
#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

WorkerImpl::WorkerImpl(Envoy::Api::Api& api, Envoy::ThreadLocal::Instance& tls)
    : thread_factory_(api.threadFactory()), dispatcher_(api.allocateDispatcher()), tls_(tls),
      store_(std::make_unique<Envoy::Stats::IsolatedStoreImpl>()),
      generator_(std::make_unique<Envoy::Runtime::RandomGeneratorImpl>()),
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
                                   Envoy::ThreadLocal::Instance& tls, const Options& options,
                                   int worker_number, uint64_t start_delay_usec)
    : WorkerImpl(api, tls), worker_number_(worker_number), start_delay_usec_(start_delay_usec),
      options_(options) {
  benchmark_client_ = option_interpreter.createBenchmarkClient(api, *dispatcher_);
}

void WorkerClientImpl::work() {
  OptionInterpreterImpl option_interpreter(options_);
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

  // TODO(oschaaf): lifetime of the latform util.
  auto platform_util = option_interpreter.getPlatformUtil();
  sequencer_.reset(new SequencerImpl(*platform_util, *dispatcher_, time_source_, rate_limiter, f,
                                     option_interpreter.createStatistic(),
                                     option_interpreter.createStatistic(), options_.duration(),
                                     options_.timeout()));

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
            std::get<1>(sequencer_->statistics().front()).mean() / 1000, 2,
            std::get<1>(sequencer_->statistics().back()).pstdev() / 1000, 2,
            benchmark_client_->countersToString(filter), worker_percentiles);

  benchmark_client_->terminate();
  dispatcher_->exit();
}

const std::vector<NamedStatistic> WorkerClientImpl::statistics() const {
  std::vector<NamedStatistic> statistics;

  // TODO(oschaaf): std::insert.
  for (auto tuple : benchmark_client_->statistics()) {
    statistics.push_back(tuple);
  }
  for (auto tuple : sequencer_->statistics()) {
    statistics.push_back(tuple);
  }

  return statistics;
}

} // namespace Client
} // namespace Nighthawk