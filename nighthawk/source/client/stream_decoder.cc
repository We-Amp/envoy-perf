#include "nighthawk/source/client/stream_decoder.h"

#include "common/http/http1/codec_impl.h"
#include "common/http/utility.h"

#include "nighthawk/source/client/benchmark_http_client.h"

namespace Nighthawk {
namespace Client {

void StreamDecoder::decodeHeaders(Envoy::Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  headers_ = std::move(headers);
  if (complete_) {
    onComplete(true);
  }
}

void StreamDecoder::decodeData(Envoy::Buffer::Instance&, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  if (complete_) {
    onComplete(true);
  }
}

void StreamDecoder::decodeTrailers(Envoy::Http::HeaderMapPtr&&) { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

void StreamDecoder::onComplete(bool success) {
  if (success && benchmark_client_->measureLatencies()) {
    latency_statistic_.addValue((time_source_.monotonicTime() - request_start_).count());
  }
  ASSERT(complete_);
  on_complete_cb_.onComplete(success, *headers_);
  caller_completion_callback_();
  delete this;
}

void StreamDecoder::onResetStream(Envoy::Http::StreamResetReason) { onComplete(false); }

void StreamDecoder::onPoolFailure(Envoy::Http::ConnectionPool::PoolFailureReason reason,
                                  Envoy::Upstream::HostDescriptionConstSharedPtr host) {
  benchmark_client_->onPoolFailure(reason, host);
}

void StreamDecoder::onPoolReady(Envoy::Http::StreamEncoder& encoder,
                                Envoy::Upstream::HostDescriptionConstSharedPtr) {
  encoder.encodeHeaders(benchmark_client_->request_headers(), true);
  request_start_ = time_source_.monotonicTime();
  if (benchmark_client_->measureLatencies()) {
    connect_statistic_.addValue((request_start_ - connect_start_).count());
  }
}

} // namespace Client
} // namespace Nighthawk
