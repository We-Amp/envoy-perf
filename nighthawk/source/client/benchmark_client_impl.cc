#include "nighthawk/source/client/benchmark_client_impl.h"

#include "common/http/utility.h"
#include "common/network/utility.h"

#include "ares.h"

#include "absl/strings/str_split.h"

#include "common/common/compiler_requirements.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/thread_local/thread_local_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "nighthawk/common/statistic.h"

#include "nighthawk/source/client/stream_decoder.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

BenchmarkHttpClient::BenchmarkHttpClient(Envoy::Api::Api& api, Envoy::Event::Dispatcher& dispatcher,
                                         StatisticPtr&& connect_statistic,
                                         StatisticPtr&& response_statistic, const std::string& uri,
                                         bool use_h2)
    : api_(api), dispatcher_(dispatcher),
      store_(std::make_unique<Envoy::Stats::IsolatedStoreImpl>()),
      connect_statistic_(std::move(connect_statistic)),
      response_statistic_(std::move(response_statistic)), use_h2_(use_h2),
      uri_(std::make_unique<Uri>(Uri::Parse(uri))), dns_failure_(true), timeout_(5s),
      connection_limit_(1), max_pending_requests_(1), pool_overflow_failures_(0),
      stream_reset_count_(0), requests_completed_(0), requests_initiated_(0),
      allow_pending_for_test_(false), measure_latencies_(false),
      transport_socket_factory_context_(api.timeSource(), store_->createScope("transport."),
                                        dispatcher_, generator_, *store_, api) {
  ASSERT(uri_->isValid());

  connect_statistic_->setId("benchmark_http_client.queue_to_connect");
  response_statistic_->setId("benchmark_http_client.request_to_response");

  request_headers_.insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
  request_headers_.insertPath().value(uri_->path());
  request_headers_.insertHost().value(uri_->host_and_port());
  request_headers_.insertScheme().value(uri_->scheme() == "https"
                                            ? Envoy::Http::Headers::get().SchemeValues.Https
                                            : Envoy::Http::Headers::get().SchemeValues.Http);
}

void BenchmarkHttpClient::syncResolveDns() {
  auto dns_resolver = dispatcher_.createDnsResolver({});
  Envoy::Network::ActiveDnsQuery* active_dns_query_ = dns_resolver->resolve(
      uri_->host_without_port(), Envoy::Network::DnsLookupFamily::V4Only,
      [this, &active_dns_query_](
          const std::list<Envoy::Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
        active_dns_query_ = nullptr;
        ENVOY_LOG(debug, "DNS resolution complete for {} ({} entries).", uri_->host_without_port(),
                  address_list.size());
        if (!address_list.empty()) {
          dns_failure_ = false;
          target_address_ =
              Envoy::Network::Utility::getAddressWithPort(*address_list.front(), uri_->port());
        } else {
          ENVOY_LOG(critical, "Could not resolve host [{}]", uri_->host_without_port());
        }
        dispatcher_.exit();
      });
  // Wait for DNS resolution to complete before proceeding.
  dispatcher_.run(Envoy::Event::Dispatcher::RunType::Block);
}

void BenchmarkHttpClient::initialize(Envoy::Runtime::Loader& runtime) {
  syncResolveDns();

  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::core::BindConfig bind_config;
  // envoy::config::bootstrap::v2::Runtime runtime_config;

  auto thresholds = cluster_config.mutable_circuit_breakers()->add_thresholds();

  cluster_config.mutable_connect_timeout()->set_seconds(timeout_.count());
  thresholds->mutable_max_retries()->set_value(0);
  thresholds->mutable_max_connections()->set_value(connection_limit_);
  thresholds->mutable_max_pending_requests()->set_value(max_pending_requests_);

  Envoy::Network::TransportSocketFactoryPtr socket_factory;

  if (uri_->scheme() == "https") {
    if (use_h2_) {
      auto common_tls_context = cluster_config.mutable_tls_context()->mutable_common_tls_context();
      common_tls_context->add_alpn_protocols("h2");
    }
    socket_factory = Envoy::Upstream::createTransportSocketFactory(
        cluster_config, transport_socket_factory_context_);
  } else {
    socket_factory = std::make_unique<Envoy::Network::RawBufferSocketFactory>();
  };

  cluster_ = std::make_unique<Envoy::Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, runtime, std::move(socket_factory),
      store_->createScope("client."), false /*added_via_api*/);

  Envoy::Network::ConnectionSocket::OptionsSharedPtr options =
      std::make_shared<Envoy::Network::ConnectionSocket::Options>();

  auto host = std::shared_ptr<Envoy::Upstream::Host>{new Envoy::Upstream::HostImpl(
      cluster_, uri_->host_and_port(), target_address_,
      envoy::api::v2::core::Metadata::default_instance(), 1 /* weight */,
      envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0,
      envoy::api::v2::core::HealthStatus::HEALTHY)};

  if (use_h2_) {
    pool_ = std::make_unique<Envoy::Http::Http2::ProdConnPoolImpl>(
        dispatcher_, host, Envoy::Upstream::ResourcePriority::Default, options);
  } else {
    pool_ = std::make_unique<Envoy::Http::Http1::ProdConnPoolImpl>(
        dispatcher_, host, Envoy::Upstream::ResourcePriority::Default, options);
  }
}

StatisticPtrVector BenchmarkHttpClient::statistics() const {
  StatisticPtrVector statistics;
  statistics.push_back(connect_statistic_.get());
  statistics.push_back(response_statistic_.get());
  return statistics;
};

bool BenchmarkHttpClient::tryStartOne(std::function<void()> caller_completion_callback) {
  if (!cluster_->resourceManager(Envoy::Upstream::ResourcePriority::Default)
           .pendingRequests()
           .canCreate()
      // In closed loop we want to be able to control the pacing as
      // exactly as possible.
      // TODO(oschaaf): We can't rely on resourceManager()::requests() because that
      // isn't used for h/1 (it is used in tcp and h2 though).
      // Note: this improves accuracy, but some tests rely on pending requests functional
      // to queue up requests.
      || (!allow_pending_for_test_ &&
          (requests_initiated_ - requests_completed_) >= connection_limit_)) {
    return false;
  }

  auto stream_decoder = new StreamDecoder(this, *connect_statistic_, *response_statistic_,
                                          api_.timeSource(), std::move(caller_completion_callback),
                                          *this, measureLatencies(), request_headers_);
  requests_initiated_++;
  pool_->newStream(*stream_decoder, *stream_decoder);

  return true;
}

std::string BenchmarkHttpClient::countersToString(CounterFilter filter) const {
  auto counters = store_->counters();
  std::string s;

  for (auto stat : counters) {
    if (filter(stat->name(), stat->value())) {
      s += fmt::format("{}:{}\n", stat->name(), stat->value());
    }
  }
  return s;
}

void BenchmarkHttpClient::onComplete(bool success, const Envoy::Http::HeaderMap& headers) {
  requests_completed_++;
  if (!success) {
    stream_reset_count_++;
  } else {
    ASSERT(headers.Status());
    const int64_t status = Envoy::Http::Utility::getResponseStatus(headers);
    if (status >= 400 && status <= 599) {
      // TODO(oschaaf): Figure out why this isn't incremented for us.
      // TODO(oschaaf): don't get this stat each time, cache it?
      store_->counter("client.upstream_cx_protocol_error").inc();
    }
  }
}

void BenchmarkHttpClient::onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason) {
  switch (reason) {
  case Envoy::Http::ConnectionPool::PoolFailureReason::ConnectionFailure:
    break;
  case Envoy::Http::ConnectionPool::PoolFailureReason::Overflow:
    pool_overflow_failures_++;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Client
} // namespace Nighthawk