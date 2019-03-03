#include <chrono>

#include "ares.h"

#include "gtest/gtest.h"

#include "common/api/api_impl.h"
#include "common/common/compiler_requirements.h"
#include "common/common/thread_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "nighthawk/source/client/benchmark_client_impl.h"
#include "nighthawk/source/common/platform_util_impl.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"
#include "nighthawk/source/common/statistic_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "test/integration/http_integration.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/utility.h"

using namespace std::chrono_literals;

namespace Nighthawk {

std::string lorem_ipsum_config;

class BenchmarkClientTest : public Envoy::BaseIntegrationTest,
                            public testing::TestWithParam<Envoy::Network::Address::IpVersion> {
public:
  BenchmarkClientTest()
      : Envoy::BaseIntegrationTest(GetParam(), realTime(), lorem_ipsum_config),
        api_(thread_factory_, store_, timeSystem()), dispatcher_(api_.allocateDispatcher()) {}

  static void SetUpTestCase() {
    Envoy::Filesystem::InstanceImpl filesystem;

    Envoy::TestEnvironment::setEnvVar("TEST_TMPDIR", Envoy::TestEnvironment::temporaryDirectory(),
                                      1);

    const std::string lorem_ipsum_content = filesystem.fileReadToEnd(
        Envoy::TestEnvironment::runfilesPath("nighthawk/test/test_data/lorem_ipsum.txt"));
    Envoy::TestEnvironment::writeStringToFileForTest("lorem_ipsum.txt", lorem_ipsum_content);

    Envoy::TestEnvironment::exec({Envoy::TestEnvironment::runfilesPath("nighthawk/test/certs.sh")});

    lorem_ipsum_config = filesystem.fileReadToEnd(Envoy::TestEnvironment::runfilesPath(
        "nighthawk/test/test_data/benchmark_http_client_test_envoy.yaml"));
    lorem_ipsum_config = Envoy::TestEnvironment::substitute(lorem_ipsum_config);
  }

  void SetUp() override {
    ares_library_init(ARES_LIB_INIT_ALL);
    Envoy::Event::Libevent::Global::initialize();
    BaseIntegrationTest::initialize();
  }

  std::string getTestServerHostAndPort() {
    uint32_t port = lookupPort("listener_0");
    return fmt::format("127.0.0.1:{}", port);
  }

  std::string getTestServerHostAndSslPort() {
    uint32_t port = lookupPort("listener_1");
    return fmt::format("127.0.0.1:{}", port);
  }

  void TearDown() override {
    tls_.shutdownGlobalThreading();
    ares_library_cleanup();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void testBasicFunctionality(std::string uriPath, bool allow_pending, uint64_t max_pending,
                              uint64_t connection_limit, bool use_https, bool use_h2,
                              uint64_t amount_of_request) {

    client_ = std::make_unique<Client::BenchmarkHttpClient>(
        api_, *dispatcher_, std::make_unique<StreamingStatistic>(),
        std::make_unique<StreamingStatistic>(),
        fmt::format("{}://{}{}", use_https ? "https" : "http", getTestServerHostAndPort(), uriPath),
        use_h2);

    client_->set_connection_timeout(10s);
    client_->set_allow_pending_for_test(allow_pending);
    client_->set_max_pending_requests(max_pending);
    client_->set_connection_limit(connection_limit);
    client_->initialize(runtime_);

    uint64_t amount = amount_of_request;
    uint64_t inflight_response_count = 0;

    std::function<void()> f = [this, &inflight_response_count]() {
      --inflight_response_count;
      if (inflight_response_count == 0) {
        dispatcher_->exit();
      }
    };

    for (uint64_t i = 0; i < amount; i++) {
      if (client_->tryStartOne(f)) {
        inflight_response_count++;
      }
    }

    EXPECT_EQ(allow_pending ? amount : 1, inflight_response_count);

    dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

    EXPECT_EQ(0, client_->stream_reset_count());
    // We throttle before the pool, so we expect no pool overflows.
    EXPECT_EQ(0, client_->pool_overflow_failures());
  }

  std::string getChangedCounters() {
    Client::CounterFilter filter = [](std::string, uint64_t value) { return value > 0; };
    return client_->countersToString(filter);
  }

  Envoy::Thread::ThreadFactoryImplPosix thread_factory_;
  Envoy::Stats::IsolatedStoreImpl store_;
  Envoy::Api::Impl api_;
  Envoy::Event::DispatcherPtr dispatcher_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::ThreadLocal::InstanceImpl tls_;
  ::testing::NiceMock<Envoy::Runtime::MockLoader> runtime_;
  std::unique_ptr<Client::BenchmarkHttpClient> client_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, BenchmarkClientTest,
                        // testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()),
                        testing::ValuesIn({Envoy::Network::Address::IpVersion::v4}),
                        Envoy::TestUtility::ipTestParamsToString);

// TODO(oschaaf): this is kind of write-only code, it's not possible to understand the tests
// based on the args we pass to testBasicfunctionality(). Fix this by adding comments.

TEST_P(BenchmarkClientTest, BasicTestH1) {
  testBasicFunctionality("/lorem-ipsum-status-200", false, 1, 1, false, false, 10);
  EXPECT_EQ("client.upstream_cx_http1_total:1\n\
client.upstream_rq_total:1\n\
client.upstream_rq_pending_total:1\n\
client.upstream_cx_total:1\n\
client.upstream_cx_rx_bytes_total:3625\n\
client.upstream_cx_tx_bytes_total:82\n",
            getChangedCounters());
}

TEST_P(BenchmarkClientTest, BasicTestH1404) {
  testBasicFunctionality("/lorem-ipsum-status-404", false, 1, 1, false, false, 10);
  EXPECT_EQ("client.upstream_cx_http1_total:1\n\
client.upstream_rq_total:1\n\
client.upstream_rq_pending_total:1\n\
client.upstream_cx_protocol_error:1\n\
client.upstream_cx_total:1\n\
client.upstream_cx_rx_bytes_total:97\n\
client.upstream_cx_tx_bytes_total:82\n",
            getChangedCounters());
}

TEST_P(BenchmarkClientTest, BasicTestHttpsH1) {
  testBasicFunctionality("/lorem-ipsum-status-200", false, 1, 1, true, false, 10);
  EXPECT_EQ("client.upstream_cx_http1_total:1\n\
client.upstream_rq_total:1\n\
client.upstream_rq_pending_total:1\n\
client.upstream_cx_total:1\n\
client.upstream_cx_rx_bytes_total:3625\n\
client.upstream_cx_tx_bytes_total:82\n",
            getChangedCounters());
}

// The following two are disabled because of trouble with runtime initialization, causing them
// to crash out.
TEST_P(BenchmarkClientTest, DISABLED_BasicTestH2) {
  testBasicFunctionality("/lorem-ipsum-status-200", false, 1, 1, true, true, 10);
  EXPECT_EQ("todo", getChangedCounters());
}

TEST_P(BenchmarkClientTest, BasicTestH2C) {
  testBasicFunctionality("/lorem-ipsum-status-200", false, 1, 1, false, true, 10);
  EXPECT_EQ("client.upstream_rq_total:1\n\
client.upstream_rq_pending_total:1\n\
client.upstream_cx_total:1\n\
client.upstream_cx_rx_bytes_total:3585\n\
client.upstream_cx_http2_total:1\n\
client.upstream_cx_tx_bytes_total:109\n",
            getChangedCounters());
}

TEST_P(BenchmarkClientTest, H1ConnectionFailure) {
  // Kill the test server, so we can't connect.
  // We allow a single connection and no pending. We expect one connection failure.
  test_server_.reset();
  testBasicFunctionality("/lorem-ipsum-status-200", false, 1, 1, false, false, 10);
  EXPECT_EQ("client.upstream_cx_http1_total:1\n\
client.upstream_rq_pending_failure_eject:1\n\
client.upstream_rq_total:1\n\
client.upstream_rq_pending_total:1\n\
client.upstream_cx_total:1\n\
client.upstream_cx_connect_fail:1\n",
            getChangedCounters());
}

TEST_P(BenchmarkClientTest, H1MultiConnectionFailure) {
  // Kill the test server, so we can't connect.
  // We allow a ten connections and ten pending requests. We expect ten connection failures.
  test_server_.reset();
  testBasicFunctionality("/lorem-ipsum-status-200", true, 10, 10, false, false, 10);
  EXPECT_EQ("client.upstream_cx_http1_total:10\n\
client.upstream_rq_pending_failure_eject:10\n\
client.upstream_rq_total:10\n\
client.upstream_rq_pending_total:10\n\
client.upstream_cx_total:10\n\
client.upstream_cx_connect_fail:10\n",
            getChangedCounters());
}

} // namespace Nighthawk
