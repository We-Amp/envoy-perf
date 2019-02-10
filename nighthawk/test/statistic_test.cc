#include "nighthawk/source/common/statistic_impl.h"

#include "nighthawk/common/statistic.h"

#include "gtest/gtest.h"

namespace Nighthawk {

using MyTypes = ::testing::Types<InMemoryStatistic, HdrStatistic, StreamingStatistic>;

template <typename T> class StatisticTest : public testing::Test {};

TYPED_TEST_SUITE(StatisticTest, MyTypes);

TYPED_TEST(StatisticTest, Simple) {
  TypeParam a;
  TypeParam b;

  std::vector<int> a_values{1, 2, 3};
  std::vector<int> b_values{1234, 6543456, 342335};

  for (int value : a_values) {
    a.addValue(value);
  }
  for (int value : b_values) {
    b.addValue(value);
  }

  EXPECT_EQ(3, a.count());
  EXPECT_EQ(2, a.mean());
  EXPECT_EQ(1, a.variance());
  EXPECT_EQ(1, a.stdev());

  EXPECT_EQ(3, b.count());

  // TODO(oschaaf):
  if (b.is_high_precision()) {
    EXPECT_EQ(2295675, b.mean());
    EXPECT_EQ(13561820041021, b.variance());
    EXPECT_DOUBLE_EQ(3682637.6472605884, b.stdev());
  } else {
    // HdrHistogram is up to 5 digits precise.
    // We configure it to 3 (similar to wrk2).
    // Note that we repeat this test with higher precision for
    // the streaming stats below.
    EXPECT_NEAR(2295675, b.mean(), 11);
    EXPECT_NEAR(13561820041021, b.variance(), 999999999);
    EXPECT_NEAR(3682637.6472605884, b.stdev(), 99);
  }

  auto c = a.combine(b);
  EXPECT_EQ(6, c.count());
  if (c.is_high_precision()) {
    EXPECT_DOUBLE_EQ(1147838.5, c.mean());
    EXPECT_EQ(7005762373287.5, c.variance());
    EXPECT_DOUBLE_EQ(2646840.0732359141, c.stdev());
  } else {
    EXPECT_NEAR(1147838.5, c.mean(), 6);
    EXPECT_NEAR(7005762373287.5, c.variance(), 999999999);
    EXPECT_NEAR(2646840.0732359141, c.stdev(), 99);
  }
}

} // namespace Nighthawk
