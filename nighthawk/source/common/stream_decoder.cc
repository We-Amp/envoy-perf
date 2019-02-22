#include "nighthawk/source/common/stream_decoder.h"

#include "common/http/http1/codec_impl.h"
#include "common/http/utility.h"

namespace Nighthawk {
namespace Http {

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
  if (success) {
    statistic_.addValue((time_source_.monotonicTime() - start_).count());
  }
  ASSERT(complete_);
  on_complete_cb_.onComplete(success, *headers_);
  caller_completion_callback_();
  delete this;
}

void StreamDecoder::onResetStream(Envoy::Http::StreamResetReason) { onComplete(false); }

} // namespace Http
} // namespace Nighthawk
