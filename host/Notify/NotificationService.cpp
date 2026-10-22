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
    "Windows Ayarlar → Gizlilik ve güvenlik → Bildirimler: uygulamaların bildirimlere erişmesine izin verin.",
};

constexpr Texts kEnglish = {
    "Notifications",
    "No notifications",
    "Notification access is off",
    "Cannot read notifications",
    " notifications",
    "Windows Settings → Privacy & security → Notifications: let apps access your notifications.",
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

// @p text cut to @p maxBytes with "…" when it does not fit.
std::string shorten(const std::string& text, std::size_t maxBytes)
{
    static const std::string kEllipsis = "\xE2\x80\xA6";
    if (text.size() <= maxBytes) {
        return text;
    }
    return cutUtf8(text, maxBytes - kEllipsis.size()) + kEllipsis;
}

// What a card line shows of a toast: "app: title" (the body without a
// title; only the app without texts).
std::string summary(const Notification& n)
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
    publish();
}

void NotificationService::publish()
{
    dash_.setCard(kSlot, card());
    dash_.refreshDetail(kSlot);
}

std::string NotificationService::card() const
{
    const Texts& t = texts(language_);
    const std::size_t n = shown_.toasts.size();
    std::string text = recordLine({std::to_string(kIcon), t.title, n > 0U ? std::to_string(n) : std::string()});
    if (shown_.access == NotificationAccess::Denied) {
        return text + "\n" + t.denied;
    }
    if (shown_.access == NotificationAccess::Failed) {
        return text + "\n" + t.failed;
    }
    if (n == 0U) {
        return text + "\n" + t.none;
    }
    for (std::size_t i = 0; i < n && i < kCardLines; ++i) {
        // A card line is cut by the board; 80 bytes is far more than fits.
        text += "\n" + recordLine({shorten(summary(shown_.toasts[i]), 80U)});
    }
    return cutUtf8(text, PROTO_CARD_MAX_BYTES);
}

std::optional<std::string> NotificationService::detail() const
{
    const Texts& t = texts(language_);
    const std::size_t n = shown_.toasts.size();
    std::string text =
        recordLine({t.title, n > 0U ? std::to_string(n) + t.count : std::string(), ""});
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
