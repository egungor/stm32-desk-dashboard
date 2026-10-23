// INotificationSource on Windows (ADR-009): UserNotificationListener
// (Windows.UI.Notifications.Management, C++/WinRT) polled on a worker
// thread in the multithreaded apartment. The spike (#33) showed that the
// unpackaged tray program gets access without package identity, and that
// its NotificationChanged event is not available to it (0x80070490), so
// the thread reads GetNotificationsAsync(Toast) every 500 ms. When the list
// or the access changed it queues HostEvent::NotificationsChanged.
//
// WinRT stays inside WinNotificationListener.cpp.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "EventQueue.h"
#include "NotificationSource.h"

namespace stm32_audio::host::win {

class WinNotificationListener : public INotificationSource {
public:
    using LogFn = std::function<void(const std::string& line)>;

    WinNotificationListener(EventQueue& events, LogFn log);
    ~WinNotificationListener() override;

    WinNotificationListener(const WinNotificationListener&) = delete;
    WinNotificationListener& operator=(const WinNotificationListener&) = delete;

    // Start the reader thread; false if it could not be started.
    bool start();
    void stop();

    NotificationSnapshot snapshot() const override;
    // Wakes the reader thread, which calls ClearNotifications and reads the
    // list again at once.
    void clearAll() override;

private:
    void run();
    // Store the list and access; true if either changed.
    bool publish(NotificationAccess access, std::vector<Notification> toasts);

    EventQueue& events_;
    LogFn log_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool clear_ = false;  // clearAll() asked; done by the reader thread
    NotificationSnapshot snapshot_;
};

// IFocusState on Windows: SHQueryUserNotificationState (shell32) accepts
// pop-ups only in QUNS_ACCEPTS_NOTIFICATIONS. Whether it follows Do not
// disturb on Windows 11 is checked with "dashboard-host notify focus"
// (ADR-009, REQUIRES WINDOWS).
class WinFocusState : public IFocusState {
public:
    bool acceptsPopups() const override;
};

// One line about the focus state for "notify focus": the
// SHQueryUserNotificationState value and the registry value
// NOC_GLOBAL_SETTING_TOASTS_ENABLED (the ADR's fallback candidate).
std::string describeFocusState();

}  // namespace stm32_audio::host::win
