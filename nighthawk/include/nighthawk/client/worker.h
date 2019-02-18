#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "nighthawk/common/statistic.h"

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

  /**
   * Get the latency statistics. Only to be called after waitForCompletion().
   * @return const Statistic&
   */
  virtual const Statistic& statistic() PURE;
};

typedef std::unique_ptr<Worker> WorkerPtr;

} // namespace Client
} // namespace Nighthawk
