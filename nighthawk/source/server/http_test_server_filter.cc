#include <string>

#include "http_test_server_filter.h"

#include "absl/strings/numbers.h"

#include "common/protobuf/utility.h"
#include "envoy/server/filter_config.h"

namespace Nighthawk {
namespace Server {

HttpTestServerDecoderFilterConfig::HttpTestServerDecoderFilterConfig(
    const nighthawk::server::TestServer& proto_config)
    : server_config_(proto_config) {}

HttpTestServerDecoderFilter::HttpTestServerDecoderFilter(
    HttpTestServerDecoderFilterConfigSharedPtr config)
    : config_(config) {}

HttpTestServerDecoderFilter::~HttpTestServerDecoderFilter() {}

void HttpTestServerDecoderFilter::onDestroy() {}

Envoy::Http::FilterHeadersStatus
HttpTestServerDecoderFilter::decodeHeaders(Envoy::Http::HeaderMap& headers, bool) {
  const auto request_config_header = headers.get(TestServer::HeaderNames::get().TestServerConfig);
  nighthawk::server::TestServer config = config_->server_config();
  bool error = false;

  if (request_config_header != nullptr) {
    try {
      nighthawk::server::TestServer json_config;
      Envoy::MessageUtil::loadFromJson(request_config_header->value().c_str(), json_config);
      config.MergeFrom(json_config);
      // TODO(oschaaf): validate the result & enable relevant tests
      if (config.persist().value()) {
        config_->set_persisted_config(config);
        // TODO(oschaaf): this persisted config needs to be distributed
      }
    } catch (Envoy::EnvoyException) {
      error = true;
    }
  } else {
    if (config_->persisted_config().persist().value()) {
      config = config_->persisted_config();
    }
  }

  if (error) {
    decoder_callbacks_->sendLocalReply(static_cast<Envoy::Http::Code>(500),
                                       "test-server didn't understand the request", nullptr,
                                       absl::nullopt);
  } else {
    decoder_callbacks_->sendLocalReply(
        static_cast<Envoy::Http::Code>(200), std::string(config.response_size(), 'a'),
        [&config](Envoy::Http::HeaderMap& direct_response_headers) {
          for (auto header_value_option : config.response_headers()) {
            auto header = header_value_option.header();
            auto lower_case_key = Envoy::Http::LowerCaseString(header.key());
            if (!header_value_option.append().value()) {
              direct_response_headers.remove(lower_case_key);
            }
            direct_response_headers.addCopy(lower_case_key, header.value());
          }
        },
        absl::nullopt);
  }
  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::FilterDataStatus HttpTestServerDecoderFilter::decodeData(Envoy::Buffer::Instance&,
                                                                      bool) {
  return Envoy::Http::FilterDataStatus::Continue;
}

Envoy::Http::FilterTrailersStatus
HttpTestServerDecoderFilter::decodeTrailers(Envoy::Http::HeaderMap&) {
  return Envoy::Http::FilterTrailersStatus::Continue;
}

void HttpTestServerDecoderFilter::setDecoderFilterCallbacks(
    Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

} // namespace Server
} // namespace Nighthawk
