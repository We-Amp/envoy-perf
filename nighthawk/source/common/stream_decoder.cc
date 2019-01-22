#include "common/stream_decoder.h"

#include "common/http/http1/codec_impl.h"
#include "common/http/utility.h"

namespace Nighthawk {
namespace Http {

std::vector<StreamDecoder*> pool;

void StreamDecoder::setCallbacks(std::function<void()> caller_completion_callback,
                                 StreamDecoderCompletionCallback* on_complete_cb) {
  caller_completion_callback_ = caller_completion_callback;
  on_complete_cb_ = on_complete_cb;
}

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
  ASSERT(complete_);
  on_complete_cb_->onComplete(success, *headers_);
  caller_completion_callback_();
}

void StreamDecoder::onResetStream(Envoy::Http::StreamResetReason) {
  // TODO(oschaaf): check if we need to do something here.
  // ADD_FAILURE();
  onComplete(false);
}

void StreamDecoder::reset() {
  complete_ = false;
  headers_.reset();
  caller_completion_callback_ = nullptr;
  on_complete_cb_ = nullptr;
}

} // namespace Http
} // namespace Nighthawk
