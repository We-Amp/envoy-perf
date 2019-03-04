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
 * Factory-like construct, responsible for constructed a few classes/instances where it is expected
 * configuration needs to be applied. Helps with keeping includes to just the interfaces in other
 * places. Will probably be decomposed into real factory constructs later on.
 */
class OptionInterpreter {
public:
  virtual ~OptionInterpreter() = default;
  virtual std::unique_ptr<BenchmarkClient>
  createBenchmarkClient(Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher) PURE;

  virtual Envoy::Stats::StorePtr createStatsStore() PURE;
  virtual StatisticPtr createStatistic(std::string id) PURE;
  virtual PlatformUtilPtr getPlatformUtil() PURE;
};

typedef std::unique_ptr<OptionInterpreter> OptionInterpreterPtr;

} // namespace Client
} // namespace Nighthawk
