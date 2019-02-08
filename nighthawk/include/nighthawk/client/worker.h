#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Nighthawk {
namespace Client {

/**
 * @brief Interface for a threaded benchmark client worker.
 *
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
  virtual void stop() PURE;
};

typedef std::unique_ptr<Worker> WorkerPtr;

/**
 * Factory for creating workers.
 */
class WorkerFactory {
public:
  virtual ~WorkerFactory() {}

  /**
   * @return WorkerPtr a new worker.
   */
  virtual WorkerPtr createWorker() PURE;
};

} // namespace Client
} // namespace Nighthawk
