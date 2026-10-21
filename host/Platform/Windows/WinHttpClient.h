// IHttpClient on WinHTTP: HTTPS GET with the system's proxy settings and
// 10 s timeouts; the body of a 200 answer, at most 1 MB.
#pragma once

#include "BackgroundFetch.h"

namespace stm32_audio::host::win {

class WinHttpClient : public IHttpClient {
public:
    std::optional<std::string> get(const std::string& url) override;
};

}  // namespace stm32_audio::host::win
