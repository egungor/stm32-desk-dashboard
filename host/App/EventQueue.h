// Thread-safe queue that serialises events into the single-threaded
// HostController (see HostController.h).
//
// Producers (Core Audio notification threads, the HID reader thread) only
// push; the consumer thread pops and calls the controller.
//
// - DevicesChanged / DefaultChanged / VolumeChanged / MenuChanged / MediaChanged /
//   NotificationsChanged / Quit: a kind that is
//   already waiting is not queued twice, so a burst of Core Audio
//   notifications becomes one refresh.
// - FrameReceived carries a protocol frame; HidConnected / HidDisconnected
//   are never merged. Received frames are bounded: when the consumer falls
//   behind, new frames are dropped and counted.
// Order between events is kept.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include "IFrameSink.h"

namespace stm32_audio::host {

enum class HostEvent {
    DevicesChanged,   // endpoint added, removed, state or name changed
    DefaultChanged,   // default render endpoint changed
    VolumeChanged,    // volume or mute of the default endpoint changed
    FrameReceived,    // a frame arrived from the STM32
    HidConnected,     // the controller's HID interface was opened
    HidDisconnected,  // the controller's HID interface went away
    MenuChanged,      // a menu import wrote the month files
    MediaChanged,     // what plays on the PC changed (ADR-008)
    NotificationsChanged,  // the toasts in the notification centre changed (ADR-009)
    HookRequest,      // a Claude Code permission request from the pipe (ticket, text)
    HookCancelled,    // its hook went away before an answer (ticket)
    Quit,
};

struct QueuedEvent {
    HostEvent event;
    Frame frame{};           // FrameReceived only
    std::uint64_t ticket = 0;  // HookRequest, HookCancelled
    std::string text;        // HookRequest: the hook input
};

class EventQueue {
public:
    explicit EventQueue(std::size_t maxFrames = 64) : maxFrames_(maxFrames) {}

    void push(HostEvent event)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (coalesces(event)) {
                for (const QueuedEvent& pending : events_) {
                    if (pending.event == event) {
                        return;
                    }
                }
            }
            events_.push_back(QueuedEvent{event, Frame{}, 0U, {}});
        }
        cv_.notify_one();
    }

    // HookRequest / HookCancelled: never merged.
    void pushHook(HostEvent event, std::uint64_t ticket, std::string text = {})
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            events_.push_back(QueuedEvent{event, Frame{}, ticket, std::move(text)});
        }
        cv_.notify_one();
    }

    // Returns false (and counts the frame as dropped) when maxFrames
    // received frames are already waiting.
    bool pushFrame(const Frame& frame)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (framesWaiting_ >= maxFrames_) {
                ++droppedFrames_;
                return false;
            }
            events_.push_back(QueuedEvent{HostEvent::FrameReceived, frame, 0U, {}});
            ++framesWaiting_;
        }
        cv_.notify_one();
        return true;
    }

    // Wait up to @p timeout for an event; nullopt on timeout.
    template <typename Rep, typename Period>
    std::optional<QueuedEvent> pop(std::chrono::duration<Rep, Period> timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !events_.empty(); })) {
            return std::nullopt;
        }
        QueuedEvent item = events_.front();
        events_.pop_front();
        if (item.event == HostEvent::FrameReceived) {
            --framesWaiting_;
        }
        return item;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    std::size_t droppedFrames() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return droppedFrames_;
    }

private:
    static bool coalesces(HostEvent event)
    {
        return event == HostEvent::DevicesChanged || event == HostEvent::DefaultChanged ||
               event == HostEvent::VolumeChanged || event == HostEvent::MenuChanged ||
               event == HostEvent::MediaChanged || event == HostEvent::NotificationsChanged ||
               event == HostEvent::Quit;
    }

    const std::size_t maxFrames_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueuedEvent> events_;
    std::size_t framesWaiting_ = 0;
    std::size_t droppedFrames_ = 0;
};

}  // namespace stm32_audio::host
