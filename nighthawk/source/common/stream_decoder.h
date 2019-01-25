#pragma once

#include <functional>
#include <queue>

#include "common/common/assert.h"
#include "common/http/codec_wrappers.h"

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
  StreamDecoder() : on_complete_cb_() {}

  void setCallbacks(std::function<void()> caller_completion_callback,
                    StreamDecoderCompletionCallback* on_complete_cb);

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

  void reset();

private:
  void onComplete(bool success);

  Envoy::Http::HeaderMapPtr headers_;
  bool complete_{};
  std::function<void()> caller_completion_callback_;
  StreamDecoderCompletionCallback* on_complete_cb_;
};

class StreamDecoderPool {
public:
  StreamDecoderPool(uint64_t size) : size_(size) {
    for (uint64_t i = 0; i < size; i++) {
      pool_.emplace(StreamDecoder());
    }
  }

  ~StreamDecoderPool() { ASSERT(pool_.size() == size_); };

  StreamDecoder* pop() {
    if (pool_.size() == 0) {
      // TODO(oschaaf): should we warn here?
      pool_.emplace(StreamDecoder());
      size_++;
    }

    auto& r = pool_.front();
    pool_.pop();
    return &r;
  }
  void push(StreamDecoder&& decoder) { pool_.emplace(std::move(decoder)); }

  uint64_t size_;
  std::queue<StreamDecoder> pool_;
};

} // namespace Http
} // namespace Nighthawk
