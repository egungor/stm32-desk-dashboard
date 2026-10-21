#include "BackgroundFetch.h"

namespace stm32_audio::host {

BackgroundFetch::~BackgroundFetch()
{
    join();  // a running GET ends by its own timeout
}

void BackgroundFetch::join()
{
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool BackgroundFetch::start(const std::string& url)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ || result_) {
            return false;
        }
        running_ = true;
    }
    join();  // the previous worker has finished: its result was taken
    worker_ = std::thread([this, url] {
        std::optional<std::string> body = http_.get(url);
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = Result{url, std::move(body)};
        running_ = false;
    });
    return true;
}

std::optional<BackgroundFetch::Result> BackgroundFetch::poll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<Result> out = std::move(result_);
    result_.reset();
    return out;
}

bool BackgroundFetch::busy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_ || result_.has_value();
}

}  // namespace stm32_audio::host
