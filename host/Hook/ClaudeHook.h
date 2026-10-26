// Claude Code PermissionRequest hook (ADR-007): the decision output, and
// the probe of the step 10 spike (#24). Platform independent.
//
// The probe ("dashboard-host hook probe") logs what Claude Code sends and
// answers as %APPDATA%\stm32-desk-dashboard\hook-probe.txt says:
//
//   answer=none|allow|deny     (default none: no decision, the normal question)
//   wait=0                     seconds to wait before answering (0..600)
//   exit=0                     exit code (0 normal; e.g. 3 = a failing hook)
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace stm32_audio::host {

enum class HookAnswer { None, Allow, Deny };

// The hook's stdout for a decision; "" for HookAnswer::None (no decision:
// Claude Code asks in the terminal as usual).
std::string permissionDecisionJson(HookAnswer answer);

struct HookProbeConfig {
    HookAnswer answer = HookAnswer::None;
    int waitSeconds = 0;
    int exitCode = 0;
};

// Unknown keys and bad values are ignored (defaults kept).
HookProbeConfig parseHookProbeConfig(const std::optional<std::string>& text);

const char* hookAnswerName(HookAnswer answer);

// ------------------------------------------------- hook <-> tray program
//
// "dashboard-host hook permission" passes the hook input to the tray
// program over a named pipe: one request line (the input as compact JSON),
// one reply line ("allow", "deny" or "none"). Anything else, or no reply,
// counts as "none": no decision.

// The hook input as one line (compact JSON + "\n"); nullopt if it is not
// a JSON object.
std::optional<std::string> hookRequestLine(const std::string& input);
std::string hookReplyLine(HookAnswer answer);
HookAnswer parseHookReply(const std::string& line);

// "dashboard-host hook resolved" (ADR-007 amendment): a request
// answered somewhere else (the dialog of Claude Desktop, the terminal)
// does not end the waiting "hook permission" process, so its prompt would
// stay on the board until the timeout. Claude Code's later events tell the
// tray program that it is answered: PostToolUse / PostToolUseFailure (the
// tool ran: that request), Stop and UserPromptSubmit (the turn ended or
// the user wrote again: every request of the session). The line is the
// hook input reduced to hook_event_name, session_id, tool_name and
// tool_input (tool_response may be large); nullopt for other events or
// without a session_id. The tray replies "none" at once.
std::optional<std::string> hookResolvedLine(const std::string& input);

// The snippet for %USERPROFILE%\.claude\settings.json of this PC (exec
// form: Claude Code starts @p exePath without a shell).
std::string hookSettingsSnippet(const std::string& exePath);

inline constexpr int kHookTimeoutSeconds = 90;        // Claude Code's timeout of the hook
inline constexpr std::uint64_t kHookClientWaitMs = 75000;  // the hook's own limit for the reply
inline constexpr int kResolvedHookTimeoutSeconds = 10;     // Claude Code's timeout of "hook resolved"
inline constexpr std::uint64_t kResolvedClientWaitMs = 3000;  // its own limit for the tray's "none"

// One line of hook-probe.log (JSON, no line end). @p phase "start" or
// "end"; @p input is the hook input as received (kept as JSON when it
// parses, as a string otherwise); @p pid tells parallel hooks apart.
std::string hookProbeLogLine(const std::string& phase, const std::string& localTime, std::uint64_t elapsedMs,
                             std::uint32_t pid, const std::string& input, const HookProbeConfig& config);

}  // namespace stm32_audio::host
