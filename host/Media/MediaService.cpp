#include "MediaService.h"

#include "DashController.h"  // recordLine, cutUtf8

namespace stm32_audio::host {

MediaService::MediaService(IFrameSink& sink, IMediaSession& session, IMediaKeys& keys)
    : sink_(sink), session_(session), keys_(keys)
{
}

void MediaService::onConnected(std::uint64_t nowMs)
{
    connected_ = true;
    (void)take(session_.snapshot());
    sendRecord();
    sendState(nowMs);
    artNext_.reset();
    if (artId_ != PROTO_NO_ART) {
        artNext_ = 0;
        sendArtParts();
    }
}

void MediaService::onDisconnected()
{
    connected_ = false;
    sent_.reset();
    artNext_.reset();
}

void MediaService::onTick(std::uint64_t nowMs)
{
    const Changes changes = take(session_.snapshot());
    if (!connected_) {
        return;
    }
    if (changes.record) {
        sendRecord();
    }
    if (changes.cover || stateDue(nowMs)) {
        sendState(nowMs);
    }
    if (changes.cover) {
        artNext_.reset();
        if (artId_ != PROTO_NO_ART) {
            artNext_ = 0;
        }
    }
    sendArtParts();
}

bool MediaService::onAction(const ProtoAction& action)
{
    if (action.kind != PROTO_ACTION_MEDIA) {
        return false;
    }
    const std::optional<MediaKey> key = mediaKeyFor(action.action);
    if (!key) {
        return false;
    }
    if (session_.snapshot().status != PROTO_MEDIA_STATUS_NONE && session_.command(*key)) {
        return true;
    }
    return keys_.press(*key);
}

bool MediaService::onSeek(std::uint32_t seconds, std::uint64_t nowMs)
{
    if (shown_.status == PROTO_MEDIA_STATUS_NONE || (shown_.controls & PROTO_MEDIA_CAN_SEEK) == 0U) {
        return false;
    }
    if (shown_.durationS != 0U && seconds > shown_.durationS) {
        seconds = shown_.durationS;
    }
    if (!session_.seek(seconds)) {
        return false;
    }
    seek_ = Seek{seconds, nowMs};
    shown_.positionS = seconds;
    shown_.sampledAtMs = nowMs;
    if (sent_) {
        // The board moved its bar already.
        sent_->state.position = seconds;
        sent_->atMs = nowMs;
    }
    return true;
}

MediaService::Changes MediaService::take(const MediaSnapshot& snap)
{
    Changes changes;
    std::string record =
        cutUtf8(recordLine({snap.title, snap.artist, snap.album, snap.app}), PROTO_MEDIA_MAX_BYTES);
    if (record != record_) {
        record_ = std::move(record);
        ++recordGeneration_;
        changes.record = true;
    }
    if (snap.coverVersion != shown_.coverVersion) {
        changes.cover = true;
        if (snap.cover && snap.cover->size() == PROTO_ART_BYTES) {
            artCounter_ = static_cast<std::uint8_t>((artCounter_ % 255U) + 1U);  // 1..255
            artId_ = artCounter_;
        } else {
            artId_ = PROTO_NO_ART;
        }
    }
    const bool sameTrack = !changes.record && !changes.cover;
    shown_ = snap;
    if (seek_ && sameTrack && snap.sampledAtMs < seek_->atMs + kSeekGraceMs) {
        // Count on from the seek until the player's timeline caught up.
        shown_.positionS = seek_->seconds;
        shown_.sampledAtMs = seek_->atMs;
    } else {
        seek_.reset();
    }
    return changes;
}

std::uint32_t MediaService::positionAt(std::uint64_t nowMs) const
{
    std::uint64_t pos = shown_.positionS;
    if (shown_.status == PROTO_MEDIA_STATUS_PLAYING && nowMs > shown_.sampledAtMs) {
        pos += (nowMs - shown_.sampledAtMs) / 1000U;
    }
    if (shown_.durationS != 0U && pos > shown_.durationS) {
        pos = shown_.durationS;
    }
    return static_cast<std::uint32_t>(pos);
}

bool MediaService::stateDue(std::uint64_t nowMs) const
{
    if (!sent_) {
        return true;
    }
    const ProtoMediaState& s = sent_->state;
    if (s.status != shown_.status || s.controls != shown_.controls || s.art_id != artId_ ||
        s.duration != shown_.durationS) {
        return true;
    }
    const std::uint64_t elapsedMs = nowMs > sent_->atMs ? nowMs - sent_->atMs : 0U;
    if (s.status == PROTO_MEDIA_STATUS_PLAYING && elapsedMs >= kStateIntervalMs) {
        return true;
    }
    // What the board shows now: it counts on while playing.
    std::uint64_t expected = s.position;
    if (s.status == PROTO_MEDIA_STATUS_PLAYING) {
        expected += elapsedMs / 1000U;
        if (s.duration != 0U && expected > s.duration) {
            expected = s.duration;
        }
    }
    const std::uint64_t actual = positionAt(nowMs);
    const std::uint64_t diff = actual > expected ? actual - expected : expected - actual;
    return diff > kJumpSeconds;
}

void MediaService::sendRecord()
{
    const auto length = static_cast<std::uint16_t>(record_.size());
    const std::uint8_t parts = Proto_RecordPartCount(length);
    for (std::uint8_t part = 0; part < parts; ++part) {
        ProtoMessage msg{};
        msg.command = PROTO_CMD_MEDIA_PART;
        if (Proto_RecordPart(reinterpret_cast<const std::uint8_t*>(record_.data()), length, 0U, recordGeneration_,
                             part, &msg.u.part) == 0) {
            ++stats_.sendFailures;
            return;
        }
        send(msg);
    }
    ++stats_.recordsSent;
}

void MediaService::sendState(std::uint64_t nowMs)
{
    ProtoMessage msg{};
    msg.command = PROTO_CMD_MEDIA_STATE;
    msg.u.media.status = shown_.status < PROTO_MEDIA_STATUS_COUNT ? shown_.status : PROTO_MEDIA_STATUS_NONE;
    msg.u.media.controls = static_cast<std::uint8_t>(shown_.controls & PROTO_MEDIA_CONTROLS_MASK);
    msg.u.media.art_id = artId_;
    msg.u.media.duration = shown_.durationS;
    msg.u.media.position = positionAt(nowMs);
    send(msg);
    ++stats_.statesSent;
    sent_ = Sent{msg.u.media, nowMs};
}

void MediaService::sendArtParts()
{
    if (!connected_ || !artNext_ || !shown_.cover || shown_.cover->size() != PROTO_ART_BYTES) {
        artNext_.reset();
        return;
    }
    for (unsigned n = 0; n < kArtPartsPerTick && *artNext_ < PROTO_ART_PARTS; ++n) {
        ProtoMessage msg{};
        msg.command = PROTO_CMD_ART_PART;
        if (Proto_ArtPart(shown_.cover->data(), artId_, *artNext_, &msg.u.art) == 0) {
            ++stats_.sendFailures;
            artNext_.reset();
            return;
        }
        send(msg);
        artNext_ = static_cast<std::uint16_t>(*artNext_ + 1U);
    }
    if (*artNext_ >= PROTO_ART_PARTS) {
        ++stats_.coversSent;
        artNext_.reset();
    }
}

void MediaService::send(const ProtoMessage& msg)
{
    Frame frame{};
    if (Proto_Encode(&msg, frame.data()) != PROTO_OK) {
        ++stats_.sendFailures;
        return;
    }
    if (sink_.sendFrame(frame)) {
        ++stats_.framesSent;
    } else {
        ++stats_.sendFailures;
    }
}

}  // namespace stm32_audio::host
