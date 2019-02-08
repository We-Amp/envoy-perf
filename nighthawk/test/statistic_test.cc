#include "nighthawk/source/common/statistic_impl.h"

#include "gtest/gtest.h"

namespace Nighthawk {

class StatisticTest : public testing::Test {};

TEST_F(StatisticTest, InMemoryStatisticTest) {
  InMemoryStatistic stat;
  stat.AddSample(1);
}

} // namespace Nighthawk
