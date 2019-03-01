#include "nighthawk/source/client/client.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>

#include "ares.h"
#include <google/protobuf/util/json_util.h>

#include "common/api/api_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/real_time_system.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "nighthawk/source/client/benchmark_client_impl.h"
#include "nighthawk/source/client/options_impl.h"
#include "nighthawk/source/client/output.pb.h"
#include "nighthawk/source/client/output_formatter_impl.h"
#include "nighthawk/source/client/worker_impl.h"
#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/statistic_impl.h"
#include "nighthawk/source/common/utility.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

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

  Envoy::Stats::IsolatedStoreImpl store;
  Envoy::Api::Impl api(thread_factory, store, *time_system_);
  Envoy::ThreadLocal::InstanceImpl tls;
  Envoy::Event::DispatcherPtr main_dispatcher(api.allocateDispatcher());
  // TODO(oschaaf): later on, fire up and use a main dispatcher loop as need arises.
  tls.registerThread(*main_dispatcher, true);
  Envoy::Runtime::RandomGeneratorImpl generator;
  Envoy::Runtime::LoaderImpl runtime(generator, store, tls);

  // We try to offset the start of each thread so that workers will execute tasks evenly
  // spaced in time.
  // E.g.if we have a 10 workers at 10k/second our global pacing is 100k/second (or 1 / 100 usec).
  // We would then offset the worker starts like [0usec, 10 usec, ..., 90 usec].
  double inter_worker_delay_usec = (1. / options_->requests_per_second()) * 1000000 / concurrency;

  // We're going to fire up #concurrency benchmark loops and wait for them to complete.
  std::vector<WorkerImplPtr> workers;
  for (uint32_t worker_number = 0; worker_number < concurrency; worker_number++) {
    Envoy::Event::DispatcherPtr dispatcher(api.allocateDispatcher());

    auto benchmark_client = std::make_unique<BenchmarkHttpClient>(
        api, std::make_unique<Envoy::Stats::IsolatedStoreImpl>(), *dispatcher, *time_system_,
        options_->uri(), std::make_unique<Envoy::Http::HeaderMapImpl>(), options_->h2());
    benchmark_client->set_connection_timeout(options_->timeout());
    benchmark_client->set_connection_limit(options_->connections());

    workers.push_back(std::make_unique<WorkerImpl>(
        tls, std::move(dispatcher), thread_factory,
        std::make_unique<Envoy::Stats::IsolatedStoreImpl>(), *options_, worker_number,
        inter_worker_delay_usec * worker_number, std::move(benchmark_client)));
  }

  for (auto& w : workers) {
    w->start();
  }

  for (auto& w : workers) {
    w->waitForCompletion();
  }

  std::unique_ptr<Statistic> sequencer_statistic = std::make_unique<HdrStatistic>();
  std::unique_ptr<Statistic> blocked_statistic = std::make_unique<HdrStatistic>();
  std::unique_ptr<Statistic> connection_statistic = std::make_unique<HdrStatistic>();
  std::unique_ptr<Statistic> response_statistic = std::make_unique<HdrStatistic>();

  for (auto& w : workers) {
    sequencer_statistic = sequencer_statistic->combine(w->sequencer().latencyStatistic());
    blocked_statistic = blocked_statistic->combine(w->sequencer().blockedStatistic());
    auto benchmark_client_statistics = w->benchmark_client().statistics();
    connection_statistic =
        connection_statistic->combine(std::get<1>(benchmark_client_statistics.front()));
    response_statistic =
        response_statistic->combine(std::get<1>(benchmark_client_statistics.back()));
  }

  tls.shutdownGlobalThreading();

  if (blocked_statistic->count() > 0) {
    ENVOY_LOG(
        warn,
        "Sequencer observed the target to be blocking on {} calls. Latency statistics are skewed.",
        blocked_statistic->count());
  }

  std::string cli_result = fmt::format(
      "Done.\n********************** Global Statistics ******************** \n"
      "Sequencer timing:\n{}\n"
      "Request/Response   timing:\n{}\n"
      "Connection/queuing timing:\n{}\n"
      "Sequencer blocking:\n{}\n",
      sequencer_statistic->toString(), response_statistic->toString(),
      connection_statistic->toString(),
      blocked_statistic->count() > 0 ? blocked_statistic->toString() : "No blocking was observed.");

  ENVOY_LOG(info, "{}", cli_result);

  nighthawk::client::Output output;
  output.set_allocated_options(options_->toCommandLineOptions().release());

  struct timeval tv;
  gettimeofday(&tv, NULL);
  output.mutable_timestamp()->set_seconds(tv.tv_sec);
  output.mutable_timestamp()->set_nanos(tv.tv_usec * 1000);
  nighthawk::client::Statistic* latency_statistic = output.mutable_latency();
  *latency_statistic = sequencer_statistic->toProto();

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
  return true;
}

} // namespace Client
} // namespace Nighthawk
