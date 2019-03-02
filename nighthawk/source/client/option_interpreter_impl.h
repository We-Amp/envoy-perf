#pragma once

#include "nighthawk/client/option_interpreter.h"

#include "nighthawk/client/benchmark_client.h"

namespace Nighthawk {
namespace Client {

class OptionInterpreterImpl : public OptionInterpreter {
public:
  OptionInterpreterImpl(const Options& options);
  std::unique_ptr<BenchmarkClient>
  createBenchmarkClient(Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher) override;

  std::unique_ptr<Envoy::Stats::Store> createStatsStore() override;

private:
  const Options& options_;
};

} // namespace Client
} // namespace Nighthawk
