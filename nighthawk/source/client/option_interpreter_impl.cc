#include "nighthawk/source/client/option_interpreter_impl.h"

#include "common/stats/isolated_store_impl.h"
#include "nighthawk/source/client/benchmark_client_impl.h"

namespace Nighthawk {
namespace Client {

OptionInterpreterImpl::OptionInterpreterImpl(const Options& options) : options_(options) {}

std::unique_ptr<BenchmarkClient>
OptionInterpreterImpl::createBenchmarkClient(Envoy::Api::Api& api,
                                             Envoy::Event::Dispatcher& dispatcher,
                                             Envoy::Event::TimeSystem& time_system) {
  auto benchmark_client = std::make_unique<BenchmarkHttpClient>(api, dispatcher, time_system,
                                                                options_.uri(), options_.h2());
  benchmark_client->set_connection_timeout(options_.timeout());
  benchmark_client->set_connection_limit(options_.connections());
  return benchmark_client;
};

std::unique_ptr<Envoy::Stats::Store> OptionInterpreterImpl::createStatsStore() {
  return std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
}

} // namespace Client
} // namespace Nighthawk
