#pragma once

#include "common/common/logger.h"
#include "common/common/thread_impl.h"
#include "common/event/real_time_system.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "nighthawk/client/options.h"
#include "nighthawk/client/worker.h"

#include "nighthawk/source/client/benchmark_http_client.h"
#include "nighthawk/source/common/sequencer_impl.h"
#include "nighthawk/source/common/statistic_impl.h"

namespace Nighthawk {
namespace Client {

class WorkerImpl : Worker, Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  WorkerImpl(Envoy::ThreadLocal::Instance& tls, Envoy::Event::DispatcherPtr&& dispatcher,
             Envoy::Thread::ThreadFactory& thread_factory, const Options& options,
             const int worker_number);
  ~WorkerImpl() override;
  void start() override;
  void waitForCompletion() override;
  const Sequencer& sequencer() const override;

  // TODO(oschaaf): not in the interface. think about the interface we need here.
  const BenchmarkHttpClient& benchmark_http_client() const;

private:
  void work();

  Envoy::ThreadLocal::Instance& tls_;
  Envoy::Event::DispatcherPtr dispatcher_;
  Envoy::Stats::IsolatedStoreImpl store_;
  std::unique_ptr<Envoy::Runtime::LoaderImpl> runtime_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::Event::RealTimeSystem time_system_;
  std::unique_ptr<SequencerImpl> sequencer_;
  std::unique_ptr<BenchmarkHttpClient> benchmark_http_client_;
  Envoy::Thread::ThreadFactory& thread_factory_;
  Envoy::Thread::ThreadPtr thread_;

  const int worker_number_;
  const Options& options_;
  bool started_;
  bool completed_;
};

typedef std::unique_ptr<WorkerImpl> WorkerImplPtr;

} // namespace Client
} // namespace Nighthawk