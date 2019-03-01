#pragma once

#include "envoy/stats/store.h"

#include "common/common/logger.h"
#include "common/common/thread_impl.h"
#include "common/event/real_time_system.h"
#include "common/runtime/runtime_impl.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/client/options.h"
#include "nighthawk/client/worker.h"
#include "nighthawk/common/sequencer.h"

namespace Nighthawk {
namespace Client {

class WorkerImpl : Worker {
public:
  WorkerImpl(Envoy::Thread::ThreadFactory& thread_factory, Envoy::ThreadLocal::Instance& tls,
             Envoy::Event::DispatcherPtr&& dispatcher, Envoy::Stats::StorePtr&& store);
  ~WorkerImpl() override;

  void start() override;
  void waitForCompletion() override;

protected:
  Envoy::Thread::ThreadFactory& thread_factory_;
  Envoy::Event::DispatcherPtr dispatcher_;
  Envoy::ThreadLocal::Instance& tls_;
  std::unique_ptr<Envoy::Stats::Store> store_;
  std::unique_ptr<Envoy::Runtime::LoaderImpl> runtime_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::Event::RealTimeSystem time_system_;

private:
  Envoy::Thread::ThreadPtr thread_;
  bool started_;
  bool completed_;
};

typedef std::unique_ptr<WorkerClientImpl> WorkerClientImplPtr;

class WorkerClientImpl : public WorkerImpl, Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  WorkerClientImpl(Envoy::Thread::ThreadFactory& thread_factory, Envoy::ThreadLocal::Instance& tls,
                   Envoy::Event::DispatcherPtr&& dispatcher, Envoy::Stats::StorePtr&& store,
                   const Options& options, int worker_number, uint64_t start_delay_usec,
                   std::unique_ptr<BenchmarkClient>&& benchmark_client);

  // TODO(oschaaf): get rid of these.
  const Sequencer& sequencer() const override;
  const BenchmarkClient& benchmark_client() const;

private:
  void work() override;

  std::unique_ptr<Sequencer> sequencer_;
  const int worker_number_;
  const uint64_t start_delay_usec_;
  const Options& options_;
  std::unique_ptr<BenchmarkClient> benchmark_client_;
};

} // namespace Client
} // namespace Nighthawk