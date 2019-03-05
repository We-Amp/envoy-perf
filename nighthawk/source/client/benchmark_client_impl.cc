#include "nighthawk/source/client/benchmark_client_impl.h"

#include "absl/strings/str_split.h"

#include "ares.h"

#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/compiler_requirements.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/http/utility.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/upstream/cluster_manager_impl.h"

#include "nighthawk/common/statistic.h"

#include "nighthawk/source/client/stream_decoder.h"

using namespace std::chrono_literals;

namespace Nighthawk {
namespace Client {

BenchmarkClientHttpImpl::BenchmarkClientHttpImpl(Envoy::Api::Api& api,
                                                 Envoy::Event::Dispatcher& dispatcher,
                                                 Envoy::Stats::StorePtr&& store,
                                                 StatisticPtr&& connect_statistic,
                                                 StatisticPtr&& response_statistic,
                                                 const std::string& uri, bool use_h2)
    : api_(api), dispatcher_(dispatcher), store_(std::move(store)),
      connect_statistic_(std::move(connect_statistic)),
      response_statistic_(std::move(response_statistic)), use_h2_(use_h2),
      uri_(std::make_unique<Uri>(Uri::Parse(uri))), dns_failure_(true), timeout_(5s),
      connection_limit_(1), max_pending_requests_(1), pool_overflow_failures_(0),
      stream_reset_count_(0), requests_completed_(0), requests_initiated_(0),
      measure_latencies_(false) {
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

void BenchmarkClientHttpImpl::syncResolveDns() {
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

void BenchmarkClientHttpImpl::initialize(Envoy::Runtime::Loader& runtime) {
  syncResolveDns();

  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::core::BindConfig bind_config;

  cluster_config.mutable_connect_timeout()->set_seconds(timeout_.count());
  auto thresholds = cluster_config.mutable_circuit_breakers()->add_thresholds();

  thresholds->mutable_max_retries()->set_value(0);
  thresholds->mutable_max_connections()->set_value(connection_limit_);
  thresholds->mutable_max_pending_requests()->set_value(max_pending_requests_);

  Envoy::Network::TransportSocketFactoryPtr socket_factory;

  if (uri_->scheme() == "https") {
    socket_factory = Envoy::Network::TransportSocketFactoryPtr{
        new Ssl::MClientSslSocketFactory(*store_, api_.timeSource(), use_h2_)};
  } else {
    socket_factory = std::make_unique<Envoy::Network::RawBufferSocketFactory>();
  };

  cluster_ = std::make_unique<Envoy::Upstream::ClusterInfoImpl>(
      cluster_config, bind_config, runtime, std::move(socket_factory),
      store_->createScope("client."), false /*added_via_api*/);

  auto host = std::shared_ptr<Envoy::Upstream::Host>{new Envoy::Upstream::HostImpl(
      cluster_, uri_->host_and_port(), target_address_,
      envoy::api::v2::core::Metadata::default_instance(), 1 /* weight */,
      envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0,
      envoy::api::v2::core::HealthStatus::HEALTHY)};

  Envoy::Network::ConnectionSocket::OptionsSharedPtr options =
      std::make_shared<Envoy::Network::ConnectionSocket::Options>();
  if (use_h2_) {
    pool_ = std::make_unique<Envoy::Http::Http2::ProdConnPoolImpl>(
        dispatcher_, host, Envoy::Upstream::ResourcePriority::Default, options);
  } else {
    pool_ = std::make_unique<Envoy::Http::Http1::ProdConnPoolImpl>(
        dispatcher_, host, Envoy::Upstream::ResourcePriority::Default, options);
  }
}

StatisticPtrMap BenchmarkClientHttpImpl::statistics() const {
  StatisticPtrMap statistics;
  statistics[connect_statistic_->id()] = connect_statistic_.get();
  statistics[response_statistic_->id()] = response_statistic_.get();
  return statistics;
};

bool BenchmarkClientHttpImpl::tryStartOne(std::function<void()> caller_completion_callback) {
  if (!cluster_->resourceManager(Envoy::Upstream::ResourcePriority::Default)
           .pendingRequests()
           .canCreate() ||
      // In closed loop mode we want to be able to control the pacing as exactly as possible.
      // In open-loop mode we probably want to skip this.
      // NOTE(oschaaf): We can't consistently rely on resourceManager()::requests() because that
      // isn't used for h/1 (it is used in tcp and h2 though).
      ((requests_initiated_ - requests_completed_) >= connection_limit_)) {
    return false;
  }

  auto stream_decoder = new StreamDecoder(
      api_.timeSource(), *this, std::move(caller_completion_callback), *connect_statistic_,
      *response_statistic_, request_headers_, measureLatencies());
  requests_initiated_++;
  pool_->newStream(*stream_decoder, *stream_decoder);
  return true;
}

std::string BenchmarkClientHttpImpl::countersToString(CounterFilter filter) const {
  auto counters = store_->counters();
  std::string s;

  for (auto stat : counters) {
    if (filter(stat->name(), stat->value())) {
      s += fmt::format("{}:{}\n", stat->name(), stat->value());
    }
  }
  return s;
}

void BenchmarkClientHttpImpl::onComplete(bool success, const Envoy::Http::HeaderMap& headers) {
  requests_completed_++;
  if (!success) {
    stream_reset_count_++;
  } else {
    ASSERT(headers.Status());
    const int64_t status = Envoy::Http::Utility::getResponseStatus(headers);
    if (status >= 400 && status <= 599) {
      // TODO(oschaaf): Figure out why this isn't incremented for us.
      store_->counter("client.upstream_cx_protocol_error").inc();
    }
  }
}

void BenchmarkClientHttpImpl::onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason) {
  switch (reason) {
  case Envoy::Http::ConnectionPool::PoolFailureReason::ConnectionFailure:
    break;
  case Envoy::Http::ConnectionPool::PoolFailureReason::Overflow:
    // We do not expect this to happen, at least not right now.
    ASSERT(false);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Client
} // namespace Nighthawk