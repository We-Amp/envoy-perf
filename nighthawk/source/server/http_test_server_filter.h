#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "nighthawk/source/server/http_test_server_filter.pb.h"

namespace Nighthawk {
namespace Server {

namespace TestServer {

class HeaderNameValues {
public:
  const Envoy::Http::LowerCaseString TestServerConfig{"x-nighthawk-test-server-config"};
};

typedef Envoy::ConstSingleton<HeaderNameValues> HeaderNames;

} // namespace TestServer

// Basically this is left in as a placeholder for further configuration.
class HttpTestServerDecoderFilterConfig {
public:
  HttpTestServerDecoderFilterConfig(const nighthawk::server::TestServer& proto_config);
  const nighthawk::server::TestServer& server_config() const { return server_config_; }
  const nighthawk::server::TestServer& persisted_config() const { return persisted_config_; }
  void set_persisted_config(const nighthawk::server::TestServer& config) {
    persisted_config_ = config;
  }

private:
  nighthawk::server::TestServer persisted_config_;
  const nighthawk::server::TestServer server_config_;
};

typedef std::shared_ptr<HttpTestServerDecoderFilterConfig>
    HttpTestServerDecoderFilterConfigSharedPtr;

class HttpTestServerDecoderFilter : public Envoy::Http::StreamDecoderFilter {
public:
  HttpTestServerDecoderFilter(HttpTestServerDecoderFilterConfigSharedPtr);
  ~HttpTestServerDecoderFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::HeaderMap&, bool) override;
  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance&, bool) override;
  Envoy::Http::FilterTrailersStatus decodeTrailers(Envoy::Http::HeaderMap&) override;
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks&) override;

private:
  const HttpTestServerDecoderFilterConfigSharedPtr config_;
  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
};

} // namespace Server
} // namespace Nighthawk
