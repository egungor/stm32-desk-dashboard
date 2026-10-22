// Keeps Preferences and the preference file in step (ADR-004, #59):
//
// - load() at start;
// - markChanged() when the STM32 or the settings window changed
//   something; onTick() writes the file at most once per kSaveIntervalMs
//   (changes come in bursts);
// - reload() when another program (e.g. "dashboard-host alias") says it
//   replaced the file (HostEvent::SettingsChanged, #61): the file is not
//   checked otherwise. A change from the file wins over unsaved changes.
//
// Time is passed in (milliseconds) so tests can drive it. Platform
// independent; not thread-safe.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "PreferenceFile.h"
#include "Preferences.h"

namespace stm32_audio::host {

class PreferenceStore {
public:
    static constexpr std::uint64_t kSaveIntervalMs = 1000;

    enum class LoadResult {
        Loaded,
        Missing,     // no file yet: defaults
        Unreadable,  // kept aside (keepUnreadable), defaults used
    };

    PreferenceStore(IPreferenceFile& file, Preferences& prefs) : file_(file), prefs_(prefs) {}

    LoadResult load();

    void markChanged() { dirty_ = true; }

    // Save if changes wait and kSaveIntervalMs passed since the last save.
    void onTick(std::uint64_t nowMs);

    // Read the file again. Returns true if it changed and was taken (the
    // caller tells the STM32); an unreadable file is ignored.
    bool reload();

    // Save now if there are unsaved changes. Returns false if writing failed.
    bool flush();

    bool dirty() const { return dirty_; }
    unsigned writeFailures() const { return writeFailures_; }

private:
    bool save();

    IPreferenceFile& file_;
    Preferences& prefs_;
    std::optional<std::string> known_;  // file content last read or written
    bool dirty_ = false;
    std::optional<std::uint64_t> lastSaveMs_;
    unsigned writeFailures_ = 0;
};

}  // namespace stm32_audio::host
