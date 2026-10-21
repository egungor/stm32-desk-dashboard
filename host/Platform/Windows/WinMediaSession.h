// IMediaSession on Windows (ADR-008): the system media transport controls
// (Windows.Media.Control, C++/WinRT) read on a worker thread in the
// multithreaded apartment. Every 500 ms (at once after a command) the
// thread reads the current session: texts, status, controls, timeline and,
// for a new track, the thumbnail, cropped to a centred square and scaled
// to 120 x 120 with WIC, as RGB565. When anything changed it queues
// HostEvent::MediaChanged. Commands are carried out on the same thread.
//
// WinRT stays inside WinMediaSession.cpp.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "EventQueue.h"
#include "MediaSession.h"

namespace stm32_audio::host::win {

class WinMediaSession : public IMediaSession {
public:
    using LogFn = std::function<void(const std::string& line)>;

    WinMediaSession(EventQueue& events, LogFn log);
    ~WinMediaSession() override;

    WinMediaSession(const WinMediaSession&) = delete;
    WinMediaSession& operator=(const WinMediaSession&) = delete;

    // Start the reader thread; false if it could not be started.
    bool start();
    void stop();

    MediaSnapshot snapshot() const override;
    bool command(MediaKey key) override;

private:
    void run();
    // Store @p next; true if it differs from the last one.
    bool publish(const MediaSnapshot& next);

    EventQueue& events_;
    LogFn log_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::deque<MediaKey> commands_;
    MediaSnapshot snapshot_;
};

}  // namespace stm32_audio::host::win
