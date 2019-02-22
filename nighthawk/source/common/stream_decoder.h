#pragma once

#include <functional>

#include "common/http/codec_wrappers.h"
#include "envoy/common/time.h"

#include "nighthawk/common/statistic.h"

namespace Nighthawk {
namespace Http {

class StreamDecoderCompletionCallback {
public:
  virtual ~StreamDecoderCompletionCallback() = default;
  virtual void onComplete(bool success, const Envoy::Http::HeaderMap& headers) PURE;
};

/**
 * A self destructing response decoder that discards the response body.
 */
class StreamDecoder : public Envoy::Http::StreamDecoder, public Envoy::Http::StreamCallbacks {
public:
  StreamDecoder(Statistic& statistic, Envoy::TimeSource& time_source,
                std::function<void()> caller_completion_callback,
                StreamDecoderCompletionCallback& on_complete_cb)
      : caller_completion_callback_(std::move(caller_completion_callback)),
        on_complete_cb_(on_complete_cb), statistic_(statistic), time_source_(time_source),
        start_(time_source_.monotonicTime()) {}

  bool complete() { return complete_; }
  const Envoy::Http::HeaderMap& headers() { return *headers_; }

  // Http::StreamDecoder
  void decode100ContinueHeaders(Envoy::Http::HeaderMapPtr&&) override {}
  void decodeHeaders(Envoy::Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Envoy::Buffer::Instance&, bool end_stream) override;
  void decodeTrailers(Envoy::Http::HeaderMapPtr&& trailers) override;
  void decodeMetadata(Envoy::Http::MetadataMapPtr&&) override {}

  // Http::StreamCallbacks
  void onResetStream(Envoy::Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void onComplete(bool success);

  Envoy::Http::HeaderMapPtr headers_;
  bool complete_{};
  std::function<void()> caller_completion_callback_;
  StreamDecoderCompletionCallback& on_complete_cb_;
  Statistic& statistic_;
  Envoy::TimeSource& time_source_;
  Envoy::MonotonicTime start_;
};

} // namespace Http
} // namespace Nighthawk
