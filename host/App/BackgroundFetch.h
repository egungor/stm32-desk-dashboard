// One HTTP GET at a time on a worker thread, so that the service loop
// (HID frames, Core Audio events) never waits for the network.
//
// IHttpClient::get() blocks; BackgroundFetch runs it on its own thread and
// keeps the result until the loop takes it with poll(). Platform
// independent (Windows: WinHttpClient).
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "HttpClient.h"

namespace stm32_audio::host {

class BackgroundFetch {
public:
    struct Result {
        std::string url;
        std::optional<std::string> body;
    };

    explicit BackgroundFetch(IHttpClient& http) : http_(http) {}
    ~BackgroundFetch();

    BackgroundFetch(const BackgroundFetch&) = delete;
    BackgroundFetch& operator=(const BackgroundFetch&) = delete;

    // Start fetching @p url. False if a fetch is still running or its
    // result has not been taken yet.
    bool start(const std::string& url);

    // The finished fetch, once; nullopt while running or idle.
    std::optional<Result> poll();

    bool busy() const;

private:
    void join();

    IHttpClient& http_;
    mutable std::mutex mutex_;
    std::thread worker_;
    bool running_ = false;
    std::optional<Result> result_;
};

}  // namespace stm32_audio::host
