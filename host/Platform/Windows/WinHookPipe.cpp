#include "WinHookPipe.h"

#include <sddl.h>

#include <chrono>
#include <vector>

#include "ApprovalService.h"
#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

constexpr DWORD kBufferBytes = 64U * 1024U;
constexpr std::size_t kMaxRequestBytes = 256U * 1024U;
constexpr DWORD kRequestReadMs = 5000;
// The board's timeout plus a margin for the loop; the hook client waits
// kHookClientWaitMs.
constexpr std::uint64_t kAnswerWaitMs = ApprovalService::kAnswerTimeoutMs + 5000U;

// "D:P(A;;GA;;;<SID of this user>)": only the current user may open it.
std::wstring currentUserSddl()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD size = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    std::wstring sddl;
    if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
        wchar_t* sid = nullptr;
        if (ConvertSidToStringSidW(user->User.Sid, &sid)) {
            sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return sddl;
}

// Overlapped I/O on the pipe with a timeout; false on error or timeout.
bool waitIo(HANDLE pipe, OVERLAPPED& ov, BOOL started, DWORD timeoutMs, DWORD& bytes)
{
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    if (WaitForSingleObject(ov.hEvent, timeoutMs) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        (void)GetOverlappedResult(pipe, &ov, &bytes, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &ov, &bytes, FALSE) != FALSE;
}

}  // namespace

WinHookPipe::WinHookPipe(EventQueue& events, LogFn log) : events_(events), log_(std::move(log)) {}

WinHookPipe::~WinHookPipe()
{
    stop();
}

HANDLE WinHookPipe::createInstance(bool first)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (sddl_.empty() ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl_.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    const HANDLE pipe =
        CreateNamedPipeW(kHookPipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0U),
                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                         PIPE_UNLIMITED_INSTANCES, kBufferBytes, kBufferBytes, 0, &sa);
    LocalFree(sd);
    return pipe;
}

bool WinHookPipe::start()
{
    if (acceptor_.joinable()) {
        return true;
    }
    sddl_ = currentUserSddl();
    // The first instance claims the name: if another program has it, this
    // fails instead of sharing it.
    firstInstance_ = createInstance(true);
    if (firstInstance_ == INVALID_HANDLE_VALUE) {
        if (log_) {
            log_("WARN: Claude Code hook pipe not available (error " + std::to_string(GetLastError()) +
                 "); approvals stay in the terminal.");
        }
        return false;
    }
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stopping_ = false;
    acceptor_ = std::thread([this] { acceptLoop(); });
    return true;
}

void WinHookPipe::stop()
{
    if (!acceptor_.joinable()) {
        return;
    }
    stopping_ = true;
    SetEvent(stopEvent_);
    cv_.notify_all();
    acceptor_.join();
    {
        // Workers notice stopping_ within half a second and answer "none".
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(10), [this] { return activeWorkers_ == 0; });
    }
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void WinHookPipe::answer(std::uint64_t ticket, HookAnswer answer)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        answers_[ticket] = answer;
    }
    cv_.notify_all();
}

void WinHookPipe::acceptLoop()
{
    HANDLE pipe = firstInstance_;
    firstInstance_ = INVALID_HANDLE_VALUE;
    HANDLE connected = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (!stopping_) {
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = createInstance(false);
            if (pipe == INVALID_HANDLE_VALUE) {
                if (WaitForSingleObject(stopEvent_, 1000) == WAIT_OBJECT_0) {
                    break;
                }
                continue;
            }
        }
        OVERLAPPED ov{};
        ResetEvent(connected);
        ov.hEvent = connected;
        bool ok = ConnectNamedPipe(pipe, &ov) != FALSE;
        if (!ok) {
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                ok = true;
            } else if (error == ERROR_IO_PENDING) {
                HANDLE handles[2] = {connected, stopEvent_};
                const DWORD which = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                DWORD unused = 0;
                if (which == WAIT_OBJECT_0) {
                    ok = GetOverlappedResult(pipe, &ov, &unused, FALSE) != FALSE;
                } else {
                    CancelIoEx(pipe, &ov);
                    (void)GetOverlappedResult(pipe, &ov, &unused, TRUE);
                }
            }
        }
        if (!ok) {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            continue;
        }
        const std::uint64_t ticket = ++nextTicket_;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++activeWorkers_;
        }
        std::thread([this, pipe, ticket] { serve(pipe, ticket); }).detach();
        pipe = INVALID_HANDLE_VALUE;  // the worker owns it
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
    }
    CloseHandle(connected);
}

void WinHookPipe::serve(HANDLE pipe, std::uint64_t ticket)
{
    HANDLE io = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::string request;
    bool complete = false;

    // One request line.
    const auto readStart = GetTickCount64();
    while (!complete && request.size() < kMaxRequestBytes) {
        const ULONGLONG spent = GetTickCount64() - readStart;
        if (spent >= kRequestReadMs) {
            break;
        }
        char chunk[4096];
        OVERLAPPED ov{};
        ResetEvent(io);
        ov.hEvent = io;
        DWORD got = 0;
        const BOOL started = ReadFile(pipe, chunk, sizeof(chunk), nullptr, &ov);
        if (!waitIo(pipe, ov, started, static_cast<DWORD>(kRequestReadMs - spent), got) || got == 0) {
            break;
        }
        request.append(chunk, got);
        complete = request.find('\n') != std::string::npos;
    }

    HookAnswer reply = HookAnswer::None;
    bool clientGone = false;
    if (complete) {
        request.resize(request.find('\n'));
        events_.pushHook(HostEvent::HookRequest, ticket, request);
        const auto waitStart = GetTickCount64();
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            const auto it = answers_.find(ticket);
            if (it != answers_.end()) {
                reply = it->second;
                answers_.erase(it);
                break;
            }
            if (stopping_ || GetTickCount64() - waitStart >= kAnswerWaitMs) {
                break;
            }
            cv_.wait_for(lock, std::chrono::milliseconds(500));
            // Has the hook gone (Claude Code's timeout, Ctrl+C)?
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) &&
                GetLastError() == ERROR_BROKEN_PIPE) {
                clientGone = true;
                break;
            }
        }
    }
    if (clientGone) {
        events_.pushHook(HostEvent::HookCancelled, ticket);
    } else if (complete) {
        const std::string line = hookReplyLine(reply);
        OVERLAPPED ov{};
        ResetEvent(io);
        ov.hEvent = io;
        DWORD written = 0;
        const BOOL started = WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), nullptr, &ov);
        (void)waitIo(pipe, ov, started, 2000, written);
        (void)FlushFileBuffers(pipe);
    }
    (void)DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    CloseHandle(io);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        answers_.erase(ticket);  // an answer that came too late
        --activeWorkers_;
    }
    cv_.notify_all();
}

}  // namespace stm32_audio::host::win
