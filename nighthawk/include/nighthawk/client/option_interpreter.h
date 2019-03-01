#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/store.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/client/options.h"

namespace Nighthawk {
namespace Client {

/**
 * Responsible option handling, meaning construction and configuration of targeted
 * classes/instances.
 */
class OptionInterpreter {
public:
  virtual ~OptionInterpreter() = default;
  virtual std::unique_ptr<BenchmarkClient>
  createBenchmarkClient(Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher,
                        Envoy::Event::TimeSystem& time_system, const Options& options) PURE;
};

} // namespace Client
} // namespace Nighthawk
