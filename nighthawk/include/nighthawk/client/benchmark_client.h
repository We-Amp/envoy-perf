#pragma once

#include "common/runtime/runtime_impl.h"

#include "nighthawk/common/statistic.h"

namespace Nighthawk {
namespace Client {

class BenchmarkClient {
public:
  virtual ~BenchmarkClient() = default;

  /**
   * Initialize will be called on the worker thread after it has started.
   * @param runtime
   */
  virtual void initialize(Envoy::Runtime::LoaderImpl& runtime) PURE;

  virtual void terminate() PURE;

  virtual bool measureLatencies() const PURE;

  virtual void setMeasureLatencies(bool measure_latencies) PURE;

  virtual const std::vector<std::tuple<std::string, const Statistic&>> statistics() const PURE;

  // TODO(oschaaf): Consider where this belongs. E.g. "Sequenceable" ?
  // In this interface we would then have "bool startRequest()" or some such, which the sequenceable
  // interface implementation should then redirect to.
  virtual bool tryStartOne(std::function<void()> caller_completion_callback) PURE;
};

} // namespace Client
} // namespace Nighthawk