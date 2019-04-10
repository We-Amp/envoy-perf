#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

#include "common/api/api_impl.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"
#include "test/common/upstream/utility.h"

#include "nighthawk/source/server/http_test_server_filter.h"

namespace Nighthawk {

class HttpTestServerIntegrationTestBase
    : public Envoy::HttpIntegrationTest,
      public testing::TestWithParam<Envoy::Network::Address::IpVersion> {
public:
  HttpTestServerIntegrationTestBase()
      : HttpIntegrationTest(Envoy::Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  // TODO(oschaaf): Modify Envoy's Envoy::IntegrationUtil::makeSingleRequest() to allow for a way to
  // manipulate the request headers before they get send. Then we can eliminate these copies.
  Envoy::BufferingStreamDecoderPtr
  makeSingleRequest(uint32_t port, const std::string& method, const std::string& url,
                    const std::string& body, Envoy::Http::CodecClient::Type type,
                    Envoy::Network::Address::IpVersion ip_version, const std::string& host,
                    const std::string& content_type,
                    std::function<void(Envoy::Http::HeaderMapImpl&)> request_header_delegate) {
    auto addr = Envoy::Network::Utility::resolveUrl(fmt::format(
        "tcp://{}:{}", Envoy::Network::Test::getLoopbackAddressUrlString(ip_version), port));
    return makeSingleRequest(addr, method, url, body, type, host, content_type,
                             request_header_delegate);
  }

  Envoy::BufferingStreamDecoderPtr
  makeSingleRequest(const Envoy::Network::Address::InstanceConstSharedPtr& addr,
                    const std::string& method, const std::string& url, const std::string& body,
                    Envoy::Http::CodecClient::Type type, const std::string& host,
                    const std::string& content_type,
                    std::function<void(Envoy::Http::HeaderMapImpl&)> request_header_delegate) {

    testing::NiceMock<Envoy::Stats::MockIsolatedStatsStore> mock_stats_store;
    Envoy::Event::GlobalTimeSystem time_system;
    Envoy::Api::Impl api(Envoy::Thread::threadFactoryForTest(), mock_stats_store, time_system,
                         Envoy::Filesystem::fileSystemForTest());
    Envoy::Event::DispatcherPtr dispatcher(api.allocateDispatcher());
    std::shared_ptr<Envoy::Upstream::MockClusterInfo> cluster{
        new testing::NiceMock<Envoy::Upstream::MockClusterInfo>()};
    Envoy::Upstream::HostDescriptionConstSharedPtr host_description{
        Envoy::Upstream::makeTestHostDescription(cluster, "tcp://127.0.0.1:80")};
    Envoy::Http::CodecClientProd client(
        type,
        dispatcher->createClientConnection(addr, Envoy::Network::Address::InstanceConstSharedPtr(),
                                           Envoy::Network::Test::createRawBufferSocket(), nullptr),
        host_description, *dispatcher);
    Envoy::BufferingStreamDecoderPtr response(new Envoy::BufferingStreamDecoder([&]() -> void {
      client.close();
      dispatcher->exit();
    }));
    Envoy::Http::StreamEncoder& encoder = client.newStream(*response);
    encoder.getStream().addCallbacks(*response);

    Envoy::Http::HeaderMapImpl headers;
    headers.insertMethod().value(method);
    headers.insertPath().value(url);
    headers.insertHost().value(host);
    headers.insertScheme().value(Envoy::Http::Headers::get().SchemeValues.Http);
    if (!content_type.empty()) {
      headers.insertContentType().value(content_type);
    }
    request_header_delegate(headers);
    encoder.encodeHeaders(headers, body.empty());
    if (!body.empty()) {
      Envoy::Buffer::OwnedImpl body_buffer(body);
      encoder.encodeData(body_buffer, true);
    }

    dispatcher->run(Envoy::Event::Dispatcher::RunType::Block);
    return response;
  }

  void testWithResponseSize(int response_size, bool expect_header = true) {
    Envoy::BufferingStreamDecoderPtr response = makeSingleRequest(
        lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "foo.com", "",
        [response_size](Envoy::Http::HeaderMapImpl& request_headers) {
          const std::string header_config = fmt::format("{{response_size:{}}}", response_size);
          request_headers.addCopy(
              Nighthawk::Server::TestServer::HeaderNames::get().TestServerConfig, header_config);
        });
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("200", response->headers().Status()->value().c_str());
    if (expect_header) {
      auto inserted_header = response->headers().get(Envoy::Http::LowerCaseString("x-supplied-by"));
      ASSERT_NE(nullptr, inserted_header);
      EXPECT_STREQ("nighthawk-test-server", inserted_header->value().c_str());
    }
    if (response_size == 0) {
      EXPECT_EQ(nullptr, response->headers().ContentType());
    } else {
      EXPECT_STREQ("text/plain", response->headers().ContentType()->value().c_str());
    }
    EXPECT_EQ(std::string(response_size, 'a'), response->body());
  }

  void testBadResponseSize(int response_size) {
    Envoy::BufferingStreamDecoderPtr response = makeSingleRequest(
        lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "foo.com", "",
        [response_size](Envoy::Http::HeaderMapImpl& request_headers) {
          const std::string header_config = fmt::format("{{response_size:{}}}", response_size);
          request_headers.addCopy(
              Nighthawk::Server::TestServer::HeaderNames::get().TestServerConfig, header_config);
        });
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("500", response->headers().Status()->value().c_str());
  }
};

class HttpTestServerIntegrationTest : public HttpTestServerIntegrationTestBase {
public:
  void SetUp() override { initialize(); }

  void initialize() override {
    config_helper_.addFilter(R"EOF(
name: test-server
config:
  response_size: 10
  response_headers:
  - { header: { key: "x-supplied-by", value: "nighthawk-test-server"} }
)EOF");
    HttpTestServerIntegrationTestBase::initialize();
  }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, HttpTestServerIntegrationTest,
                        testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()));

TEST_P(HttpTestServerIntegrationTest, TestNoHeaderConfig) {
  Envoy::BufferingStreamDecoderPtr response =
      makeSingleRequest(lookupPort("http"), "GET", "/", "", downstream_protocol_, version_,
                        "foo.com", "", [](Envoy::Http::HeaderMapImpl&) {});
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(std::string(10, 'a'), response->body());
}

TEST_P(HttpTestServerIntegrationTest, TestBasics) {
  testWithResponseSize(1);
  testWithResponseSize(10);
  testWithResponseSize(100);
  testWithResponseSize(1000);
  testWithResponseSize(10000);
}

TEST_P(HttpTestServerIntegrationTest, TestNegative) { testBadResponseSize(-1); }

TEST_P(HttpTestServerIntegrationTest, TestZeroLengthRequest) { testWithResponseSize(0); }

TEST_P(HttpTestServerIntegrationTest, TestMaxBoundaryLengthRequest) {
  const int max = 1024 * 1024 * 4;
  testWithResponseSize(max);
}

TEST_P(HttpTestServerIntegrationTest, DISABLED_TestTooLarge) {
  const int max = 1024 * 1024 * 4;
  testBadResponseSize(max + 1);
}

TEST_P(HttpTestServerIntegrationTest, TestHeaderConfig) {
  Envoy::BufferingStreamDecoderPtr response = makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "foo.com", "",
      [](Envoy::Http::HeaderMapImpl& request_headers) {
        const std::string header_config =
            "{response_headers: [ { header: { key: \"foo\", value: \"bar2\"}, append: true } ]}";
        request_headers.addCopy(Nighthawk::Server::TestServer::HeaderNames::get().TestServerConfig,
                                header_config);
      });
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("bar2",
               response->headers().get(Envoy::Http::LowerCaseString("foo"))->value().c_str());
  EXPECT_EQ(std::string(0, 'a'), response->body());
}

class HttpTestServerIntegrationNoConfigTest : public HttpTestServerIntegrationTestBase {
public:
  void SetUp() override { initialize(); }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override {
    config_helper_.addFilter(R"EOF(
name: test-server
)EOF");
    HttpTestServerIntegrationTestBase::initialize();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, HttpTestServerIntegrationNoConfigTest,
                        testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()));

TEST_P(HttpTestServerIntegrationNoConfigTest, TestNoHeaderConfig) {
  Envoy::BufferingStreamDecoderPtr response =
      makeSingleRequest(lookupPort("http"), "GET", "/", "", downstream_protocol_, version_,
                        "foo.com", "", [](Envoy::Http::HeaderMapImpl&) {});
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(std::string(0, 'a'), response->body());
}

TEST_P(HttpTestServerIntegrationNoConfigTest, TestBasics) {
  testWithResponseSize(1, false);
  testWithResponseSize(10, false);
  testWithResponseSize(100, false);
  testWithResponseSize(1000, false);
  testWithResponseSize(10000, false);
}

TEST_P(HttpTestServerIntegrationNoConfigTest, TestNegative) { testBadResponseSize(-1); }

TEST_P(HttpTestServerIntegrationNoConfigTest, TestZeroLengthRequest) {
  testWithResponseSize(0, false);
}

TEST_P(HttpTestServerIntegrationNoConfigTest, TestMaxBoundaryLengthRequest) {
  const int max = 1024 * 1024 * 4;
  testWithResponseSize(max, false);
}

TEST_P(HttpTestServerIntegrationNoConfigTest, DISABLED_TestTooLarge) {
  const int max = 1024 * 1024 * 4;
  testBadResponseSize(max + 1);
}

TEST_P(HttpTestServerIntegrationNoConfigTest, TestHeaderConfig) {
  Envoy::BufferingStreamDecoderPtr response = makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "foo.com", "",
      [](Envoy::Http::HeaderMapImpl& request_headers) {
        const std::string header_config =
            "{response_headers: [ { header: { key: \"foo\", value: \"bar2\"}, append: true } ]}";
        request_headers.addCopy(Nighthawk::Server::TestServer::HeaderNames::get().TestServerConfig,
                                header_config);
      });
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("bar2",
               response->headers().get(Envoy::Http::LowerCaseString("foo"))->value().c_str());
  EXPECT_EQ(std::string(0, 'a'), response->body());
}

// TODO(oschaaf): maybe add a couple more tests for appending headers.

} // namespace Nighthawk