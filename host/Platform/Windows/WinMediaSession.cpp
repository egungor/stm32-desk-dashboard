#include "WinMediaSession.h"

#include <windows.h>
#include <unknwn.h>  // before C++/WinRT: classic COM interop (get_unknown)
#include <shcore.h>
#include <wincodec.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <memory>
#include <vector>

#include "MediaCover.h"

namespace stm32_audio::host::win {

namespace {

using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using winrt::Windows::Storage::Streams::IRandomAccessStreamReference;

constexpr auto kPollInterval = std::chrono::milliseconds(500);
constexpr auto kAfterCommand = std::chrono::milliseconds(150);
constexpr int kCoverRetries = 10;  // polls to wait for a thumbnail of a new track

std::uint32_t seconds(winrt::Windows::Foundation::TimeSpan span)
{
    const long long s = std::chrono::duration_cast<std::chrono::seconds>(span).count();
    if (s <= 0) {
        return 0U;
    }
    return s > 0xFFFFFFFFLL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(s);
}

// The thumbnail as a 120 x 120 RGB565 cover, or null.
std::shared_ptr<const std::vector<std::uint8_t>> readCover(const IRandomAccessStreamReference& ref)
{
    if (!ref) {
        return nullptr;
    }
    const auto stream = ref.OpenReadAsync().get();
    winrt::com_ptr<IStream> istream;
    winrt::check_hresult(
        CreateStreamOverRandomAccessStream(winrt::get_unknown(stream), __uuidof(IStream), istream.put_void()));

    winrt::com_ptr<IWICImagingFactory> wic;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          __uuidof(IWICImagingFactory), wic.put_void()));
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::check_hresult(
        wic->CreateDecoderFromStream(istream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put()));
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(decoder->GetFrame(0, frame.put()));
    UINT width = 0;
    UINT height = 0;
    winrt::check_hresult(frame->GetSize(&width, &height));
    const SquareCrop crop = centerSquare(width, height);
    if (crop.size == 0U) {
        return nullptr;
    }

    winrt::com_ptr<IWICBitmapClipper> clipper;
    winrt::check_hresult(wic->CreateBitmapClipper(clipper.put()));
    const WICRect rect{static_cast<INT>(crop.x), static_cast<INT>(crop.y), static_cast<INT>(crop.size),
                       static_cast<INT>(crop.size)};
    winrt::check_hresult(clipper->Initialize(frame.get(), &rect));
    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::check_hresult(wic->CreateBitmapScaler(scaler.put()));
    winrt::check_hresult(
        scaler->Initialize(clipper.get(), PROTO_ART_SIZE, PROTO_ART_SIZE, WICBitmapInterpolationModeHighQualityCubic));
    winrt::com_ptr<IWICFormatConverter> converter;
    winrt::check_hresult(wic->CreateFormatConverter(converter.put()));
    winrt::check_hresult(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                               nullptr, 0.0, WICBitmapPaletteTypeCustom));

    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(PROTO_ART_SIZE) * PROTO_ART_SIZE * 4U);
    winrt::check_hresult(
        converter->CopyPixels(nullptr, PROTO_ART_SIZE * 4U, static_cast<UINT>(bgra.size()), bgra.data()));
    auto cover = std::make_shared<std::vector<std::uint8_t>>(rgb565FromBgra(bgra.data(), bgra.size()));
    if (cover->size() != PROTO_ART_BYTES) {
        return nullptr;
    }
    return cover;
}

std::uint8_t statusOf(GlobalSystemMediaTransportControlsSessionPlaybackStatus status, std::uint8_t previous)
{
    switch (status) {
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
        return PROTO_MEDIA_STATUS_PLAYING;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
        return PROTO_MEDIA_STATUS_PAUSED;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
        // Between two tracks: keep what was shown, so the icon does not flicker.
        return previous != PROTO_MEDIA_STATUS_NONE ? previous : PROTO_MEDIA_STATUS_STOPPED;
    default:
        return PROTO_MEDIA_STATUS_STOPPED;
    }
}

bool sameExceptSample(const MediaSnapshot& a, const MediaSnapshot& b)
{
    return a.status == b.status && a.controls == b.controls && a.title == b.title && a.artist == b.artist &&
           a.album == b.album && a.app == b.app && a.positionS == b.positionS && a.durationS == b.durationS &&
           a.coverVersion == b.coverVersion;
}

}  // namespace

WinMediaSession::WinMediaSession(EventQueue& events, LogFn log) : events_(events), log_(std::move(log)) {}

WinMediaSession::~WinMediaSession()
{
    stop();
}

bool WinMediaSession::start()
{
    if (thread_.joinable()) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = false;
    }
    try {
        thread_ = std::thread([this] { run(); });
    } catch (...) {
        return false;
    }
    return true;
}

void WinMediaSession::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

MediaSnapshot WinMediaSession::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool WinMediaSession::command(MediaKey key)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshot_.status == PROTO_MEDIA_STATUS_NONE || !thread_.joinable()) {
            return false;
        }
        commands_.push_back(key);
    }
    cv_.notify_all();
    return true;
}

bool WinMediaSession::seek(std::uint32_t seconds)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshot_.status == PROTO_MEDIA_STATUS_NONE || !thread_.joinable()) {
            return false;
        }
        seek_ = seconds;
    }
    cv_.notify_all();
    return true;
}

bool WinMediaSession::publish(const MediaSnapshot& next)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed = !sameExceptSample(snapshot_, next);
    snapshot_ = next;
    return changed;
}

void WinMediaSession::run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    try {
        manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (...) {
        log_("WARN: the Windows media session is not available; the media strip uses media keys.");
    }

    MediaSnapshot last;
    std::string trackKey;
    std::uint32_t coverVersion = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> cover;
    int coverRetries = 0;
    bool reportedError = false;
    auto wait = kPollInterval;

    for (;;) {
        std::deque<MediaKey> commands;
        std::optional<std::uint32_t> seekTo;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, wait, [this] { return stop_ || !commands_.empty() || seek_.has_value(); });
            if (stop_) {
                break;
            }
            commands.swap(commands_);
            seekTo = seek_;
            seek_.reset();
        }
        wait = commands.empty() && !seekTo ? kPollInterval : kAfterCommand;

        MediaSnapshot next;
        try {
            const GlobalSystemMediaTransportControlsSession session =
                manager ? manager.GetCurrentSession() : GlobalSystemMediaTransportControlsSession{nullptr};
            if (session) {
                for (MediaKey key : commands) {
                    switch (key) {
                    case MediaKey::Previous:
                        (void)session.TrySkipPreviousAsync().get();
                        break;
                    case MediaKey::PlayPause:
                        (void)session.TryTogglePlayPauseAsync().get();
                        break;
                    case MediaKey::Next:
                        (void)session.TrySkipNextAsync().get();
                        break;
                    }
                }

                if (seekTo) {
                    // Ticks of 100 ns from the timeline's start.
                    const auto start = session.GetTimelineProperties().StartTime();
                    const std::int64_t ticks = start.count() + static_cast<std::int64_t>(*seekTo) * 10000000LL;
                    if (!session.TryChangePlaybackPositionAsync(ticks).get()) {
                        log_("WARN: media session: the player refused the seek");
                    }
                }

                const auto info = session.GetPlaybackInfo();
                next.status = statusOf(info.PlaybackStatus(), last.status);
                const auto controls = info.Controls();
                if (controls.IsPreviousEnabled()) {
                    next.controls = static_cast<std::uint8_t>(next.controls | PROTO_MEDIA_CAN_PREVIOUS);
                }
                if (controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() || controls.IsPauseEnabled()) {
                    next.controls = static_cast<std::uint8_t>(next.controls | PROTO_MEDIA_CAN_PLAY_PAUSE);
                }
                if (controls.IsNextEnabled()) {
                    next.controls = static_cast<std::uint8_t>(next.controls | PROTO_MEDIA_CAN_NEXT);
                }
                if (controls.IsPlaybackPositionEnabled()) {
                    next.controls = static_cast<std::uint8_t>(next.controls | PROTO_MEDIA_CAN_SEEK);
                }

                const auto props = session.TryGetMediaPropertiesAsync().get();
                next.title = winrt::to_string(props.Title());
                next.artist = winrt::to_string(props.Artist());
                next.album = winrt::to_string(props.AlbumTitle());
                next.app = mediaAppName(winrt::to_string(session.SourceAppUserModelId()));

                const auto timeline = session.GetTimelineProperties();
                auto position = timeline.Position() - timeline.StartTime();
                if (next.status == PROTO_MEDIA_STATUS_PLAYING) {
                    // The app updates the timeline now and then; count on from it.
                    const auto age = winrt::clock::now() - timeline.LastUpdatedTime();
                    if (age > winrt::Windows::Foundation::TimeSpan::zero() && age < std::chrono::hours(1)) {
                        position += age;
                    }
                }
                next.durationS = seconds(timeline.EndTime() - timeline.StartTime());
                next.positionS = seconds(position);
                if (next.durationS != 0U && next.positionS > next.durationS) {
                    next.positionS = next.durationS;
                }

                const std::string key = next.title + '\x1F' + next.artist + '\x1F' + next.album + '\x1F' + next.app;
                if (key != trackKey) {
                    trackKey = key;
                    cover.reset();
                    ++coverVersion;
                    coverRetries = kCoverRetries;
                }
                if (!cover && coverRetries > 0) {
                    // Players often publish the thumbnail a moment after the title.
                    --coverRetries;
                    try {
                        cover = readCover(props.Thumbnail());
                    } catch (...) {
                        cover.reset();
                    }
                    if (cover) {
                        ++coverVersion;
                    }
                }
                next.cover = cover;
                next.coverVersion = coverVersion;
            } else {
                if (!trackKey.empty() || cover) {
                    trackKey.clear();
                    cover.reset();
                    ++coverVersion;
                }
                next.coverVersion = coverVersion;
            }
            reportedError = false;
        } catch (const winrt::hresult_error& e) {
            if (!reportedError) {
                log_("WARN: media session: " + winrt::to_string(e.message()));
                reportedError = true;
            }
            next = last;  // keep what was shown
        } catch (...) {
            next = last;
        }
        next.sampledAtMs = GetTickCount64();
        last = next;
        if (publish(next)) {
            events_.push(HostEvent::MediaChanged);
        }
    }

    winrt::uninit_apartment();
}

}  // namespace stm32_audio::host::win
