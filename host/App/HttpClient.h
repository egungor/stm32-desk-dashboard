// HTTP for the host's features (weather, menu import), behind an interface
// so that they are tested without a network. Windows: WinHttpClient.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stm32_audio::host {

struct HttpResponse {
    int status = 0;
    std::string body;
};

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // GET @p url; the body of a 200 answer, or nullopt (no network, other
    // status, timeout). May block for seconds; must be thread-safe.
    virtual std::optional<std::string> get(const std::string& url) = 0;

    // POST @p body to @p url; the answer with any status, or nullopt if
    // there was none (no network, timeout). May block for minutes.
    virtual std::optional<HttpResponse> post(const std::string& url, const HttpHeaders& headers,
                                             const std::string& body) = 0;
};

}  // namespace stm32_audio::host
