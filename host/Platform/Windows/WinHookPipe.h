// The tray program's end of the Claude Code hook bridge (ADR-007): a named
// pipe server, \\.\pipe\stm32-desk-dashboard-hook, open to the current
// user only and to no remote client. Each connection carries one request
// line (ClaudeHook.h) that is queued as HostEvent::HookRequest with a
// ticket; the service loop answers it with answer(). A client that goes
// away is queued as HostEvent::HookCancelled. Without an answer, the
// request gets "none" after ApprovalService's timeout plus a margin.
#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "ClaudeHook.h"
#include "EventQueue.h"

namespace stm32_audio::host::win {

inline constexpr wchar_t kHookPipeName[] = L"\\\\.\\pipe\\stm32-desk-dashboard-hook";

class WinHookPipe {
public:
    using LogFn = std::function<void(const std::string& line)>;

    WinHookPipe(EventQueue& events, LogFn log);
    ~WinHookPipe();

    WinHookPipe(const WinHookPipe&) = delete;
    WinHookPipe& operator=(const WinHookPipe&) = delete;

    // False if the pipe cannot be created (e.g. another program has it).
    bool start();
    // Every waiting request gets "none"; returns when the threads ended.
    void stop();

    // From the service loop (ApprovalService's reply).
    void answer(std::uint64_t ticket, HookAnswer answer);

private:
    HANDLE createInstance(bool first);
    void acceptLoop();
    void serve(HANDLE pipe, std::uint64_t ticket);

    EventQueue& events_;
    LogFn log_;
    std::thread acceptor_;
    HANDLE stopEvent_ = nullptr;
    HANDLE firstInstance_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::uint64_t, HookAnswer> answers_;  // under mutex_
    int activeWorkers_ = 0;                        // under mutex_
    std::uint64_t nextTicket_ = 0;
    std::wstring sddl_;
};

}  // namespace stm32_audio::host::win
