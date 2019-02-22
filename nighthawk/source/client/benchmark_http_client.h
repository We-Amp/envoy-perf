#pragma once

#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/address.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/store.h"
#include "envoy/upstream/upstream.h"

#include "nighthawk/source/common/statistic_impl.h"
#include "nighthawk/source/common/stream_decoder.h"
#include "nighthawk/source/common/utility.h"

namespace Nighthawk {
namespace Client {

class BenchmarkHttpClient : public Nighthawk::Http::StreamDecoderCompletionCallback,
                            public Envoy::Logger::Loggable<Envoy::Logger::Id::main>,
                            public Envoy::Http::ConnectionPool::Callbacks {
public:
  // TODO(oschaaf): Pass in a request generator instead of just the request headers.
  BenchmarkHttpClient(Envoy::Event::Dispatcher& dispatcher, Envoy::Stats::Store& store,
                      Envoy::TimeSource& time_source, const std::string& uri,
                      Envoy::Http::HeaderMapImplPtr&& request_headers, bool use_h2);
  ~BenchmarkHttpClient() override = default;

  void initialize(Envoy::Runtime::LoaderImpl& runtime);
  bool tryStartOne(std::function<void()> caller_completion_callback);

  // ConnectionPool::Callbacks
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

  // StreamDecoderCompletionCallback
  void onComplete(bool success, const Envoy::Http::HeaderMap& headers) override;

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

  const Statistic& statistic() { return statistic_; }

private:
  void syncResolveDns();

  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::Stats::Store& store_;
  Envoy::TimeSource& time_source_;
  const Envoy::Http::HeaderMapImplPtr request_headers_;
  Envoy::Upstream::ClusterInfoConstSharedPtr cluster_;
  HdrStatistic statistic_;
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
};

} // namespace Client
} // namespace Nighthawk