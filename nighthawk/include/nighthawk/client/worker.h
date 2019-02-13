#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "nighthawk/source/common/statistic_impl.h"

namespace Nighthawk {
namespace Client {

/**
 * Interface for a threaded benchmark client worker.
 */
class Worker {
public:
  virtual ~Worker() {}

  /**
   * Start the worker thread.
   */
  virtual void start() PURE;

  /**
   * Stop the worker thread.
   */
  virtual void waitForCompletion() PURE;

  // TODO(oschaaf): Stop exposing the implementation of statistic here.
  /**
   * Get the latency statistics. Only to be called after waitForCompletion().
   * @return const HdrStatistic&
   */
  virtual const HdrStatistic& statistic() PURE;
};

typedef std::unique_ptr<Worker> WorkerPtr;

} // namespace Client
} // namespace Nighthawk
