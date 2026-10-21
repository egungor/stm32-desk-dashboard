#include "WinHttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

constexpr int kGetTimeoutMs = 10000;    // resolve, connect, send, receive each
constexpr int kPostTimeoutMs = 180000;  // the Claude API reading images takes a while
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

// One HTTPS request; nullopt if no answer arrived.
std::optional<HttpResponse> request(const wchar_t* verb, const std::string& url, const HttpHeaders& headers,
                                    const std::string& body, int timeoutMs)
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
    WinHttpSetTimeouts(session.h, kGetTimeoutMs, kGetTimeoutMs, timeoutMs, timeoutMs);
    Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (connection.h == nullptr) {
        return std::nullopt;
    }
    Handle req(WinHttpOpenRequest(connection.h, verb, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (req.h == nullptr) {
        return std::nullopt;
    }
    std::wstring headerText;
    for (const auto& [name, value] : headers) {
        headerText += toWide(name) + L": " + toWide(value) + L"\r\n";
    }
    if (!WinHttpSendRequest(req.h, headerText.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerText.c_str(),
                            headerText.empty() ? 0 : static_cast<DWORD>(-1L),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        return std::nullopt;
    }

    HttpResponse response;
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }
    response.status = static_cast<int>(status);

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.h, &available)) {
            return std::nullopt;
        }
        if (available == 0) {
            break;
        }
        if (response.body.size() + available > kMaxBody) {
            return std::nullopt;
        }
        std::vector<char> chunk(available);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, chunk.data(), available, &got)) {
            return std::nullopt;
        }
        response.body.append(chunk.data(), got);
    }
    return response;
}

}  // namespace

std::optional<std::string> WinHttpClient::get(const std::string& url)
{
    std::optional<HttpResponse> response = request(L"GET", url, {}, {}, kGetTimeoutMs);
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    return std::move(response->body);
}

std::optional<HttpResponse> WinHttpClient::post(const std::string& url, const HttpHeaders& headers,
                                                const std::string& body)
{
    return request(L"POST", url, headers, body, kPostTimeoutMs);
}

}  // namespace stm32_audio::host::win
