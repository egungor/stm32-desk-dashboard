// IHttpClient on WinHTTP: HTTPS with the system's proxy settings. GET: 10 s
// timeouts, the body of a 200 answer. POST: up to 3 minutes for the answer
// (the Claude API reading menu images). Bodies at most 1 MB.
#pragma once

#include "HttpClient.h"

namespace stm32_audio::host::win {

class WinHttpClient : public IHttpClient {
public:
    std::optional<std::string> get(const std::string& url) override;
    std::optional<HttpResponse> post(const std::string& url, const HttpHeaders& headers,
                                     const std::string& body) override;
};

}  // namespace stm32_audio::host::win
