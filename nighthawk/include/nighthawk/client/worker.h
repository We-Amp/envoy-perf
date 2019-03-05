#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "nighthawk/common/statistic.h"

// TODO(oschaaf): Arguably Worker belongs in /common instead of here in /client.
// However, have also have WorkerClient(Impl), and I feel splitting this over two places is overkill
// right now.

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
   * Gets the statistics, keyed by id.
   *
   * @return StatisticPtrMap A map of Statistics keyed by id.
   */
  virtual StatisticPtrMap statistics() const PURE;

protected:
  /**
   * Perform the actual work on the associated thread.
   */
  virtual void work() PURE;
};

typedef std::unique_ptr<Worker> WorkerPtr;

} // namespace Client
} // namespace Nighthawk
