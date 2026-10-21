#include "ApprovalService.h"

#include <cstdio>

#include "nlohmann/json.hpp"

namespace stm32_audio::host {

namespace {

using nlohmann::json;

struct Texts {
    const char* title;
    const char* tool;
    const char* command;
    const char* description;
    const char* file;
    const char* oldText;
    const char* newText;
    const char* allOccurrences;
    const char* content;
    const char* edits;
    const char* url;
    const char* request;
    const char* input;
    const char* cut;
    const char* allow;
    const char* deny;
    const char* askTerminal;
    const char* yes;
    // card and detail
    const char* noneWaiting;
    const char* waitingOne;   // + count in front
    const char* waitingMany;
    const char* last;
    const char* resultAllowed;
    const char* resultDenied;
    const char* resultTerminal;
    const char* resultTimeout;
    const char* resultNoBoard;
    const char* resultCancelled;
    const char* detailSubtitle;
    const char* sectionWaiting;
    const char* sectionLast;
    const char* noRequests;
};

constexpr Texts kTurkish = {
    "Claude Code izin istiyor", "Araç", "Komut", "Açıklama", "Dosya", "Eski", "Yeni", "Tümü", "İçerik",
    "Değişiklikler", "Adres", "İstem", "Girdi",
    "(kısaltıldı: tamamı ekrana sığmıyor, burada onaylanamaz)", "Onayla", "Reddet", "Terminalde sor", "evet",
    "Onay bekleyen yok", " onay bekliyor", " onay bekliyor", "Son: ", "onaylandı", "reddedildi", "terminalde",
    "süre doldu", "kart yok", "iptal", "Onaylar", "Bekleyen", "Son kararlar", "Henüz istek yok",
};

constexpr Texts kEnglish = {
    "Claude Code asks for permission", "Tool", "Command", "Description", "File", "Old", "New", "All", "Content",
    "Edits", "URL", "Prompt", "Input", "(cut: does not fit the screen, cannot be approved here)", "Allow", "Deny",
    "Ask in terminal", "yes", "No approval waiting", " approval waiting", " approvals waiting", "Last: ",
    "allowed", "denied", "in the terminal", "timed out", "no board", "cancelled", "Approvals", "Waiting",
    "Last decisions", "No requests yet",
};

const Texts& texts(std::uint8_t language)
{
    return language == 1U ? kEnglish : kTurkish;
}

// One line: a line break becomes " ¶ ", a tab a space.
std::string flatten(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '\n') {
            out += " \xC2\xB6 ";
        } else if (c == '\r') {
            continue;
        } else if (c == '\t') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::string stringOf(const json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    return it->dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string lastPathPart(const std::string& path)
{
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1U);
}

std::string hhmm(std::uint32_t localTime)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>((localTime / 3600U) % 24U),
                  static_cast<unsigned>((localTime / 60U) % 60U));
    return buffer;
}

}  // namespace

std::optional<ApprovalInfo> parseApprovalInput(const std::string& text, std::uint8_t language)
{
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto toolIt = root.find("tool_name");
    if (toolIt == root.end() || !toolIt->is_string() || toolIt->get_ref<const std::string&>().empty()) {
        return std::nullopt;
    }
    const Texts& t = texts(language);
    ApprovalInfo info;
    info.tool = toolIt->get<std::string>();
    info.project = lastPathPart(stringOf(root, "cwd"));
    const auto inputIt = root.find("tool_input");
    const json input = (inputIt != root.end() && inputIt->is_object()) ? *inputIt : json::object();

    auto add = [&info](const char* label, const std::string& value, bool essential) {
        if (!value.empty()) {
            info.rows.push_back(ApprovalRow{label, flatten(value), essential});
        }
    };
    add(t.tool, info.tool, true);
    const std::string& tool = info.tool;
    if (tool == "Bash" || tool == "PowerShell") {
        add(t.command, stringOf(input, "command"), true);
        add(t.description, stringOf(input, "description"), false);
    } else if (tool == "Edit") {
        add(t.file, stringOf(input, "file_path"), true);
        add(t.oldText, stringOf(input, "old_string"), true);
        add(t.newText, stringOf(input, "new_string"), true);
        if (input.value("replace_all", false)) {
            add(t.allOccurrences, t.yes, true);
        }
    } else if (tool == "Write") {
        add(t.file, stringOf(input, "file_path"), true);
        add(t.content, stringOf(input, "content"), true);
    } else if (tool == "MultiEdit") {
        add(t.file, stringOf(input, "file_path"), true);
        add(t.edits, stringOf(input, "edits"), true);
    } else if (tool == "NotebookEdit") {
        add(t.file, stringOf(input, "notebook_path"), true);
        add(t.content, stringOf(input, "new_source"), true);
    } else if (tool == "WebFetch") {
        add(t.url, stringOf(input, "url"), true);
        add(t.request, stringOf(input, "prompt"), false);
    } else {
        add(t.input, input.dump(-1, ' ', false, json::error_handler_t::replace), true);
    }
    return info;
}

std::string approvalPrompt(const ApprovalInfo& info, std::size_t position, std::size_t count,
                           std::uint32_t seconds, std::uint8_t language, bool& approvable)
{
    const Texts& t = texts(language);
    std::string subtitle = info.project.empty() ? std::string() : info.project + " \xC2\xB7 ";
    subtitle += std::to_string(position) + "/" + std::to_string(count);
    const std::string head = recordLine({t.title, subtitle, std::to_string(seconds)});
    const std::string allButtons = "\n" + recordLine({"!1", t.allow}) + "\n" + recordLine({"!2", t.deny}) + "\n" +
                                   recordLine({"!3", t.askTerminal});
    const std::string cutButtons = "\n" + recordLine({"!2", t.deny}) + "\n" + recordLine({"!3", t.askTerminal});

    auto rowsText = [&info](bool withContext) {
        std::string text;
        for (const ApprovalRow& row : info.rows) {
            if (row.essential || withContext) {
                text += "\n" + recordLine({"", row.label, row.value});
            }
        }
        return text;
    };

    std::string text = head + rowsText(true) + allButtons;
    if (text.size() <= PROTO_PROMPT_MAX_BYTES) {
        approvable = true;
        return text;
    }
    text = head + rowsText(false) + allButtons;
    if (text.size() <= PROTO_PROMPT_MAX_BYTES) {
        approvable = true;  // only context was left out
        return text;
    }

    // The request itself does not fit: show as much as fits, never approvable.
    approvable = false;
    const std::string marker = "\n" + recordLine({"", "", t.cut});
    const std::size_t room = PROTO_PROMPT_MAX_BYTES - head.size() - marker.size() - cutButtons.size();
    std::string rows;
    for (const ApprovalRow& row : info.rows) {
        if (!row.essential) {
            continue;
        }
        const std::string line = "\n" + recordLine({"", row.label, row.value});
        if (rows.size() + line.size() <= room) {
            rows += line;
            continue;
        }
        const std::string prefix = "\n" + recordLine({"", row.label, ""});
        if (rows.size() + prefix.size() + 8U < room) {
            rows += prefix + cutUtf8(flatten(row.value), room - rows.size() - prefix.size() - 3U) + "\xE2\x80\xA6";
        }
        break;
    }
    return head + rows + marker + cutButtons;
}

// ------------------------------------------------------------------ service

ApprovalService::ApprovalService(DashController& dash, DashController::ClockFn clock, ReplyFn reply, LogFn log)
    : dash_(dash), clock_(std::move(clock)), reply_(std::move(reply)), log_(std::move(log))
{
}

void ApprovalService::setLanguage(std::uint8_t language)
{
    if (language == language_) {
        return;
    }
    language_ = language;
    for (Request& r : queue_) {
        if (const std::optional<ApprovalInfo> info = parseApprovalInput(r.input, language_)) {
            r.info = *info;
        }
    }
    publish();
}

void ApprovalService::onRequest(std::uint64_t ticket, const std::string& input, std::uint64_t nowMs)
{
    const std::optional<ApprovalInfo> info = parseApprovalInput(input, language_);
    const Texts& t = texts(language_);
    if (!info) {
        if (log_) {
            log_("Claude Code request " + std::to_string(ticket) + ": not a permission request; no decision.");
        }
        reply_(ticket, HookAnswer::None);
        return;
    }
    if (!dash_.synced() || queue_.size() >= kMaxQueue) {
        if (log_) {
            log_("Claude Code request " + std::to_string(ticket) + " (" + info->tool + "): " +
                 (dash_.synced() ? "queue full" : "board not connected") + "; asked in the terminal.");
        }
        reply_(ticket, HookAnswer::None);
        Decision d;
        d.localTime = clock_();
        d.tool = info->tool;
        d.summary = info->rows.size() > 1U ? info->rows[1].value : std::string();
        d.result = dash_.synced() ? t.resultTerminal : t.resultNoBoard;
        history_.push_front(std::move(d));
        if (history_.size() > kHistory) {
            history_.pop_back();
        }
        publish();
        return;
    }
    Request r;
    r.ticket = ticket;
    r.promptId = nextPromptId_++;
    r.deadlineMs = nowMs + kAnswerTimeoutMs;
    r.input = input;
    r.info = *info;
    queue_.push_back(std::move(r));
    if (log_) {
        log_("Claude Code request " + std::to_string(ticket) + " (" + info->tool + ") on the board, " +
             std::to_string(queue_.size()) + " waiting.");
    }
    showHead(nowMs);
    publish();
}

void ApprovalService::answer(std::size_t index, HookAnswer answer, const char* how)
{
    const Request r = queue_[index];
    reply_(r.ticket, answer);
    if (log_) {
        log_("Claude Code request " + std::to_string(r.ticket) + " (" + r.info.tool + "): " + how + ".");
    }
    Decision d;
    d.localTime = clock_();
    d.tool = r.info.tool;
    d.summary = r.info.rows.size() > 1U ? r.info.rows[1].value : std::string();
    d.result = how;
    history_.push_front(std::move(d));
    if (history_.size() > kHistory) {
        history_.pop_back();
    }
    queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(index));
    if (shownId_ && *shownId_ == r.promptId) {
        dash_.closePrompt(r.promptId);
        shownId_.reset();
    }
}

void ApprovalService::showHead(std::uint64_t nowMs)
{
    if (queue_.empty()) {
        return;
    }
    const Request& head = queue_.front();
    const std::uint64_t left = head.deadlineMs > nowMs ? head.deadlineMs - nowMs : 0U;
    const auto seconds = static_cast<std::uint32_t>((left + 999U) / 1000U);
    bool approvable = false;
    const std::string text = approvalPrompt(head.info, 1U, queue_.size(), seconds, language_, approvable);
    shownId_ = head.promptId;
    shownApprovable_ = approvable;
    dash_.showPrompt(head.promptId, text);
}

void ApprovalService::onCancelled(std::uint64_t ticket, std::uint64_t nowMs)
{
    for (std::size_t i = 0; i < queue_.size(); ++i) {
        if (queue_[i].ticket == ticket) {
            const bool head = (i == 0U);
            answer(i, HookAnswer::None, texts(language_).resultCancelled);
            if (head) {
                showHead(nowMs);
            }
            publish();
            return;
        }
    }
}

void ApprovalService::onTick(std::uint64_t nowMs)
{
    bool headGone = false;
    bool changed = false;
    for (std::size_t i = 0; i < queue_.size();) {
        if (nowMs >= queue_[i].deadlineMs) {
            headGone = headGone || (i == 0U);
            answer(i, HookAnswer::None, texts(language_).resultTimeout);
            changed = true;
        } else {
            ++i;
        }
    }
    if (headGone) {
        showHead(nowMs);
    }
    if (changed) {
        publish();
    }
}

bool ApprovalService::onAction(const ProtoAction& action, std::uint64_t nowMs)
{
    if (action.kind != PROTO_ACTION_PROMPT) {
        return false;
    }
    for (std::size_t i = 0; i < queue_.size(); ++i) {
        if (queue_[i].promptId != action.ref) {
            continue;
        }
        const Texts& t = texts(language_);
        const bool shownApprovable = shownId_ && *shownId_ == action.ref && shownApprovable_;
        const bool head = (i == 0U);
        if (action.action == 1U && shownApprovable) {
            answer(i, HookAnswer::Allow, t.resultAllowed);
        } else if (action.action == 2U) {
            answer(i, HookAnswer::Deny, t.resultDenied);
        } else {
            // 3, or "allow" for a prompt that was not approvable: no decision.
            answer(i, HookAnswer::None, t.resultTerminal);
        }
        if (head) {
            showHead(nowMs);
        }
        publish();
        return true;
    }
    return true;  // an answer to a prompt that is gone (timed out): nothing to do
}

void ApprovalService::onDisconnected()
{
    const char* how = texts(language_).resultNoBoard;
    while (!queue_.empty()) {
        answer(0U, HookAnswer::None, how);
    }
    shownId_.reset();
    publish();
}

void ApprovalService::cancelAll()
{
    const char* how = texts(language_).resultCancelled;
    while (!queue_.empty()) {
        answer(0U, HookAnswer::None, how);
    }
    publish();
}

std::string ApprovalService::card() const
{
    const Texts& t = texts(language_);
    const std::size_t n = queue_.size();
    std::string text = recordLine({"0", "Claude Code", n > 0U ? std::to_string(n) : std::string()});
    text += "\n" + (n == 0U ? std::string(t.noneWaiting)
                            : std::to_string(n) + (n == 1U ? t.waitingOne : t.waitingMany));
    if (!history_.empty()) {
        const Decision& d = history_.front();
        text += "\n" + std::string(t.last) + d.result + " \xC2\xB7 " + d.tool;
        if (!d.summary.empty()) {
            text += "\n" + d.summary;
        }
    }
    return text;
}

std::optional<std::string> ApprovalService::detail() const
{
    const Texts& t = texts(language_);
    std::string text = recordLine({"Claude Code", t.detailSubtitle, ""});
    if (queue_.empty() && history_.empty()) {
        return text + "\n" + recordLine({"", "", t.noRequests});
    }
    if (!queue_.empty()) {
        text += "\n#" + std::string(t.sectionWaiting);
        for (const Request& r : queue_) {
            text += "\n" + recordLine({"", r.info.tool, r.info.rows.size() > 1U ? r.info.rows[1].value : ""});
        }
    }
    if (!history_.empty()) {
        text += "\n#" + std::string(t.sectionLast);
        for (const Decision& d : history_) {
            text += "\n" + recordLine({"", hhmm(d.localTime) + " " + d.tool,
                                       d.summary.empty() ? d.result : d.result + " \xC2\xB7 " + d.summary});
        }
    }
    return cutUtf8(text, PROTO_DETAIL_MAX_BYTES);
}

void ApprovalService::publish()
{
    dash_.setCard(kSlot, card());
    dash_.refreshDetail(kSlot);
}

}  // namespace stm32_audio::host
