#pragma once

#include <string>

#include "absl/strings/string_view.h"

#include "common/common/logger.h"
#include "common/network/dns_impl.h"
#include "common/network/utility.h"

#include "nighthawk/common/exception.h"

namespace Nighthawk {

namespace PlatformUtils {
uint32_t determineCpuCoresWithAffinity();
}

class UriException : public NighthawkException {
public:
  UriException(const std::string& message) : NighthawkException(message) {}
};

class Uri : public Envoy::Logger::Loggable<Envoy::Logger::Id::main> {
public:
  static Uri Parse(absl::string_view uri) {
    auto r = Uri(uri);
    if (!r.isValid()) {
      throw UriException("Invalid URI");
    }
    return r;
  }

  const std::string& host_and_port() const { return host_and_port_; }
  const std::string& host_without_port() const { return host_without_port_; }
  const std::string& path() const { return path_; }
  uint64_t port() const { return port_; }
  const std::string& scheme() const { return scheme_; }

  /**
   * Finds the position of the port separator in the host:port fragment.
   *
   * @param hostname valid "host[:port]" string.
   * @return size_t the position of the port separator, or absl::string_view::npos if none was
   * found.
   */
  static size_t findPortSeparator(absl::string_view hostname);

  Envoy::Network::Address::InstanceConstSharedPtr
  resolve(Envoy::Event::Dispatcher& dispatcher,
          const Envoy::Network::DnsLookupFamily dns_lookup_family);
  Envoy::Network::Address::InstanceConstSharedPtr address() const {
    ASSERT(resolve_attempted_, "resolve() must be called first.");
    return address_;
  }

private:
  Uri(absl::string_view uri);
  bool isValid() const {
    return (scheme_ == "http" || scheme_ == "https") && (port_ > 0 && port_ <= 65535) &&
           // We check that we do not start with '-' because that overlaps with CLI argument
           // parsing. For other hostname validation, we defer to parseInternetAddressAndPort() and
           // dns resolution later on.
           host_without_port_.size() > 0 && host_without_port_[0] != '-';
  }

  bool performDnsLookup(Envoy::Event::Dispatcher& dispatcher,
                        const Envoy::Network::DnsLookupFamily dns_lookup_family);

  // TODO(oschaaf): username, password, query etc. But we may want to look at
  // pulling in a mature uri parser.
  std::string host_and_port_;
  std::string host_without_port_;
  std::string path_;
  uint64_t port_{};
  std::string scheme_;

  Envoy::Network::Address::InstanceConstSharedPtr address_;
  bool resolve_attempted_{};
};

} // namespace Nighthawk