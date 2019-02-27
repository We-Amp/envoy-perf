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

#include "nighthawk/source/client/benchmark_client_impl.h"
#include "nighthawk/source/common/platform_util_impl.h"
#include "nighthawk/source/common/rate_limiter_impl.h"
#include "nighthawk/source/common/sequencer_impl.h"

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
        time_system_(timeSystem()), api_(thread_factory_, store_, time_system_),
        dispatcher_(api_.allocateDispatcher()), runtime_(generator_, store_, tls_) {}

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

  void testWithoutRequestQueue(std::string proto, bool use_h2) {
    Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
    request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
    Client::BenchmarkHttpClient client(api_, *dispatcher_, time_system_,
                                       fmt::format("{}://{}/", proto, getTestServerHostAndPort()),
                                       std::move(request_headers), use_h2);

    client.set_connection_timeout(1s);
    client.set_max_pending_requests(1);
    client.set_allow_pending_for_test(true);
    client.initialize(runtime_);

    uint64_t amount = 10;
    uint64_t inflight_response_count = 0;

    std::function<void()> f = [this, &inflight_response_count]() {
      --inflight_response_count;
      if (inflight_response_count == 0) {
        dispatcher_->exit();
      }
    };

    for (uint64_t i = 0; i < amount; i++) {
      if (client.tryStartOne(f)) {
        inflight_response_count++;
      }
    }

    EXPECT_EQ(1, inflight_response_count);

    dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

    EXPECT_EQ(0, inflight_response_count);
    // TODO(oschaaf): benchark client has it's own isolated store.
    EXPECT_EQ(0, store_.counter("nighthawk.upstream_cx_connect_fail").value());
    EXPECT_EQ(0, client.http_bad_response_count());
    EXPECT_EQ(0, client.stream_reset_count());
    // We throttle before the pool, so we expect no pool overflows.
    EXPECT_EQ(0, client.pool_overflow_failures());
    EXPECT_EQ(1, client.http_good_response_count());
  }

  Envoy::Thread::ThreadFactoryImplPosix thread_factory_;
  Envoy::Stats::IsolatedStoreImpl store_;
  Envoy::Event::TimeSystem& time_system_;
  Envoy::Api::Impl api_;
  Envoy::Event::DispatcherPtr dispatcher_;
  Envoy::Runtime::RandomGeneratorImpl generator_;
  Envoy::ThreadLocal::InstanceImpl tls_;
  Envoy::Runtime::LoaderImpl runtime_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, BenchmarkClientTest,
                        // testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()),
                        testing::ValuesIn({Envoy::Network::Address::IpVersion::v4}),
                        Envoy::TestUtility::ipTestParamsToString);

TEST_P(BenchmarkClientTest, BasicTestH1WithRequestQueue) {
  Envoy::Http::HeaderMapImplPtr request_headers = std::make_unique<Envoy::Http::HeaderMapImpl>();
  request_headers->insertMethod().value(Envoy::Http::Headers::get().MethodValues.Get);
  Client::BenchmarkHttpClient client(api_, *dispatcher_, time_system_,
                                     fmt::format("http://{}/", getTestServerHostAndPort()),
                                     std::move(request_headers), false /*use h2*/);

  int amount = 10;
  int inflight_response_count = 0;

  // Allow  request queueing so we can queue up everything all at once.
  client.set_connection_timeout(1s);
  client.set_max_pending_requests(amount);
  client.set_allow_pending_for_test(true);

  // TODO(oschaaf): either get rid of the intialize call, or ensure that this throws
  // when we didn't call it before calling tryStartOne().  client.initialize(runtime_);
  client.initialize(runtime_);

  std::function<void()> f = [this, &inflight_response_count]() {
    if (--inflight_response_count == 0) {
      dispatcher_->exit();
    }
  };

  for (int i = 0; i < amount; i++) {
    if (client.tryStartOne(f)) {
      inflight_response_count++;
    }
  }

  EXPECT_EQ(amount, inflight_response_count);
  dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);

  EXPECT_EQ(0, inflight_response_count);
  // TODO(oschaaf): benchark client has its own isolated store.
  EXPECT_EQ(0, store_.counter("nighthawk.upstream_cx_connect_fail").value());
  EXPECT_EQ(0, client.http_bad_response_count());
  EXPECT_EQ(0, client.stream_reset_count());
  EXPECT_EQ(0, client.pool_overflow_failures());
  EXPECT_EQ(amount, client.http_good_response_count());
}

TEST_P(BenchmarkClientTest, BasicTestH1WithoutRequestQueue) {
  testWithoutRequestQueue("http", false);
}

TEST_P(BenchmarkClientTest, BasicTestHttpsH1WithoutRequestQueue) {
  testWithoutRequestQueue("https", false);
}

TEST_P(BenchmarkClientTest, DISABLED_BasicTestH2WithoutRequestQueue) {
  testWithoutRequestQueue("https", true);
}

} // namespace Nighthawk
