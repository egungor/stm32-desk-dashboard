// Keeps Preferences and the preference file in step (ADR-004, #59):
//
// - load() at start;
// - markChanged() when the STM32 changed something; poll() writes the
//   file at most once per kSaveIntervalMs (changes come in bursts);
// - poll() also notices, once per kCheckIntervalMs, that another program
//   (e.g. "dashboard-host alias") replaced the file, and reloads it. A
//   change from the file wins over unsaved changes from the STM32.
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
    static constexpr std::uint64_t kCheckIntervalMs = 1000;

    enum class LoadResult {
        Loaded,
        Missing,     // no file yet: defaults
        Unreadable,  // kept aside (keepUnreadable), defaults used
    };

    PreferenceStore(IPreferenceFile& file, Preferences& prefs) : file_(file), prefs_(prefs) {}

    LoadResult load();

    void markChanged() { dirty_ = true; }

    // Save or reload as due. Returns true if the preferences were reloaded
    // from the file (the caller tells the STM32).
    bool poll(std::uint64_t nowMs);

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
    std::optional<std::uint64_t> lastCheckMs_;
    unsigned writeFailures_ = 0;
};

}  // namespace stm32_audio::host
