#include "WinNotificationListener.h"

#include <windows.h>
#include <unknwn.h>  // before C++/WinRT: classic COM interop
#include <shellapi.h>

#include <winrt/Windows.ApplicationModel.h>  // AppInfo of a UserNotification
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

#include "DashController.h"  // localTimeValue

namespace stm32_audio::host::win {

namespace {

using winrt::Windows::UI::Notifications::KnownNotificationBindings;
using winrt::Windows::UI::Notifications::NotificationKinds;
using winrt::Windows::UI::Notifications::UserNotification;
using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
using winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus;

constexpr auto kPollInterval = std::chrono::milliseconds(500);
constexpr auto kDeniedInterval = std::chrono::seconds(5);   // access off: look again now and then
constexpr auto kFailedInterval = std::chrono::seconds(30);  // the listener failed: try again

// A FILETIME value (100 ns since 1601, UTC) as Windows' local time in the
// form of TIME (localTimeValue).
std::uint32_t localTimeOf(std::int64_t filetime)
{
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(filetime) & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(filetime) >> 32);
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return 0U;
    }
    return localTimeValue(local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
}

struct Read {
    Notification toast;
    std::int64_t created = 0;  // FILETIME, for the order
};

Read readToast(const UserNotification& n)
{
    Read r;
    r.toast.id = n.Id();
    r.created = static_cast<std::int64_t>(winrt::clock::to_file_time(n.CreationTime()).value);
    r.toast.localTime = localTimeOf(r.created);
    try {
        r.toast.app = winrt::to_string(n.AppInfo().DisplayInfo().DisplayName());
    } catch (const winrt::hresult_error&) {
        r.toast.app.clear();
    }
    if (r.toast.app.empty()) {
        try {
            r.toast.app = winrt::to_string(n.AppInfo().AppUserModelId());
        } catch (const winrt::hresult_error&) {
            r.toast.app = "?";
        }
    }
    try {
        const auto binding = n.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
        if (binding) {
            bool first = true;
            for (const auto& element : binding.GetTextElements()) {
                std::string text = winrt::to_string(element.Text());
                if (text.empty()) {
                    continue;
                }
                if (first) {
                    r.toast.title = std::move(text);
                    first = false;
                } else {
                    r.toast.body += r.toast.body.empty() ? text : " " + text;
                }
            }
        }
    } catch (const winrt::hresult_error&) {
        // Some toasts carry no ToastGeneric texts: the app alone is shown.
    }
    return r;
}

bool sameToast(const Notification& a, const Notification& b)
{
    return a.id == b.id && a.localTime == b.localTime && a.app == b.app && a.title == b.title && a.body == b.body;
}

bool sameList(const std::vector<Notification>& a, const std::vector<Notification>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameToast);
}

std::string hresultText(const winrt::hresult_error& e)
{
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(e.code()));
    return code;
}

}  // namespace

WinNotificationListener::WinNotificationListener(EventQueue& events, LogFn log)
    : events_(events), log_(std::move(log))
{
}

WinNotificationListener::~WinNotificationListener()
{
    stop();
}

bool WinNotificationListener::start()
{
    if (thread_.joinable()) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = false;
    }
    try {
        thread_ = std::thread([this] { run(); });
    } catch (...) {
        return false;
    }
    return true;
}

void WinNotificationListener::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

NotificationSnapshot WinNotificationListener::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool WinNotificationListener::publish(NotificationAccess access, std::vector<Notification> toasts)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (access == snapshot_.access && snapshot_.version != 0U && sameList(toasts, snapshot_.toasts)) {
        return false;
    }
    snapshot_.access = access;
    snapshot_.toasts = std::move(toasts);
    ++snapshot_.version;
    if (snapshot_.version == 0U) {
        snapshot_.version = 1U;  // 0 means "no list yet"
    }
    return true;
}

void WinNotificationListener::run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    UserNotificationListener listener{nullptr};
    bool asked = false;
    bool reportedFailure = false;
    std::chrono::milliseconds wait(0);  // the first read at once

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (cv_.wait_for(lock, wait, [this] { return stop_; })) {
                break;
            }
        }
        wait = kPollInterval;

        NotificationAccess access = NotificationAccess::Failed;
        std::vector<Notification> toasts;
        try {
            if (!listener) {
                listener = UserNotificationListener::Current();
            }
            UserNotificationListenerAccessStatus status = listener.GetAccessStatus();
            if (status == UserNotificationListenerAccessStatus::Unspecified && !asked) {
                asked = true;
                status = listener.RequestAccessAsync().get();
            }
            if (status != UserNotificationListenerAccessStatus::Allowed) {
                access = NotificationAccess::Denied;
                wait = kDeniedInterval;
            } else {
                std::vector<Read> read;
                for (const auto& n : listener.GetNotificationsAsync(NotificationKinds::Toast).get()) {
                    read.push_back(readToast(n));
                }
                // Newest first; ids grow with time, so they order toasts of
                // the same instant.
                std::sort(read.begin(), read.end(), [](const Read& a, const Read& b) {
                    return a.created != b.created ? a.created > b.created : a.toast.id > b.toast.id;
                });
                for (Read& r : read) {
                    toasts.push_back(std::move(r.toast));
                }
                access = NotificationAccess::Allowed;
            }
            reportedFailure = false;
        } catch (const winrt::hresult_error& e) {
            if (!reportedFailure && log_) {
                log_("WARN: cannot read Windows notifications (" + hresultText(e) + "); trying again later.");
            }
            reportedFailure = true;
            listener = nullptr;
            wait = kFailedInterval;
        } catch (...) {
            if (!reportedFailure && log_) {
                log_("WARN: cannot read Windows notifications; trying again later.");
            }
            reportedFailure = true;
            listener = nullptr;
            wait = kFailedInterval;
        }

        if (publish(access, std::move(toasts))) {
            events_.push(HostEvent::NotificationsChanged);
        }
    }

    winrt::uninit_apartment();
}

// ------------------------------------------------------------------ focus

namespace {

bool queryFocus(QUERY_USER_NOTIFICATION_STATE& state)
{
    return SUCCEEDED(SHQueryUserNotificationState(&state));
}

const char* focusName(QUERY_USER_NOTIFICATION_STATE s)
{
    switch (s) {
    case QUNS_NOT_PRESENT:
        return "NOT_PRESENT (screen locked or saver)";
    case QUNS_BUSY:
        return "BUSY (full-screen app)";
    case QUNS_RUNNING_D3D_FULL_SCREEN:
        return "RUNNING_D3D_FULL_SCREEN";
    case QUNS_PRESENTATION_MODE:
        return "PRESENTATION_MODE";
    case QUNS_ACCEPTS_NOTIFICATIONS:
        return "ACCEPTS_NOTIFICATIONS";
    case QUNS_QUIET_TIME:
        return "QUIET_TIME";
    case QUNS_APP:
        return "APP (full-screen Store app)";
    default:
        return "?";
    }
}

}  // namespace

bool WinFocusState::acceptsPopups() const
{
    QUERY_USER_NOTIFICATION_STATE state{};
    if (!queryFocus(state)) {
        return true;  // unknown: show, as without the rule
    }
    return state == QUNS_ACCEPTS_NOTIFICATIONS;
}

std::string describeFocusState()
{
    std::string line = "SHQueryUserNotificationState=";
    QUERY_USER_NOTIFICATION_STATE state{};
    if (queryFocus(state)) {
        line += std::to_string(static_cast<int>(state)) + " " + focusName(state);
    } else {
        line += "FAIL";
    }

    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS rc = RegGetValueW(HKEY_CURRENT_USER,
                                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings",
                                    L"NOC_GLOBAL_SETTING_TOASTS_ENABLED", RRF_RT_REG_DWORD, nullptr, &value, &size);
    line += "  NOC_GLOBAL_SETTING_TOASTS_ENABLED=";
    line += rc == ERROR_SUCCESS ? std::to_string(value) : std::string("(not set)");
    return line;
}

}  // namespace stm32_audio::host::win
