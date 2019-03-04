#pragma once

#include <functional>

#include "common/http/codec_wrappers.h"
#include "envoy/common/time.h"
#include "envoy/http/conn_pool.h"

#include "nighthawk/common/statistic.h"

namespace Nighthawk {
namespace Client {

class BenchmarkClientHttpImpl;

class StreamDecoderCompletionCallback {
public:
  virtual ~StreamDecoderCompletionCallback() = default;
  virtual void onComplete(bool success, const Envoy::Http::HeaderMap& headers) PURE;
  virtual void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason) PURE;
};

/**
 * A self destructing response decoder that discards the response body.
 */
// TODO(oschaaf): fix arg order etc.
// TODO(oschaaf): create a StreamDecoderPool?
class StreamDecoder : public Envoy::Http::StreamDecoder,
                      public Envoy::Http::StreamCallbacks,
                      public Envoy::Http::ConnectionPool::Callbacks {
public:
  StreamDecoder(StreamDecoderCompletionCallback* benchmark_client, Statistic& connect_statistic,
                Statistic& latency_statistic, Envoy::TimeSource& time_source,
                std::function<void()> caller_completion_callback,
                StreamDecoderCompletionCallback& on_complete_cb, bool measure_latencies,
                const Envoy::Http::HeaderMap& request_headers)
      : caller_completion_callback_(std::move(caller_completion_callback)),
        on_complete_cb_(on_complete_cb), connect_statistic_(connect_statistic),
        latency_statistic_(latency_statistic), time_source_(time_source),
        connect_start_(time_source_.monotonicTime()), benchmark_client_(benchmark_client),
        measure_latencies_(measure_latencies), request_headers_(request_headers) {}

  bool complete() { return complete_; }
  const Envoy::Http::HeaderMap& response_headers() { return *response_headers_; }

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

  // ConnectionPool::Callbacks
  void onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::StreamEncoder& encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

private:
  void onComplete(bool success);

  Envoy::Http::HeaderMapPtr response_headers_;

  bool complete_{};
  std::function<void()> caller_completion_callback_;
  StreamDecoderCompletionCallback& on_complete_cb_;
  Statistic& connect_statistic_;
  Statistic& latency_statistic_;
  Envoy::TimeSource& time_source_;
  Envoy::MonotonicTime connect_start_;
  Envoy::MonotonicTime request_start_;
  StreamDecoderCompletionCallback* benchmark_client_;
  bool measure_latencies_;
  const Envoy::Http::HeaderMap& request_headers_;
};

} // namespace Client
} // namespace Nighthawk
