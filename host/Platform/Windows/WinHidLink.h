// HID transport to the controller on Windows (docs/decisions/ADR-003).
//
// A worker thread looks for the controller's HID interface (isController
// in HidMatch.h) once a second, opens it, and then reads input reports.
// It only talks to the consumer through the EventQueue:
//   HidConnected     the interface was opened; sendFrame() works from now on
//   FrameReceived    one 64-byte frame from the STM32 (not validated here)
//   HidDisconnected  the interface went away; scanning starts again
//
// sendFrame() is called on the consumer thread (it is the IFrameSink of
// HostController) and writes one output report, waiting at most
// kWriteTimeoutMs.
#pragma once

#include <windows.h>

#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "EventQueue.h"
#include "HidMatch.h"
#include "IFrameSink.h"

namespace stm32_audio::host::win {

// The connected board's versions, read without opening it for reading or
// writing (works while the tray program serves it); nullopt if no board.
std::optional<BoardVersion> findBoardVersion();

class WinHidLink : public IFrameSink {
public:
    static constexpr DWORD kScanIntervalMs = 1000;
    static constexpr DWORD kWriteTimeoutMs = 1000;

    explicit WinHidLink(EventQueue& events);
    ~WinHidLink() override;

    WinHidLink(const WinHidLink&) = delete;
    WinHidLink& operator=(const WinHidLink&) = delete;

    // Start the worker thread. False if the Win32 events could not be
    // created.
    bool start();
    // Stop the worker thread and close the device. Called by the destructor.
    void stop();

    bool sendFrame(const Frame& frame) override;

    // Device path and product string of the open interface (empty when
    // not connected), for logs.
    std::string description() const;
    // The open board's versions from its USB configuration string
    // (ADR-011); empty when not connected or unknown.
    BoardVersion boardVersion() const;

private:
    void run();
    HANDLE findAndOpen(std::string& description, BoardVersion& version);
    void readUntilGone(HANDLE device);
    void closeDevice();

    EventQueue& events_;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;

    // device_ is written by the worker thread and used by sendFrame();
    // mutex_ keeps a write from racing the close.
    mutable std::mutex mutex_;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    std::string description_;
    BoardVersion boardVersion_;
};

}  // namespace stm32_audio::host::win
