#include "WinSettingsSignal.h"

namespace stm32_audio::host::win {

namespace {

constexpr const wchar_t* kEventName = L"Local\\stm32-desk-dashboard-settings";

}  // namespace

bool notifySettingsChanged()
{
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kEventName);
    if (event == nullptr) {
        return false;
    }
    const bool ok = SetEvent(event) != 0;
    CloseHandle(event);
    return ok;
}

SettingsWatcher::SettingsWatcher(EventQueue& events) : events_(events) {}

SettingsWatcher::~SettingsWatcher()
{
    stop();
}

bool SettingsWatcher::start()
{
    changed_ = CreateEventW(nullptr, FALSE, FALSE, kEventName);
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (changed_ == nullptr || stop_ == nullptr) {
        return false;
    }
    thread_ = std::thread([this] { run(); });
    return true;
}

void SettingsWatcher::stop()
{
    if (thread_.joinable()) {
        SetEvent(stop_);
        thread_.join();
    }
    if (changed_ != nullptr) {
        CloseHandle(changed_);
        changed_ = nullptr;
    }
    if (stop_ != nullptr) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
}

void SettingsWatcher::run()
{
    const HANDLE handles[2] = {stop_, changed_};
    for (;;) {
        const DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0 + 1) {
            return;  // stop, or the handles are gone
        }
        events_.push(HostEvent::SettingsChanged);
    }
}

}  // namespace stm32_audio::host::win
