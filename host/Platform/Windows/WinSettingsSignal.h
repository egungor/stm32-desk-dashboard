// Settings changed outside the service loop (#61, ADR-011). The service
// never checks the setting files by itself: the console tools and the
// settings window write a file, then set a named event; the service loop
// gets HostEvent::SettingsChanged and reads the files again.
//
// Event: "Local\stm32-desk-dashboard-settings" (auto-reset, this user's
// session), created by the service (SettingsWatcher).
#pragma once

#include <windows.h>

#include <thread>

#include "EventQueue.h"

namespace stm32_audio::host::win {

// Tell a running service that setting files changed. False if none runs
// (then it reads them when it starts).
bool notifySettingsChanged();

// Waits for the event on its own thread and pushes SettingsChanged.
class SettingsWatcher {
public:
    explicit SettingsWatcher(EventQueue& events);
    ~SettingsWatcher();
    SettingsWatcher(const SettingsWatcher&) = delete;
    SettingsWatcher& operator=(const SettingsWatcher&) = delete;

    // False if the event or the thread could not be made.
    bool start();
    void stop();

private:
    void run();

    EventQueue& events_;
    HANDLE changed_ = nullptr;
    HANDLE stop_ = nullptr;
    std::thread thread_;
};

}  // namespace stm32_audio::host::win
