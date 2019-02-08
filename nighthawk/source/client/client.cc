#include "nighthawk/source/client/client.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>

#include "ares.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/real_time_system.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "nighthawk/source/client/benchmark_http_client.h"
#include "nighthawk/source/client/options_impl.h"
#include "nighthawk/source/common/frequency.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer.h"
#include "nighthawk/source/common/streaming_stats.h"
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

  // We're going to fire up #concurrency benchmark loops and wait for them to complete.
  std::vector<Envoy::Thread::ThreadPtr> threads;
  std::vector<std::vector<uint64_t>> global_results;
  std::vector<StreamingStats> global_streaming_stats;

  // TODO(oschaaf): Wire up a proper stats sink.
  global_results.reserve(concurrency);
  global_streaming_stats.resize(concurrency);

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

  for (uint32_t i = 0; i < concurrency; i++) {
    global_results.emplace_back(std::vector<uint64_t>());
    std::vector<uint64_t>& results = global_results.at(i);
    // TODO(oschaaf): get us a stats sink.

    results.reserve(options_->duration().count() * options_->requests_per_second());

    // TODO(oschaaf): properly set up and use ThreadLocal::InstanceImpl.
    auto thread = thread_factory.createThread([&, i]() {
      auto store = std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
      auto api = std::make_unique<Envoy::Api::Impl>(1000ms /*flush interval*/, thread_factory,
                                                    *store, *time_system_);
      auto dispatcher = api->allocateDispatcher();
      StreamingStats& streaming_stats = global_streaming_stats[i];

      // TODO(oschaaf): not here.
      Envoy::ThreadLocal::InstanceImpl tls;
      Envoy::Runtime::RandomGeneratorImpl generator;
      Envoy::Runtime::LoaderImpl runtime(generator, *store, tls);
      Envoy::Event::RealTimeSystem time_system;

      auto client = std::make_unique<BenchmarkHttpClient>(
          *dispatcher, *store, time_system, options_->uri(),
          std::make_unique<Envoy::Http::HeaderMapImpl>(), options_->h2());
      client->set_connection_timeout(options_->timeout());
      client->set_connection_limit(options_->connections());
      client->initialize(runtime);

      // We try to offset the start of each thread so that workers will execute tasks evenly spaced
      // in time.
      // TODO(oschaaf): use dispatcher to sleep.
      double rate = 1 / double(options_->requests_per_second()) / concurrency;
      int64_t spread_us = static_cast<int64_t>(rate * i * 1000000);
      ENVOY_LOG(debug, "> worker {}: Delay start of worker for {} us.", i, spread_us);
      if (spread_us) {
        usleep(spread_us);
      }

      // one to warm up.
      client->tryStartOne([&dispatcher] { dispatcher->exit(); });
      dispatcher->run(Envoy::Event::Dispatcher::RunType::Block);

      LinearRateLimiter rate_limiter(time_system, Frequency(options_->requests_per_second()));
      SequencerTarget f =
          std::bind(&BenchmarkHttpClient::tryStartOne, client.get(), std::placeholders::_1);
      Sequencer sequencer(*dispatcher, time_system, rate_limiter, f, options_->duration(),
                          options_->timeout());

      sequencer.set_latency_callback(
          [&results, &streaming_stats](std::chrono::nanoseconds latency) {
            ASSERT(latency.count() > 0);
            results.push_back(latency.count());
            streaming_stats.addValue(latency.count());
          });

      sequencer.start();
      sequencer.waitForCompletion();

      ENVOY_LOG(info,
                "> worker {}: {:.{}f}/second. Mean: {:.{}f}μs. Stdev: "
                "{:.{}f}μs. "
                "Connections good/bad/overflow: {}/{}/{}. Replies: good/fail:{}/{}. Stream "
                "resets: {}. ",
                i, sequencer.completions_per_second(), 2, streaming_stats.mean() / 1000, 2,
                streaming_stats.stdev() / 1000, 2,
                store->counter("nighthawk.upstream_cx_total").value(),
                store->counter("nighthawk.upstream_cx_connect_fail").value(),
                client->pool_overflow_failures(), client->http_good_response_count(),
                client->http_bad_response_count(), client->stream_reset_count());
      client.reset();
      // TODO(oschaaf): shouldn't be doing this here.
      tls.shutdownGlobalThreading();
    });

    threads.push_back(std::move(thread));
  }

  for (auto& t : threads) {
    t->join();
  }

  if (concurrency > 1) {
    StreamingStats merged_global_stats = global_streaming_stats[0];
    for (uint32_t i = 1; i < concurrency; i++) {
      merged_global_stats = merged_global_stats.combine(global_streaming_stats[i]);
    }
    ENVOY_LOG(info, "Global #complete:{}. Mean: {:.{}f}μs. Stdev: {:.{}f}μs.",
              merged_global_stats.count(), merged_global_stats.mean() / 1000, 2,
              merged_global_stats.stdev() / 1000, 2);
  }

  // TODO(oschaaf): proper stats tracking/configuration
  std::ofstream myfile;
  myfile.open("res.txt");

  for (uint32_t i = 0; i < concurrency; i++) {
    std::vector<uint64_t>& results = global_results.at(i);
    for (int r : results) {
      myfile << r << "\n";
    }
  }
  myfile.close();

  ENVOY_LOG(info, "Done. Run 'tools/stats.py res.txt benchmark' for hdrhistogram.");

  return true;
}

} // namespace Client
} // namespace Nighthawk
