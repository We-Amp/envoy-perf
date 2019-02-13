#include "nighthawk/source/client/client.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>

#include "ares.h"
#include <google/protobuf/util/json_util.h>

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/real_time_system.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "nighthawk/source/client/benchmark_http_client.h"
#include "nighthawk/source/client/options_impl.h"
#include "nighthawk/source/client/output.pb.h"
#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer.h"
#include "nighthawk/source/common/statistic_impl.h"
#include "nighthawk/source/common/utility.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

class WorkerContext : Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  WorkerContext(Envoy::Thread::ThreadFactoryImplPosix& thread_factory, Options& options,
                int worker_number)
      : thread_factory_(thread_factory), worker_number_(worker_number), options_(options) {
    thread_ = thread_factory.createThread([this]() { work(); });
  }

  void work() {
    auto store_ = std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
    auto api_ = std::make_unique<Envoy::Api::Impl>(1000ms /*flush interval*/, thread_factory_,
                                                   *store_, time_system_);
    auto dispatcher_ = api_->allocateDispatcher();

    // TODO(oschaaf): not here.
    Envoy::ThreadLocal::InstanceImpl tls_;
    Envoy::Runtime::LoaderImpl runtime_(generator_, *store_, tls_);

    auto client_ = std::make_unique<BenchmarkHttpClient>(
        *dispatcher_, *store_, time_system_, options_.uri(),
        std::make_unique<Envoy::Http::HeaderMapImpl>(), options_.h2());

    client_->set_connection_timeout(options_.timeout());
    client_->set_connection_limit(options_.connections());
    client_->initialize(runtime_);

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

    // one to warm up.
    client_->tryStartOne([&dispatcher_] { dispatcher_->exit(); });
    dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

    LinearRateLimiter rate_limiter(time_system_, Frequency(options_.requests_per_second()));
    SequencerTarget f =
        std::bind(&BenchmarkHttpClient::tryStartOne, client_.get(), std::placeholders::_1);
    sequencer_.reset(new Sequencer(*dispatcher_, time_system_, rate_limiter, f, options_.duration(),
                                   options_.timeout()));

    sequencer_->start();
    sequencer_->waitForCompletion();

    ENVOY_LOG(info,
              "> worker {}: {:.{}f}/second. Mean: {:.{}f}μs. Stdev: "
              "{:.{}f}μs. "
              "Connections good/bad/overflow: {}/{}/{}. Replies: good/fail:{}/{}. Stream "
              "resets: {}. ",
              worker_number_, sequencer_->completions_per_second(), 2, statistic().mean() / 1000, 2,
              statistic().stdev() / 1000, 2, store_->counter("nighthawk.upstream_cx_total").value(),
              store_->counter("nighthawk.upstream_cx_connect_fail").value(),
              client_->pool_overflow_failures(), client_->http_good_response_count(),
              client_->http_bad_response_count(), client_->stream_reset_count());
    // Drop everything that is outstanding by resetting the client.
    client_.reset();
    // TODO(oschaaf): shouldn't be doing this here.
    tls_.shutdownGlobalThreading();
  }

  void waitForCompletion() { thread_->join(); }
  const HdrStatistic& statistic() { return sequencer_->latency_statistic(); }

private:
  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::Event::RealTimeSystem time_system_;
  std::unique_ptr<Sequencer> sequencer_;
  Envoy::Thread::ThreadFactoryImplPosix& thread_factory_;
  Envoy::Thread::ThreadPtr thread_;
  int worker_number_;
  Options& options_;
};

Main::Main(int argc, const char* const* argv)
    : Main(std::make_unique<Client::OptionsImpl>(argc, argv)) {}

Main::Main(Client::OptionsPtr&& options)
    : options_(std::move(options)), time_system_(std::make_unique<Envoy::Event::RealTimeSystem>()) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Envoy::Event::Libevent::Global::initialize();
  configureComponentLogLevels(spdlog::level::from_str(options_->verbosity()));
}

Main::~Main() { ares_library_cleanup(); }

void Main::configureComponentLogLevels(spdlog::level::level_enum level) {
  // We rely on Envoy's logging infra.
  // TODO(oschaaf): Add options to tweak the log level of the various log tags
  // that are available.
  Envoy::Logger::Registry::setLogLevel(level);
  Envoy::Logger::Logger* logger_to_change = Envoy::Logger::Registry::logger("main");
  logger_to_change->setLevel(level);
}

bool Main::run() {
  // TODO(oschaaf): platform specificity need addressing.
  auto thread_factory = Envoy::Thread::ThreadFactoryImplPosix();
  Envoy::Thread::MutexBasicLockable log_lock;
  auto logging_context = std::make_unique<Envoy::Logger::Context>(
      spdlog::level::from_str(options_->verbosity()), "[%T.%f][%t][%L] %v", log_lock);

  uint32_t cpu_cores_with_affinity = PlatformUtils::determineCpuCoresWithAffinity();
  if (cpu_cores_with_affinity == 0) {
    ENVOY_LOG(warn, "Failed to determine the number of cpus with affinity to our thread.");
    cpu_cores_with_affinity = std::thread::hardware_concurrency();
  }

  bool autoscale = options_->concurrency() == "auto";
  // TODO(oschaaf): Maybe, in the case where the concurrency flag is left out, but
  // affinity is set / we don't have affinity with all cores, we should default to autoscale.
  // (e.g. we are called via taskset).
  uint32_t concurrency = autoscale ? cpu_cores_with_affinity : std::stoi(options_->concurrency());

  if (autoscale) {
    ENVOY_LOG(info, "Detected {} (v)CPUs with affinity..", cpu_cores_with_affinity);
  }

  ENVOY_LOG(info, "Starting {} threads / event loops. Test duration: {} seconds.", concurrency,
            options_->duration().count());
  ENVOY_LOG(info, "Global targets: {} connections and {} calls per second.",
            options_->connections() * concurrency, options_->requests_per_second() * concurrency);

  if (concurrency > 1) {
    ENVOY_LOG(info, "   (Per-worker targets: {} connections and {} calls per second)",
              options_->connections(), options_->requests_per_second());
  }

  // We're going to fire up #concurrency benchmark loops and wait for them to complete.
  std::vector<std::unique_ptr<WorkerContext>> worker_contexts;
  for (uint32_t i = 0; i < concurrency; i++) {
    worker_contexts.push_back(std::make_unique<WorkerContext>(thread_factory, *options_, i));
  }

  auto merged_statistics = std::make_unique<HdrStatistic>();
  for (auto& w : worker_contexts) {
    w->waitForCompletion();
    merged_statistics = merged_statistics->combine(w->statistic());
  }

  if (concurrency > 1) {
    ENVOY_LOG(info, "Global #complete:{}. Mean: {:.{}f}μs. Stdev: {:.{}f}μs.",
              merged_statistics->count(), merged_statistics->mean() / 1000, 2,
              merged_statistics->stdev() / 1000, 2);
  }
  merged_statistics->dumpToStdOut("Uncorrected latencies");
  auto corrected = merged_statistics->getCorrected(Frequency(options_->requests_per_second()));
  corrected->dumpToStdOut("Corrected latencies");

  nighthawk::client::Output output;
  output.set_allocated_options(options_->toCommandLineOptions().release());
  output.set_request_count(merged_statistics->count());
  output.mutable_mean()->set_nanos(merged_statistics->mean());
  output.mutable_stdev()->set_nanos(merged_statistics->stdev());

  struct timeval tv;
  gettimeofday(&tv, NULL);
  output.mutable_timestamp()->set_seconds(tv.tv_sec);
  output.mutable_timestamp()->set_nanos(tv.tv_usec * 1000);
  merged_statistics->percentilesToProto(output, false /* corrected */);
  corrected->percentilesToProto(output, true /* corrected */);

  std::string str;
  google::protobuf::util::JsonPrintOptions options;
  google::protobuf::util::MessageToJsonString(output, &str, options);

  mkdir("measurements", 0777);
  std::ofstream stream;
  int64_t epoch_seconds = std::chrono::system_clock::now().time_since_epoch().count();
  std::string filename = fmt::format("measurements/{}.json", epoch_seconds);
  stream.open(filename);
  stream << str;
  ENVOY_LOG(info, "Done. Wrote {}.", filename);
  for (auto& w : worker_contexts) {
    w.release();
  }
  return true;
}

} // namespace Client
} // namespace Nighthawk
