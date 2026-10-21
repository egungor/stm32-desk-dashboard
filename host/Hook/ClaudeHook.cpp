#include "ClaudeHook.h"

#include <cstdlib>

#include "nlohmann/json.hpp"

namespace stm32_audio::host {

namespace {

std::string trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1U);
}

std::optional<int> parseInt(const std::string& text, int min, int max)
{
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value < min || value > max) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

}  // namespace

std::string permissionDecisionJson(HookAnswer answer)
{
    if (answer == HookAnswer::None) {
        return {};
    }
    const nlohmann::json out = {
        {"hookSpecificOutput",
         {{"hookEventName", "PermissionRequest"},
          {"decision", {{"behavior", answer == HookAnswer::Allow ? "allow" : "deny"}}}}},
    };
    return out.dump();
}

const char* hookAnswerName(HookAnswer answer)
{
    switch (answer) {
    case HookAnswer::Allow:
        return "allow";
    case HookAnswer::Deny:
        return "deny";
    case HookAnswer::None:
        break;
    }
    return "none";
}

HookProbeConfig parseHookProbeConfig(const std::optional<std::string>& text)
{
    HookProbeConfig config;
    if (!text) {
        return config;
    }
    std::size_t start = (text->compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3U : 0U;
    while (start <= text->size()) {
        std::size_t end = text->find('\n', start);
        if (end == std::string::npos) {
            end = text->size();
        }
        const std::string line = trim(text->substr(start, end - start));
        start = end + 1U;
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1U));
        if (key == "answer") {
            if (value == "allow") {
                config.answer = HookAnswer::Allow;
            } else if (value == "deny") {
                config.answer = HookAnswer::Deny;
            } else if (value == "none") {
                config.answer = HookAnswer::None;
            }
        } else if (key == "wait") {
            config.waitSeconds = parseInt(value, 0, 600).value_or(config.waitSeconds);
        } else if (key == "exit") {
            config.exitCode = parseInt(value, 0, 255).value_or(config.exitCode);
        }
    }
    return config;
}

std::optional<std::string> hookRequestLine(const std::string& input)
{
    const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (!parsed.is_object()) {
        return std::nullopt;
    }
    return parsed.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

std::string hookReplyLine(HookAnswer answer)
{
    return std::string(hookAnswerName(answer)) + "\n";
}

HookAnswer parseHookReply(const std::string& line)
{
    const std::string word = trim(line);
    if (word == "allow") {
        return HookAnswer::Allow;
    }
    if (word == "deny") {
        return HookAnswer::Deny;
    }
    return HookAnswer::None;
}

std::string hookSettingsSnippet(const std::string& exePath)
{
    const nlohmann::ordered_json hook = {
        {"type", "command"},
        {"command", exePath},
        {"args", nlohmann::ordered_json::array({"hook", "permission"})},
        {"timeout", kHookTimeoutSeconds},
    };
    const nlohmann::ordered_json root = {
        {"hooks",
         {{"PermissionRequest",
           nlohmann::ordered_json::array({{{"matcher", "Bash|PowerShell|Edit|MultiEdit|Write|NotebookEdit|WebFetch"},
                                           {"hooks", nlohmann::ordered_json::array({hook})}}})}}},
    };
    return root.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

std::string hookProbeLogLine(const std::string& phase, const std::string& localTime, std::uint64_t elapsedMs,
                             std::uint32_t pid, const std::string& input, const HookProbeConfig& config)
{
    nlohmann::ordered_json line = nlohmann::ordered_json::object();
    line["phase"] = phase;
    line["time"] = localTime;
    line["elapsed_ms"] = elapsedMs;
    line["pid"] = pid;
    if (phase == "start") {
        const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(input, nullptr, false);
        if (parsed.is_discarded()) {
            line["input_raw"] = input;
        } else {
            line["input"] = parsed;
        }
    } else {
        line["answer"] = hookAnswerName(config.answer);
        line["wait_s"] = config.waitSeconds;
        line["exit"] = config.exitCode;
    }
    return line.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

}  // namespace stm32_audio::host
