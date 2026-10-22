// Claude Code permission requests on the board (ADR-007, v1 step 11).
//
// Requests come from "dashboard-host hook permission" through the named
// pipe (the caller passes them in with a ticket); they wait in a queue and
// the board shows the first one as a prompt (ADR-006). The answer goes back
// through the reply function:
//
//   board button 1 Onayla        -> allow
//   board button 2 Reddet        -> deny
//   board button 3 Terminalde sor -> none (Claude Code asks in the terminal)
//   no answer in kAnswerTimeoutMs, board not connected, board unplugged,
//   queue full                    -> none
//
// Never "allow" by default. A request whose tool input does not fit the
// prompt shows only Reddet and Terminalde sor: what cannot be read in full
// on the board cannot be approved there.
//
// The Claude Code status icon (ADR-010, icon 13 at the top right of the
// Dashboard) shows how many requests wait and, by its colour, what happened
// since its list (the detail of slot 3) was last opened: red while a
// request waits, else yellow if one timed out, else green if one was
// allowed, else normal. Single threaded, platform independent.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ClaudeHook.h"
#include "DashController.h"
#include "protocol.h"

namespace stm32_audio::host {

// What a request shows: the tool, the folder of the session, and rows. The
// "essential" rows are what is approved (a command, a file and its new
// content); the others are context that may be cut.
struct ApprovalRow {
    std::string label;
    std::string value;
    bool essential = true;
};

struct ApprovalInfo {
    std::string tool;
    std::string project;  // last part of cwd
    std::vector<ApprovalRow> rows;
};

// nullopt if @p json is not a hook input with a tool_name. Labels in
// @p language (0 Turkish, 1 English).
std::optional<ApprovalInfo> parseApprovalInput(const std::string& json, std::uint8_t language);

// The prompt record; @p approvable is set false when the essential rows had
// to be cut (then only buttons 2 and 3).
std::string approvalPrompt(const ApprovalInfo& info, std::size_t position, std::size_t count,
                           std::uint32_t seconds, std::uint8_t language, bool& approvable);

class ApprovalService {
public:
    using ReplyFn = std::function<void(std::uint64_t ticket, HookAnswer answer)>;
    using LogFn = std::function<void(const std::string& line)>;

    static constexpr std::uint8_t kSlot = 3;  // ADR-006: 3 Claude Code (its detail)
    static constexpr std::uint8_t kIcon = 13; // protocol.md "Icon": robot
    // Prompt ids of requests: 0..kMaxPromptId (255 is the menu warning's).
    static constexpr std::uint8_t kMaxPromptId = 254;
    static constexpr std::uint64_t kAnswerTimeoutMs = 60000;
    static constexpr std::size_t kMaxQueue = 8;
    static constexpr std::size_t kHistory = 5;

    ApprovalService(DashController& dash, DashController::ClockFn clock, ReplyFn reply, LogFn log);

    void setLanguage(std::uint8_t language);

    // A request from the pipe. Answered at once (none) when the board is
    // not synced or the queue is full.
    void onRequest(std::uint64_t ticket, const std::string& input, std::uint64_t nowMs);
    // The hook went away (Claude Code's timeout, the session ended).
    void onCancelled(std::uint64_t ticket, std::uint64_t nowMs);
    // Timeouts; call regularly with a monotonic clock.
    void onTick(std::uint64_t nowMs);
    // ACTION kind 1 (a prompt button); false for other kinds.
    bool onAction(const ProtoAction& action, std::uint64_t nowMs);
    // The board went away: every waiting request gets "none".
    void onDisconnected();
    // The tray stops: the same.
    void cancelAll();

    std::size_t waiting() const { return queue_.size(); }

    // The status icon now.
    ProtoIndicator indicator() const;
    // OPEN_DETAIL on kSlot: the list; marks timeouts and allowed requests as
    // seen (the icon's yellow and green go).
    std::optional<std::string> detail();

private:
    struct Request {
        std::uint64_t ticket = 0;
        std::uint8_t promptId = 0;
        std::uint64_t deadlineMs = 0;
        std::string input;
        ApprovalInfo info;
    };
    struct Decision {
        std::uint32_t localTime = 0;
        std::string tool;
        std::string summary;
        std::string result;
    };

    void answer(std::size_t index, HookAnswer answer, const char* how);
    void showHead(std::uint64_t nowMs);
    void publish();

    DashController& dash_;
    DashController::ClockFn clock_;
    ReplyFn reply_;
    LogFn log_;
    std::uint8_t language_ = 0;
    std::deque<Request> queue_;
    std::deque<Decision> history_;
    std::uint8_t nextPromptId_ = 0;
    std::optional<std::uint8_t> shownId_;
    bool shownApprovable_ = false;
    bool timedOutUnseen_ = false;
    bool allowedUnseen_ = false;
};

}  // namespace stm32_audio::host
