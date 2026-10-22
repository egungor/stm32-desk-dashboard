// The "run" loop: serve the controller over USB HID until Quit (issue #74).
// Shared by the console program (dashboard-host run, lines on stdout)
// and the tray program (dashboard-tray, lines in host.log).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "EventQueue.h"
#include "HostController.h"
#include "PreferenceStore.h"
#include "SettingsModel.h"
#include "WinAudioSystem.h"
#include "WinPreferenceFile.h"

namespace stm32_audio::host::win {

// One log line, without the line end.
using LogFn = std::function<void(const std::string& line)>;

struct HostServiceHooks {
    LogFn log;                                           // required
    std::function<void(bool connected)> connection;      // optional
    std::function<void(std::uint8_t language)> language; // optional: settings record's language
    // optional: what the settings window shows of the service (ADR-011),
    // called on the service thread whenever it changes
    std::function<void(const ServiceSnapshot& snapshot)> snapshot;
};

// Load the preference file into @p store and log what happened. Returns
// false only if %APPDATA% is unknown (nothing will be saved).
bool loadPreferences(WinPreferenceFile& file, PreferenceStore& store, bool quiet, const LogFn& log);

// Core Audio events, the same in "watch" and "run".
void handleAudioEvent(HostEvent event, WinAudioSystem& audio, HostController& controller);

// Serve the controller until HostEvent::Quit arrives in @p events.
// Returns 0, or 1 if the HID thread cannot start.
int runHostService(WinAudioSystem& audio, EventQueue& events, const HostServiceHooks& hooks);

}  // namespace stm32_audio::host::win
