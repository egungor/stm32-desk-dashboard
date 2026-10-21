// Notification spike (#33, docs/notification-spike.md): what the tray
// program can learn about Windows notifications, tried two ways on the
// user's PC:
//
//   1. UserNotificationListener (Windows.UI.Notifications.Management,
//      C++/WinRT): access status and, if allowed, the toasts. Documented,
//      but it may need package identity, which the Inno Setup install
//      does not give.
//   2. The notification database of the current user,
//      %LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db
//      (SQLite, read-only with Windows' own winsqlite3): the last toasts
//      with time, app and texts. Undocumented.
//
// Probe only: prints what it finds, keeps and sends nothing.
#pragma once

#include <functional>
#include <string>

namespace stm32_audio::host::win {

using PrintFn = std::function<void(const std::string& line)>;

// Both ways once; the last @p count toasts of the database.
int runNotificationProbe(unsigned count, const PrintFn& print);

// Poll the database every second and print each new toast with the delay
// between its arrival time and when it was seen; Ctrl+C stops.
int runNotificationWatch(const PrintFn& print);

}  // namespace stm32_audio::host::win
