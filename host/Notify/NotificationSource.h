// Windows notifications as the rest of the host sees them (ADR-009). The
// Windows implementation (WinNotificationListener) polls the
// UserNotificationListener on its own thread; NotificationService only
// takes snapshots, so it is tested with a fake.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stm32_audio::host {

struct Notification {
    std::uint32_t id = 0;         // Windows' id of the toast
    std::uint32_t localTime = 0;  // arrival, as localTimeValue() (DashController.h)
    std::string app;              // display name, e.g. "WhatsApp"
    std::string title;            // first text of the toast; may be empty
    std::string body;             // the other texts, joined by a space
};

enum class NotificationAccess {
    Unknown,  // not asked yet (the reader is starting)
    Allowed,
    Denied,   // the user turned notification access off for apps
    Failed,   // the listener is not available on this PC
};

struct NotificationSnapshot {
    NotificationAccess access = NotificationAccess::Unknown;
    // The toasts in the notification centre now, newest first.
    std::vector<Notification> toasts;
    // Changes with every new list (0: no list yet).
    std::uint32_t version = 0;
};

class INotificationSource {
public:
    virtual ~INotificationSource() = default;
    // The current state; cheap, any thread.
    virtual NotificationSnapshot snapshot() const = 0;
    // Remove every toast from the notification centre (the clear button of
    // the list); the next snapshot shows the result. Any thread, returns at
    // once.
    virtual void clearAll() = 0;
};

// Whether Windows itself shows pop-ups now: false while Do not disturb is
// on (and, on the Windows implementation, in the other quiet states of
// SHQueryUserNotificationState: presentation, full-screen app).
class IFocusState {
public:
    virtual ~IFocusState() = default;
    virtual bool acceptsPopups() const = 0;
};

}  // namespace stm32_audio::host
