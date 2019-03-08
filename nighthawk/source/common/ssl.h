
#pragma once

// TODO(oschaaf): certificate validation, set up alpn for h/2.

#include "server/transport_socket_config_impl.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "common/secret/secret_manager_impl.h"

#include "envoy/network/transport_socket.h"

#include "openssl/ssl.h" // TLS1_2_VERSION etc

namespace Nighthawk {
namespace Ssl {

class MinimalTransportSocketFactoryContext
    : public Envoy::Server::Configuration::TransportSocketFactoryContext {
public:
  MinimalTransportSocketFactoryContext(
      /*Envoy::TimeSource& time_source,*/ Envoy::Stats::ScopePtr&& stats_scope,
      Envoy::Event::Dispatcher& dispatcher, Envoy::Runtime::RandomGenerator& random,
      Envoy::Stats::Store& stats, Envoy::Api::Api& api,
      Envoy::Extensions::TransportSockets::Tls::ContextManagerImpl& ssl_context_manager)
      : admin_(nullptr), /*time_source_(time_source),*/ ssl_context_manager_(ssl_context_manager),
        stats_scope_(std::move(stats_scope)), cluster_manager_(nullptr), local_info_(nullptr),
        dispatcher_(dispatcher), random_(random), stats_(stats), singleton_manager_(nullptr),
        thread_local_(nullptr), api_(api) {}

  Envoy::Server::Admin& admin() override {
    ASSERT(false);
    return *admin_;
  }

  Envoy::Ssl::ContextManager& sslContextManager() override { return ssl_context_manager_; }

  Envoy::Stats::Scope& statsScope() const override { return *stats_scope_; }

  Envoy::Secret::SecretManager& secretManager() override { return secret_manager_; }

  Envoy::Upstream::ClusterManager& clusterManager() override {
    ASSERT(false);
    return *cluster_manager_;
  }

  const Envoy::LocalInfo::LocalInfo& localInfo() override {
    ASSERT(false);
    return *local_info_;
  }

  Envoy::Event::Dispatcher& dispatcher() override { return dispatcher_; }

  Envoy::Runtime::RandomGenerator& random() override { return random_; }

  Envoy::Stats::Store& stats() override { return stats_; }

  void setInitManager(Envoy::Init::Manager&) override { ASSERT(false); }

  Envoy::Init::Manager* initManager() override {
    ASSERT(false);
    return init_manager_;
  }

  Envoy::Singleton::Manager& singletonManager() override {
    ASSERT(false);
    return *singleton_manager_;
  }

  Envoy::ThreadLocal::SlotAllocator& threadLocal() override {
    ASSERT(false);
    return *thread_local_;
  }

  Envoy::Api::Api& api() override { return api_; }

private:
  Envoy::Server::Admin* admin_;
  // Envoy::TimeSource& time_source_;
  Envoy::Extensions::TransportSockets::Tls::ContextManagerImpl& ssl_context_manager_;
  Envoy::Stats::ScopePtr stats_scope_;
  Envoy::Secret::SecretManagerImpl secret_manager_;
  Envoy::Upstream::ClusterManager* cluster_manager_;
  const Envoy::LocalInfo::LocalInfo* local_info_;
  Envoy::Event::Dispatcher& dispatcher_;
  Envoy::Runtime::RandomGenerator& random_;
  Envoy::Stats::Store& stats_;
  Envoy::Init::Manager* init_manager_;
  Envoy::Singleton::Manager* singleton_manager_;
  Envoy::ThreadLocal::SlotAllocator* thread_local_;
  Envoy::Api::Api& api_;
};

} // namespace Ssl
} // namespace Nighthawk