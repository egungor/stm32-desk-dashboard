// Now playing on the board (ADR-008; protocol.md, "Now playing"). Turns
// the media session's snapshots into MEDIA_PART, MEDIA_STATE and the
// cover's ART_PARTs, and carries out the media controls.
//
// - Everything goes out on connect and on GET_MEDIA: the record, the
//   state, then the cover.
// - Afterwards: a new record when the texts change; MEDIA_STATE when the
//   status, the controls, the cover or the duration change, when the
//   position jumps by more than kJumpSeconds against what the board
//   counts, and every kStateIntervalMs while playing.
// - The cover goes out kArtPartsPerTick parts per onTick, so other frames
//   are not held up; a new cover stops an older one.
// - The controls go to the session; without one, to the media keys.
// - SEEK goes to the session when it can seek (PROTO_MEDIA_CAN_SEEK). The
//   board shows the new position at once, so for kSeekGraceMs the
//   session's position is taken from the seek (players update their
//   timeline late), and no MEDIA_STATE pulls the bar back.
//
// Platform independent; single threaded (the service loop). Call onTick
// often while sendingArt() is true.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "IFrameSink.h"
#include "MediaKeys.h"
#include "MediaSession.h"
#include "protocol.h"

namespace stm32_audio::host {

class MediaService {
public:
    struct Stats {
        unsigned framesSent = 0;
        unsigned sendFailures = 0;
        unsigned statesSent = 0;
        unsigned recordsSent = 0;
        unsigned coversSent = 0;  // covers whose last part went out
    };

    static constexpr std::uint64_t kStateIntervalMs = 30U * 1000U;
    static constexpr std::uint32_t kJumpSeconds = 2U;
    static constexpr unsigned kArtPartsPerTick = 8U;
    static constexpr std::uint64_t kSeekGraceMs = 3000U;

    MediaService(IFrameSink& sink, IMediaSession& session, IMediaKeys& keys);

    // The HID device was opened, or the board sent GET_MEDIA: send all.
    void onConnected(std::uint64_t nowMs);
    void onRequest(std::uint64_t nowMs) { onConnected(nowMs); }
    // The HID device went away: send nothing until the next connect.
    void onDisconnected();

    // Read the session and send what changed, then the next cover parts.
    void onTick(std::uint64_t nowMs);

    // True while cover parts wait to be sent.
    bool sendingArt() const { return connected_ && artNext_.has_value(); }

    // An ACTION: true if it was the media strip's and was carried out;
    // false otherwise (also for other kinds, for the caller to try).
    bool onAction(const ProtoAction& action);

    // A SEEK to @p seconds: true if the session took it; false without a
    // session or when it cannot seek (the caller answers ERROR).
    bool onSeek(std::uint32_t seconds, std::uint64_t nowMs);

    std::uint8_t artId() const { return artId_; }
    const Stats& stats() const { return stats_; }

private:
    // Take the session's snapshot; returns whether the record changed and
    // whether the cover changed.
    struct Changes {
        bool record = false;
        bool cover = false;
    };
    Changes take(const MediaSnapshot& snap);
    std::uint32_t positionAt(std::uint64_t nowMs) const;
    bool stateDue(std::uint64_t nowMs) const;
    void sendRecord();
    void sendState(std::uint64_t nowMs);
    void sendArtParts();
    void send(const ProtoMessage& msg);

    IFrameSink& sink_;
    IMediaSession& session_;
    IMediaKeys& keys_;
    Stats stats_;
    bool connected_ = false;

    MediaSnapshot shown_;  // the last snapshot taken
    std::string record_;
    std::uint8_t recordGeneration_ = 0;
    std::uint8_t artId_ = PROTO_NO_ART;
    std::uint8_t artCounter_ = 0;
    std::optional<std::uint16_t> artNext_;  // next cover part to send

    struct Sent {
        ProtoMediaState state{};
        std::uint64_t atMs = 0;
    };
    std::optional<Sent> sent_;  // the last MEDIA_STATE

    struct Seek {
        std::uint32_t seconds = 0;
        std::uint64_t atMs = 0;
    };
    std::optional<Seek> seek_;  // the last SEEK, within kSeekGraceMs
};

}  // namespace stm32_audio::host
