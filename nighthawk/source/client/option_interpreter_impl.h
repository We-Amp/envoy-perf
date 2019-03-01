#pragma once

#include "nighthawk/client/option_interpreter.h"

#include "nighthawk/client/benchmark_client.h"

namespace Nighthawk {
namespace Client {

class OptionInterpreterImpl : public OptionInterpreter {
public:
  std::unique_ptr<BenchmarkClient> createBenchmarkClient(Envoy::Api::Api& api,
                                                         Envoy::Event::Dispatcher& dispatcher,
                                                         Envoy::Event::TimeSystem& time_system,
                                                         const Options& options) override;
};

} // namespace Client
} // namespace Nighthawk
