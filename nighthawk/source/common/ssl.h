
#pragma once

// TODO(oschaaf): discuss how to improve.
// This class copies some ssl-related code from Envoy, to avoid a cascade of dependencies that would
// slip in because of some of the constructors involved to do it otherwise.
// It's also not cool that this doesn't validate certificates.
// Notable is:
//   TransportSocketFactoryContextImpl(Server::Admin& admin, Ssl::ContextManager& context_manager,
//                                    Stats::Scope& stats_scope, Upstream::ClusterManager& cm,
//                                    const LocalInfo::LocalInfo& local_info,
//                                    Event::Dispatcher& dispatcher,
//                                    Envoy::Runtime::RandomGenerator& random, Stats::Store& stats,
//                                    Singleton::Manager& singleton_manager,
//                                    ThreadLocal::SlotAllocator& tls, Api::Api& api)
//

#include "server/transport_socket_config_impl.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "envoy/network/transport_socket.h"

#include "openssl/ssl.h" // TLS1_2_VERSION etc

namespace Nighthawk {
namespace Ssl {

const std::string DEFAULT_CIPHER_SUITES =
#ifndef BORINGSSL_FIPS
    "[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:"
    "[ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]:"
#else // BoringSSL FIPS
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
#endif
    "ECDHE-ECDSA-AES128-SHA:"
    "ECDHE-RSA-AES128-SHA:"
    "AES128-GCM-SHA256:"
    "AES128-SHA:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES256-SHA:"
    "ECDHE-RSA-AES256-SHA:"
    "AES256-GCM-SHA384:"
    "AES256-SHA";

const std::string DEFAULT_ECDH_CURVES =
#ifndef BORINGSSL_FIPS
    "X25519:"
#endif
    "P-256";

// TODO(oschaaf): make a concrete implementation out of this one.
class MClientContextConfigImpl : public Envoy::Ssl::ClientContextConfig {
public:
  MClientContextConfigImpl(bool h2) : alpn_(h2 ? "h2" : "http/1.1") {}
  ~MClientContextConfigImpl() override = default;

  const std::string& alpnProtocols() const override { return alpn_; };

  const std::string& cipherSuites() const override { return DEFAULT_CIPHER_SUITES; };

  const std::string& ecdhCurves() const override { return DEFAULT_ECDH_CURVES; };

  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  tlsCertificates() const override {
    std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> configs;
    for (const auto& config : tls_certificate_configs_) {
      configs.emplace_back(config);
    }
    return configs;
  };

  const Envoy::Ssl::CertificateValidationContextConfig*
  certificateValidationContext() const override {
    return validation_context_config_.get();
  };

  unsigned minProtocolVersion() const override { return TLS1_VERSION; };

  unsigned maxProtocolVersion() const override { return TLS1_2_VERSION; };

  bool isReady() const override { return true; };

  void setSecretUpdateCallback(std::function<void()> callback) override { callback_ = callback; };

  // Ssl::ClientContextConfig interface
  const std::string& serverNameIndication() const override { return foo_; };

  bool allowRenegotiation() const override { return true; };

  size_t maxSessionKeys() const override { return 0; };

  const std::string& signingAlgorithmsForTest() const override { return foo_; };

private:
  std::string foo_;
  std::string alpn_;
  std::function<void()> callback_;
  std::vector<Envoy::Ssl::TlsCertificateConfigImpl> tls_certificate_configs_;
  Envoy::Ssl::CertificateValidationContextConfigPtr validation_context_config_;
};

class MClientSslSocketFactory : public Envoy::Network::TransportSocketFactory,
                                Envoy::Logger::Loggable<Envoy::Logger::Id::config> {
public:
  MClientSslSocketFactory(Envoy::Stats::Store& store, Envoy::TimeSource& time_source, bool h2)
      : config_(h2), scope_(store.createScope("nighthawk.ssl-client.")),
        ssl_ctx_(std::make_shared<Envoy::Extensions::TransportSockets::Tls::ClientContextImpl>(
            *scope_, config_, time_source)) {}

  Envoy::Network::TransportSocketPtr createTransportSocket(
      Envoy::Network::TransportSocketOptionsSharedPtr transport_socket_options) const override {
    return std::make_unique<Envoy::Extensions::TransportSockets::Tls::SslSocket>(
        Envoy::Ssl::ClientContextSharedPtr{ssl_ctx_},
        Envoy::Extensions::TransportSockets::Tls::InitialState::Client, transport_socket_options);
  }

  bool implementsSecureTransport() const override { return true; };

private:
  MClientContextConfigImpl config_;
  Envoy::Stats::ScopePtr scope_;
  Envoy::Ssl::ClientContextSharedPtr ssl_ctx_;
};

} // namespace Ssl
} // namespace Nighthawk