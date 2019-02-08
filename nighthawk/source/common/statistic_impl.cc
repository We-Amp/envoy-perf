#include "nighthawk/source/common/statistic_impl.h"

namespace Nighthawk {

void InMemoryStatistic::AddSample(int64_t sample_value) { samples_.push_back(sample_value); }

void HdrStatistic::AddSample(int64_t sample_value) { hdr_record_value(histogram_, sample_value); }

} // namespace Nighthawk