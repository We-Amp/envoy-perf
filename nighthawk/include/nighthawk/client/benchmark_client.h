#pragma once

#include "envoy/runtime/runtime.h"

#include "nighthawk/common/statistic.h"

namespace Nighthawk {
namespace Client {

class BenchmarkClient {
public:
  virtual ~BenchmarkClient() = default;

  /**
   * Initialize will be called on the worker thread after it has started.
   * @param runtime to be used during initialization.
   */
  virtual void initialize(Envoy::Runtime::Loader& runtime) PURE;

  /**
   * Will be called before exitting the worker thread, after it is done using this BenchmarkClient.
   */
  virtual void terminate() PURE;

  /**
   * Turns latency measurement on or off.
   *
   * @param measure_latencies true iff latencies should be measured.
   */
  virtual void setMeasureLatencies(bool measure_latencies) PURE;

  /**
   * gets the statistics.
   *
   * @return const std::vector<std::tuple<std::string, const Statistic&>> A vector of Statistics and
   * their respective names.
   */
  virtual const std::vector<std::tuple<std::string, const Statistic&>> statistics() const PURE;

  // TODO(oschaaf): Consider where this belongs. E.g. "Sequenceable" ?
  // In this interface we would then have "bool startRequest()" or some such, which the sequenceable
  // interface implementation should then redirect to.
  /**
   * @brief Tries to start a request. In open-loop mode this MUST always return true.
   *
   * @param caller_completion_callback The callback the client must call back upon completion of a
   * successfully started request.
   *
   * @return true if the request could be started.
   * @return false if the request could not be started, for example due to resource limits.
   */
  virtual bool tryStartOne(std::function<void()> caller_completion_callback) PURE;

protected:
  /**
   * @brief Determines if latency measurement is on.
   *
   * @return true if latency measurement is on.
   * @return false if latency measurement is off.
   */
  virtual bool measureLatencies() const PURE;
};

} // namespace Client
} // namespace Nighthawk