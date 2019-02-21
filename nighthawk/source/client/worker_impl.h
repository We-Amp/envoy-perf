#pragma once

#include "common/common/logger.h"
#include "common/common/thread_impl.h"
#include "common/event/real_time_system.h"
#include "common/runtime/runtime_impl.h"

#include "nighthawk/client/options.h"
#include "nighthawk/client/worker.h"

#include "nighthawk/source/common/sequencer_impl.h"
#include "nighthawk/source/common/statistic_impl.h"

namespace Nighthawk {
namespace Client {

class WorkerImpl : Worker, Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  WorkerImpl(Envoy::Thread::ThreadFactoryImplPosix& thread_factory, const Options& options,
             const int worker_number);

  void start() override;
  void waitForCompletion() override;
  const Statistic& statistic() override;

private:
  void work();

  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::Event::RealTimeSystem time_system_;
  std::unique_ptr<SequencerImpl> sequencer_;
  Envoy::Thread::ThreadFactoryImplPosix& thread_factory_;
  Envoy::Thread::ThreadPtr thread_;
  const int worker_number_;
  const Options& options_;
  bool started_;
  bool completed_;
};

typedef std::unique_ptr<WorkerImpl> WorkerImplPtr;

} // namespace Client
} // namespace Nighthawk