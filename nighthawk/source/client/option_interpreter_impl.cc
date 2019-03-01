#include "nighthawk/source/client/option_interpreter_impl.h"

#include "nighthawk/source/client/benchmark_client_impl.h"

namespace Nighthawk {
namespace Client {

std::unique_ptr<BenchmarkClient> OptionInterpreterImpl::createBenchmarkClient(
    Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher,
    Envoy::Event::TimeSystem& time_system, const Options& options) {
  auto benchmark_client = std::make_unique<BenchmarkHttpClient>(api, dispatcher, time_system,
                                                                options.uri(), options.h2());
  benchmark_client->set_connection_timeout(options.timeout());
  benchmark_client->set_connection_limit(options.connections());
  return benchmark_client;
};

} // namespace Client
} // namespace Nighthawk
