#pragma once

#include "common/api/api_impl.h"
#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/address.h"
#include "envoy/runtime/runtime.h"
//#include "envoy/stats/store.h"
#include "common/stats/isolated_store_impl.h"

#include "envoy/upstream/upstream.h"

#include "nighthawk/client/benchmark_client.h"
#include "nighthawk/common/sequencer.h"

#include "nighthawk/source/client/stream_decoder.h"
#include "nighthawk/source/common/ssl.h"
#include "nighthawk/source/common/statistic_impl.h"
#include "nighthawk/source/common/utility.h"

namespace Nighthawk {
namespace Client {

class BenchmarkHttpClient : public BenchmarkClient,
                            public StreamDecoderCompletionCallback,
                            public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  // TODO(oschaaf): Pass in a request generator instead of just the request headers.
  BenchmarkHttpClient(Envoy::Api::Api& api, Envoy::Stats::Store& store,
                      Envoy::Event::Dispatcher& dispatcher, Envoy::Event::TimeSystem& time_system,
                      const std::string& uri, Envoy::Http::HeaderMapImplPtr&& request_headers,
                      bool use_h2);
  ~BenchmarkHttpClient() override = default;

  uint64_t pool_overflow_failures() { return pool_overflow_failures_; }
  uint64_t stream_reset_count() { return stream_reset_count_; }
  uint64_t http_good_response_count() { return http_good_response_count_; }
  uint64_t http_bad_response_count() { return http_bad_response_count_; }

  void set_connection_limit(uint64_t connection_limit) { connection_limit_ = connection_limit; }
  void set_connection_timeout(std::chrono::seconds timeout) { timeout_ = timeout; }
  void set_max_pending_requests(uint64_t max_pending_requests) {
    max_pending_requests_ = max_pending_requests;
  }
  void set_allow_pending_for_test(bool allow_pending_for_test) {
    allow_pending_for_test_ = allow_pending_for_test;
  }

  const Envoy::Http::HeaderMapImpl& request_headers() const { return *request_headers_; }

  // BenchmarkClient
  void initialize(Envoy::Runtime::LoaderImpl& runtime) override;
  void terminate() override { resetPool(); };

  const std::vector<std::tuple<std::string, const Statistic&>> statistics() const override {
    std::vector<std::tuple<std::string, const Statistic&>> statistics;
    statistics.push_back(
        std::tuple<std::string, const Statistic&>{"Pool and connection setup", connect_statistic_});
    statistics.push_back(
        std::tuple<std::string, const Statistic&>{"Request to response", response_statistic_});
    return statistics;
  };
  bool measureLatencies() const override { return measure_latencies_; }
  void setMeasureLatencies(bool measure_latencies) override {
    measure_latencies_ = measure_latencies;
  }

  bool tryStartOne(std::function<void()> caller_completion_callback) override;

  // StreamDecoderCompletionCallback
  void onComplete(bool success, const Envoy::Http::HeaderMap& headers) override;
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason) override;

private:
  void syncResolveDns();
  void resetPool() { pool_.reset(); }

  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::Stats::Store& store_;
  Envoy::Event::TimeSystem& time_system_;
  const Envoy::Http::HeaderMapImplPtr request_headers_;
  Envoy::Upstream::ClusterInfoConstSharedPtr cluster_;
  HdrStatistic response_statistic_;
  HdrStatistic connect_statistic_;
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
  uint64_t http_good_response_count_;
  uint64_t http_bad_response_count_;
  uint64_t requests_completed_;
  uint64_t requests_initiated_;
  bool allow_pending_for_test_;
  bool measure_latencies_;
  Ssl::MinimalTransportSocketFactoryContext factory_context_;
};

} // namespace Client
} // namespace Nighthawk