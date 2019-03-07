#include "gtest/gtest.h"

#include "common/api/api_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/thread_local/mocks.h"

#include "nighthawk/test/mocks.h"

#include "nighthawk/source/client/client_worker_impl.h"

namespace Nighthawk {
namespace Client {

class ClientWorkerTest : public testing::Test {
public:
  ClientWorkerTest() : api_(Envoy::Thread::ThreadFactorySingleton::get(), store_, time_system_) {}
  Envoy::Api::Impl api_;
  MockOptions options_;
  MockOptionInterpreter option_interpreter_;
  Envoy::Stats::IsolatedStoreImpl store_;
  ::testing::NiceMock<Envoy::ThreadLocal::MockInstance> tls_;
  Envoy::Event::RealTimeSystem time_system_;
  // MockBenchmarkClient& benchmark_client_;
};

TEST_F(ClientWorkerTest, BasicTest) {
  EXPECT_CALL(option_interpreter_, createBenchmarkClient(_, _))
      .Times(1)
      .WillOnce(testing::Return(
          testing::ByMove(std::make_unique<testing::NiceMock<MockBenchmarkClient>>())));

  EXPECT_CALL(option_interpreter_, createSequencer(_, _, _))
      .Times(1)
      .WillOnce(
          testing::Return(testing::ByMove(std::make_unique<testing::NiceMock<MockSequencer>>())));

  int worker_number = 12345;
  auto worker = std::make_unique<ClientWorkerImpl>(
      option_interpreter_, api_, tls_, std::make_unique<Envoy::Stats::IsolatedStoreImpl>(),
      worker_number, 0);
  worker->start();
  worker->waitForCompletion();
}

} // namespace Client
} // namespace Nighthawk
