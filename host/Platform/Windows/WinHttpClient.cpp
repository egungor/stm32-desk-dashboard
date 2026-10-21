#include "WinHttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

constexpr int kTimeoutMs = 10000;  // resolve, connect, send, receive each
constexpr std::size_t kMaxBody = 1024U * 1024U;

// Closes a WinHTTP handle when it leaves scope.
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle()
    {
        if (h != nullptr) {
            WinHttpCloseHandle(h);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

}  // namespace

std::optional<std::string> WinHttpClient::get(const std::string& url)
{
    const std::wstring wide = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::nullopt;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY: Windows 8.1 and later.
    Handle session(WinHttpOpen(L"stm32-desk-dashboard", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.h == nullptr) {
        return std::nullopt;
    }
    WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (connection.h == nullptr) {
        return std::nullopt;
    }
    Handle request(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (request.h == nullptr ||
        !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        return std::nullopt;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200U) {
        return std::nullopt;
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            return std::nullopt;
        }
        if (available == 0) {
            break;
        }
        if (body.size() + available > kMaxBody) {
            return std::nullopt;
        }
        std::vector<char> chunk(available);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &got)) {
            return std::nullopt;
        }
        body.append(chunk.data(), got);
    }
    return body;
}

}  // namespace stm32_audio::host::win
