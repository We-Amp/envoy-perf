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
#include "common/runtime/runtime_impl.h"
#include "common/thread_local/thread_local_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "nighthawk/source/client/stream_decoder.h"
#include "nighthawk/source/common/ssl.h"
#include "nighthawk/source/common/statistic_impl.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

BenchmarkHttpClient::BenchmarkHttpClient(Envoy::Event::Dispatcher& dispatcher,
                                         Envoy::Event::TimeSystem& time_system,
                                         const std::string& uri,
                                         Envoy::Http::HeaderMapImplPtr&& request_headers,
                                         bool use_h2)
    : dispatcher_(dispatcher), time_system_(time_system),
      request_headers_(std::move(request_headers)), use_h2_(use_h2),
      uri_(std::make_unique<Uri>(Uri::Parse(uri))), dns_failure_(true), timeout_(5s),
      connection_limit_(1), max_pending_requests_(1), pool_overflow_failures_(0),
      stream_reset_count_(0), http_good_response_count_(0), http_bad_response_count_(0),
      requests_completed_(0), requests_initiated_(0), allow_pending_for_test_(false),
      measure_latencies_(false) {
  ASSERT(uri_->isValid());
  request_headers_->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
  request_headers_->insertPath().value(uri_->path());
  request_headers_->insertHost().value(uri_->host_and_port());
  request_headers_->insertScheme().value(uri_->scheme() == "https"
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

void BenchmarkHttpClient::initialize(Envoy::Runtime::LoaderImpl& runtime) {
  syncResolveDns();

  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::core::BindConfig bind_config;
  // envoy::config::bootstrap::v2::Runtime runtime_config;

  auto thresholds = cluster_config.mutable_circuit_breakers()->add_thresholds();

  cluster_config.mutable_connect_timeout()->set_seconds(timeout_.count());
  thresholds->mutable_max_retries()->set_value(0);
  thresholds->mutable_max_connections()->set_value(connection_limit_);
  thresholds->mutable_max_pending_requests()->set_value(max_pending_requests_);

  Envoy::Stats::ScopePtr scope = store_.createScope(fmt::format(
      "nighthawk.{}", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                             : cluster_config.alt_stat_name()));

  Envoy::Network::TransportSocketFactoryPtr socket_factory;
  if (uri_->scheme() == "https") {
    socket_factory = Envoy::Network::TransportSocketFactoryPtr{
        new Ssl::MClientSslSocketFactory(store_, time_system_, use_h2_)};
  } else {
    socket_factory = std::make_unique<Envoy::Network::RawBufferSocketFactory>();
  };

  cluster_ = std::make_unique<Envoy::Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, runtime, std::move(socket_factory), std::move(scope),
      false /*added_via_api*/);

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
    pool_ = std::make_unique<Envoy::Http::Http1::ConnPoolImplProd>(
        dispatcher_, host, Envoy::Upstream::ResourcePriority::Default, options);
  }
}

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

  auto stream_decoder = new StreamDecoder(this, connect_statistic_, response_statistic_,
                                          time_system_, std::move(caller_completion_callback),
                                          *this, this->measureLatencies(), this->request_headers());
  requests_initiated_++;
  pool_->newStream(*stream_decoder, *stream_decoder);

  return true;
}

void BenchmarkHttpClient::onComplete(bool success, const Envoy::Http::HeaderMap& headers) {
  requests_completed_++;
  if (!success) {
    stream_reset_count_++;
  } else {
    ASSERT(headers.Status());
    const int64_t status = Envoy::Http::Utility::getResponseStatus(headers);
    // TODO(oschaaf): we can very probably pull these from the stats.
    if (status >= 400 && status <= 599) {
      http_bad_response_count_++;
    } else {
      http_good_response_count_++;
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