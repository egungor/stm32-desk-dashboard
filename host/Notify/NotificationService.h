// Windows notifications on the board (ADR-009, ADR-010): the bell at the
// top right of the Dashboard (a status icon with the count; red while the
// list has not shown every toast, else normal), its list (the detail of
// slot 2), and a banner for each new toast.
//
//   detail: Bildirimler ␟ "3 bildirim" ␟ \n then per toast, newest first (at
//           most kDetailItems): #app · hh:mm \n ␟ title ␟ body
//   banner: 12 ␟ app ␟ kBannerSeconds ␟ 2 \n title \n body
//
// The list mirrors Windows' notification centre: a toast dismissed on the
// PC leaves the card and the detail at the next poll. A banner goes out for
// a toast that is new since the last snapshot - not for the toasts of the
// first snapshot (the tray started, or access was just allowed), not while
// the board is not synced (a reconnect shows nothing old), not while
// Windows does not accept pop-ups (Do not disturb), and at most one every
// kBannerIntervalMs: of a burst only the newest is shown.
//
// Texts are not logged (they would end up in host.log): only app names.
// Single threaded (the service loop); platform independent.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>

#include "DashController.h"
#include "NotificationSource.h"

namespace stm32_audio::host {

class NotificationService {
public:
    using LogFn = std::function<void(const std::string& line)>;

    struct Stats {
        unsigned bannersSent = 0;
        unsigned bannersQuiet = 0;     // not shown: Windows does not accept pop-ups
        unsigned bannersDropped = 0;   // not shown: board not synced, or gone before its turn
    };

    static constexpr std::uint8_t kSlot = 2;   // ADR-006: 2 notifications (its detail)
    static constexpr int kIcon = 12;           // protocol.md "Icon": bell
    static constexpr std::size_t kDetailItems = 20;
    static constexpr std::size_t kDetailBodyBytes = 240;  // per toast, in the detail
    static constexpr std::uint64_t kBannerIntervalMs = 2000;
    static constexpr unsigned kBannerSeconds = 5;

    NotificationService(DashController& dash, INotificationSource& source, IFocusState& focus, LogFn log);

    void setLanguage(std::uint8_t language);

    // Take a new snapshot if there is one, update the card, send a banner
    // when one is due. Call regularly (e.g. every 500 ms).
    void onTick(std::uint64_t nowMs);

    // The board went away: a banner waiting for its turn is dropped.
    void onDisconnected();

    // The bell now.
    ProtoIndicator indicator() const;
    // OPEN_DETAIL on kSlot (arg 0; there is no previous / next); the toasts
    // it lists count as seen.
    std::optional<std::string> detail();
    // A short summary for the console: the access, the count, the newest.
    std::string summary() const;
    // The banner record of @p n (public for tests).
    std::string banner(const Notification& n) const;

    const Stats& stats() const { return stats_; }

private:
    void take(const NotificationSnapshot& next);
    void publish();

    DashController& dash_;
    INotificationSource& source_;
    IFocusState& focus_;
    LogFn log_;
    std::uint8_t language_ = 0;
    NotificationSnapshot shown_;
    std::set<std::uint32_t> seen_;  // toast ids the list has shown
    std::optional<Notification> pending_;          // banner waiting for its turn
    std::optional<std::uint64_t> lastBannerMs_;
    std::uint8_t bannerId_ = 0;
    Stats stats_;
};

}  // namespace stm32_audio::host
