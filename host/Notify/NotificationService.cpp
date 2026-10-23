#include "NotificationService.h"

#include <cstdio>
#include <set>
#include <utility>

namespace stm32_audio::host {

namespace {

struct Texts {
    const char* title;
    const char* none;
    const char* denied;
    const char* failed;
    const char* count;  // number in front
    const char* howTo;
};

constexpr Texts kTurkish = {
    "Bildirimler",
    "Bildirim yok",
    "Bildirim erişimi kapalı",
    "Bildirimler okunamıyor",
    " bildirim",
    "Windows Ayarlar > Gizlilik ve güvenlik > Bildirimler: uygulamaların bildirimlere erişmesine izin verin.",
};

constexpr Texts kEnglish = {
    "Notifications",
    "No notifications",
    "Notification access is off",
    "Cannot read notifications",
    " notifications",
    "Windows Settings > Privacy & security > Notifications: let apps access your notifications.",
};

const Texts& texts(std::uint8_t language)
{
    return language == 1U ? kEnglish : kTurkish;
}

std::string hhmm(std::uint32_t localTime)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>((localTime / 3600U) % 24U),
                  static_cast<unsigned>((localTime / 60U) % 60U));
    return buffer;
}

// @p text cut to @p maxBytes with "..." when it does not fit (the board's
// fonts have no "…").
std::string shorten(const std::string& text, std::size_t maxBytes)
{
    static const std::string kEllipsis = "...";
    if (text.size() <= maxBytes) {
        return text;
    }
    return cutUtf8(text, maxBytes - kEllipsis.size()) + kEllipsis;
}

// One line about a toast: "app: title" (the body without a title; only the
// app without texts).
std::string oneLine(const Notification& n)
{
    const std::string& what = n.title.empty() ? n.body : n.title;
    return what.empty() ? n.app : n.app + ": " + what;
}

}  // namespace

NotificationService::NotificationService(DashController& dash, INotificationSource& source, IFocusState& focus,
                                         LogFn log)
    : dash_(dash), source_(source), focus_(focus), log_(std::move(log))
{
    publish();
}

void NotificationService::setLanguage(std::uint8_t language)
{
    if (language != language_) {
        language_ = language;
        publish();
    }
}

void NotificationService::onTick(std::uint64_t nowMs)
{
    NotificationSnapshot next = source_.snapshot();
    if (next.version != shown_.version || next.access != shown_.access) {
        take(next);
    }
    if (!pending_) {
        return;
    }
    if (!dash_.synced()) {
        pending_.reset();
        ++stats_.bannersDropped;
        return;
    }
    if (lastBannerMs_ && nowMs - *lastBannerMs_ < kBannerIntervalMs) {
        return;  // its turn comes; a newer toast may replace it
    }
    if (!focus_.acceptsPopups()) {
        pending_.reset();
        ++stats_.bannersQuiet;
        return;
    }
    dash_.showBanner(++bannerId_, banner(*pending_));
    lastBannerMs_ = nowMs;
    ++stats_.bannersSent;
    pending_.reset();
}

void NotificationService::onDisconnected()
{
    if (pending_) {
        pending_.reset();
        ++stats_.bannersDropped;
    }
}

void NotificationService::take(const NotificationSnapshot& next)
{
    if (next.access != shown_.access && log_) {
        switch (next.access) {
        case NotificationAccess::Allowed:
            log_("Notifications: access allowed.");
            break;
        case NotificationAccess::Denied:
            log_("WARN: notification access is off (Windows Settings, Privacy & security, Notifications).");
            break;
        case NotificationAccess::Failed:
            log_("WARN: Windows notifications cannot be read on this PC.");
            break;
        case NotificationAccess::Unknown:
            break;
        }
    }

    // New toasts: not in the last list. The first list after access was
    // allowed only sets what is known.
    const bool baseline = shown_.access != NotificationAccess::Allowed;
    std::set<std::uint32_t> known;
    for (const Notification& n : shown_.toasts) {
        known.insert(n.id);
    }
    const Notification* newest = nullptr;
    unsigned fresh = 0;
    for (const Notification& n : next.toasts) {  // newest first
        if (known.count(n.id) == 0U) {
            if (newest == nullptr) {
                newest = &n;
            }
            ++fresh;
        }
    }
    if (!baseline && newest != nullptr) {
        if (pending_) {
            ++stats_.bannersDropped;  // replaced by a newer one
        }
        pending_ = *newest;
        if (log_) {
            log_("Notification from " + newest->app + (fresh > 1U ? " (+" + std::to_string(fresh - 1U) + ")" : ""));
        }
    }
    if (pending_) {
        bool still = false;
        for (const Notification& n : next.toasts) {
            still = still || n.id == pending_->id;
        }
        if (!still) {
            pending_.reset();  // dismissed on the PC before its turn
            ++stats_.bannersDropped;
        }
    }
    shown_ = next;
    // Forget seen ids that left the list (the set stays small).
    std::set<std::uint32_t> still;
    for (const Notification& n : shown_.toasts) {
        if (seen_.count(n.id) != 0U) {
            still.insert(n.id);
        }
    }
    seen_.swap(still);
    publish();
}

void NotificationService::publish()
{
    // No card any more (ADR-010): the bell is read by the service loop
    // (indicator()); an open list follows.
    dash_.refreshDetail(kSlot);
}

ProtoIndicator NotificationService::indicator() const
{
    ProtoIndicator ind{};
    ind.icon = static_cast<std::uint8_t>(kIcon);
    ind.slot = kSlot;
    const std::size_t n = shown_.toasts.size();
    ind.number = static_cast<std::uint8_t>(n > 255U ? 255U : n);
    ind.colour = PROTO_INDICATOR_NORMAL;
    for (const Notification& toast : shown_.toasts) {
        if (seen_.count(toast.id) == 0U) {
            ind.colour = PROTO_INDICATOR_RED;
        }
    }
    return ind;
}

std::string NotificationService::summary() const
{
    const Texts& t = texts(language_);
    if (shown_.access == NotificationAccess::Denied) {
        return t.denied;
    }
    if (shown_.access == NotificationAccess::Failed) {
        return t.failed;
    }
    if (shown_.toasts.empty()) {
        return t.none;
    }
    return std::to_string(shown_.toasts.size()) + t.count + "; " + oneLine(shown_.toasts.front());
}

std::optional<std::string> NotificationService::detail()
{
    for (const Notification& toast : shown_.toasts) {
        seen_.insert(toast.id);
    }
    const Texts& t = texts(language_);
    const std::size_t n = shown_.toasts.size();
    const bool clearable = shown_.access == NotificationAccess::Allowed && n > 0U;
    std::string text =
        recordLine({t.title, n > 0U ? std::to_string(n) + t.count : std::string(), clearable ? "c" : ""});
    if (shown_.access == NotificationAccess::Denied || shown_.access == NotificationAccess::Failed) {
        text += "\n" + recordLine({"", "", shown_.access == NotificationAccess::Denied ? t.denied : t.failed});
        if (shown_.access == NotificationAccess::Denied) {
            text += "\n" + recordLine({"", "", t.howTo});
        }
        return text;
    }
    if (n == 0U) {
        return text + "\n" + recordLine({"", "", t.none});
    }
    for (std::size_t i = 0; i < n && i < kDetailItems; ++i) {
        const Notification& toast = shown_.toasts[i];
        std::string item = "\n#" + recordLine({toast.app + " \xC2\xB7 " + hhmm(toast.localTime)});
        if (!toast.title.empty() || !toast.body.empty()) {
            item += "\n" + recordLine({"", shorten(toast.title, 80U), shorten(toast.body, kDetailBodyBytes)});
        }
        if (text.size() + item.size() > PROTO_DETAIL_MAX_BYTES) {
            break;  // whole toasts only
        }
        text += item;
    }
    return text;
}

bool NotificationService::onAction(const ProtoAction& action)
{
    if (action.kind != PROTO_ACTION_CARD || action.ref != kSlot || action.action != PROTO_DETAIL_ACTION_CLEAR ||
        shown_.access != NotificationAccess::Allowed) {
        return false;
    }
    if (!shown_.toasts.empty()) {
        source_.clearAll();  // the list follows with the next snapshot
        if (pending_) {
            pending_.reset();
            ++stats_.bannersDropped;
        }
        if (log_) {
            log_("Notifications: cleared from the board (" + std::to_string(shown_.toasts.size()) + ").");
        }
    }
    return true;
}

std::string NotificationService::banner(const Notification& n) const
{
    std::string text = recordLine({std::to_string(kIcon), n.app, std::to_string(kBannerSeconds),
                                   std::to_string(kSlot)});
    if (!n.title.empty()) {
        text += "\n" + recordLine({n.title});
    }
    if (!n.body.empty()) {
        text += "\n" + recordLine({n.body});
    }
    return cutUtf8(text, PROTO_BANNER_MAX_BYTES);
}

}  // namespace stm32_audio::host
