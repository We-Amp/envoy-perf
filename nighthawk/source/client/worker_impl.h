#pragma once

#include "envoy/api/api.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"

#include "common/common/logger.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/client/option_interpreter.h"
#include "nighthawk/client/options.h"
#include "nighthawk/client/worker.h"
#include "nighthawk/common/platform_util.h"
#include "nighthawk/common/sequencer.h"
#include "nighthawk/common/statistic.h"

namespace Nighthawk {
namespace Client {

class WorkerImpl : Worker {
public:
  WorkerImpl(Envoy::Api::Api& api, Envoy::ThreadLocal::Instance& tls,
             Envoy::Stats::StorePtr&& store);
  ~WorkerImpl() override;

  void start() override;
  void waitForCompletion() override;

protected:
  Envoy::Thread::ThreadFactory& thread_factory_;
  Envoy::Event::DispatcherPtr dispatcher_;
  Envoy::ThreadLocal::Instance& tls_;
  Envoy::Stats::StorePtr store_;
  std::unique_ptr<Envoy::Runtime::Loader> runtime_;
  std::unique_ptr<Envoy::Runtime::RandomGenerator> generator_;
  Envoy::TimeSource& time_source_;

private:
  Envoy::Thread::ThreadPtr thread_;
  bool started_;
  bool completed_;
};

class WorkerClientImpl : public WorkerImpl, Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  WorkerClientImpl(OptionInterpreter& option_interpreter, Envoy::Api::Api& api,
                   Envoy::ThreadLocal::Instance& tls, Envoy::Stats::StorePtr&& store,
                   const Options& options, int worker_number, uint64_t start_delay_usec);

  StatisticPtrMap statistics() const override;

private:
  void work() override;
  void simpleWarmup();
  void delayStart();
  void logResult();

  OptionInterpreter& option_interpreter_;
  const Options& options_;
  PlatformUtilPtr platform_util_;
  std::unique_ptr<BenchmarkClient> benchmark_client_;
  std::unique_ptr<Sequencer> sequencer_;
  const int worker_number_;
  const uint64_t start_delay_usec_;
};

typedef std::unique_ptr<WorkerClientImpl> WorkerClientImplPtr;

} // namespace Client
} // namespace Nighthawk