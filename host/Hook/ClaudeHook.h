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

// The snippet for %USERPROFILE%\.claude\settings.json of this PC (exec
// form: Claude Code starts @p exePath without a shell).
std::string hookSettingsSnippet(const std::string& exePath);

inline constexpr int kHookTimeoutSeconds = 90;        // Claude Code's timeout of the hook
inline constexpr std::uint64_t kHookClientWaitMs = 75000;  // the hook's own limit for the reply

// One line of hook-probe.log (JSON, no line end). @p phase "start" or
// "end"; @p input is the hook input as received (kept as JSON when it
// parses, as a string otherwise); @p pid tells parallel hooks apart.
std::string hookProbeLogLine(const std::string& phase, const std::string& localTime, std::uint64_t elapsedMs,
                             std::uint32_t pid, const std::string& input, const HookProbeConfig& config);

}  // namespace stm32_audio::host
