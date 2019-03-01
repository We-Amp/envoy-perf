#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "nighthawk/common/sequencer.h"

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
   * Get the associated Sequencer.
   * @return const Sequencer&
   */
  virtual const Sequencer& sequencer() const PURE;

protected:
  /**
   * Perform the actual work on the thread.
   */
  virtual void work() PURE;
};

typedef std::unique_ptr<Worker> WorkerPtr;

} // namespace Client
} // namespace Nighthawk
