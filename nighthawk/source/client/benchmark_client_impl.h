#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/address.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/store.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
#include "common/runtime/runtime_impl.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/common/sequencer.h"
#include "nighthawk/common/statistic.h"

#include "nighthawk/source/client/stream_decoder.h"
#include "nighthawk/source/common/ssl.h"
#include "nighthawk/source/common/utility.h"

namespace Nighthawk {
namespace Client {

class BenchmarkClientHttpImpl : public BenchmarkClient,
                                public StreamDecoderCompletionCallback,
                                public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  BenchmarkClientHttpImpl(Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher,
                          Envoy::Stats::StorePtr&& store, StatisticPtr&& connect_statistic,
                          StatisticPtr&& response_statistic, const std::string& uri, bool use_h2);
  ~BenchmarkClientHttpImpl() override = default;

  // TODO(oschaaf): can probably get rid of these.
  uint64_t stream_reset_count() { return stream_reset_count_; }

  void set_connection_limit(uint64_t connection_limit) { connection_limit_ = connection_limit; }
  void set_connection_timeout(std::chrono::seconds timeout) { timeout_ = timeout; }
  void set_max_pending_requests(uint64_t max_pending_requests) {
    max_pending_requests_ = max_pending_requests;
  }

  // BenchmarkClient
  void initialize(Envoy::Runtime::Loader& runtime) override;

  void terminate() override { resetPool(); };

  StatisticPtrVector statistics() const override;

  bool measureLatencies() const override { return measure_latencies_; }

  void setMeasureLatencies(bool measure_latencies) override {
    measure_latencies_ = measure_latencies;
  }

  bool tryStartOne(std::function<void()> caller_completion_callback) override;

  uint64_t getCounter(const std::string& counter) const override {
    return store_->counter(counter).value();
  }

  std::string countersToString(CounterFilter filter = [](std::string, uint64_t) {
    return true;
  }) const override;

  // StreamDecoderCompletionCallback
  void onComplete(bool success, const Envoy::Http::HeaderMap& headers) override;
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason) override;

private:
  void syncResolveDns();
  void resetPool() { pool_.reset(); }

  Envoy::Api::Api& api_;
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::Stats::StorePtr store_;
  Envoy::Http::HeaderMapImpl request_headers_;
  Envoy::Upstream::ClusterInfoConstSharedPtr cluster_;
  StatisticPtr connect_statistic_;
  StatisticPtr response_statistic_;
  const bool use_h2_;
  const std::unique_ptr<Uri> uri_;
  // dns_failure_ will be set by syncResolveDns. If false, the benchmark client should be disposed,
  // as it isn't further usable.
  bool dns_failure_;
  Envoy::Network::Address::InstanceConstSharedPtr target_address_;
  std::chrono::seconds timeout_;
  uint64_t connection_limit_;
  uint64_t max_pending_requests_;
  uint64_t pool_overflow_failures_;
  Envoy::Http::ConnectionPool::InstancePtr pool_;
  Envoy::Event::TimerPtr timer_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  uint64_t stream_reset_count_;
  uint64_t requests_completed_;
  uint64_t requests_initiated_;
  bool measure_latencies_;
  Ssl::MinimalTransportSocketFactoryContext transport_socket_factory_context_;
}; // namespace Client

} // namespace Client
} // namespace Nighthawk