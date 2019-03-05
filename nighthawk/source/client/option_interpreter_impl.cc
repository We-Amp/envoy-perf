#include "nighthawk/source/client/option_interpreter_impl.h"

#include "common/stats/isolated_store_impl.h"

#include "nighthawk/source/client/benchmark_client_impl.h"
#include "nighthawk/source/common/platform_util_impl.h"
#include "nighthawk/source/common/statistic_impl.h"

namespace Nighthawk {
namespace Client {

OptionInterpreterImpl::OptionInterpreterImpl(const Options& options) : options_(options) {}

std::unique_ptr<BenchmarkClient>
OptionInterpreterImpl::createBenchmarkClient(Envoy::Api::Api& api,
                                             Envoy::Event::Dispatcher& dispatcher) {
  auto benchmark_client = std::make_unique<BenchmarkClientHttpImpl>(
      api, dispatcher, createStatsStore(), createStatistic(), createStatistic(), options_.uri(),
      options_.h2());
  benchmark_client->set_connection_timeout(options_.timeout());
  benchmark_client->set_connection_limit(options_.connections());
  return benchmark_client;
};

Envoy::Stats::StorePtr OptionInterpreterImpl::createStatsStore() {
  return std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
}

StatisticPtr OptionInterpreterImpl::createStatistic() {
  auto statistic = std::make_unique<HdrStatistic>();
  return statistic;
}

PlatformUtilPtr OptionInterpreterImpl::getPlatformUtil() {
  // TODO(oschaaf): singleton?
  return std::make_unique<PlatformUtilImpl>();
}

} // namespace Client
} // namespace Nighthawk
