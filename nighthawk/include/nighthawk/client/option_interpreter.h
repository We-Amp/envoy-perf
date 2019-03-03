#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/store.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/client/options.h"
#include "nighthawk/common/platform_util.h"
#include "nighthawk/common/statistic.h"

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
  createBenchmarkClient(Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher) PURE;

  virtual std::unique_ptr<Envoy::Stats::Store> createStatsStore() PURE;
  virtual std::unique_ptr<Statistic> createStatistic(std::string id) PURE;
  virtual std::unique_ptr<PlatformUtil> getPlatformUtil() PURE;
};

} // namespace Client
} // namespace Nighthawk
