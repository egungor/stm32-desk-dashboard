#include "PictureService.h"

namespace stm32_audio::host {

PictureService::PictureService(IFrameSink& sink, IPictureSource& source) : sink_(sink), source_(source) {}

void PictureService::onRequest()
{
    connected_ = true;
    take();
    sendPicture();
}

void PictureService::onDisconnected()
{
    connected_ = false;
    next_.reset();
}

void PictureService::take()
{
    const PictureSnapshot snap = source_.snapshot();
    if (snap.version == version_) {
        return;
    }
    version_ = snap.version;
    const bool valid = snap.pixels && snap.pixels->size() == PROTO_PICTURE_BYTES;
    pixels_ = valid ? snap.pixels : nullptr;
    if (pixels_) {
        counter_ = static_cast<std::uint8_t>(counter_ == 255U ? 1U : counter_ + 1U);
        picId_ = counter_;
    } else {
        picId_ = PROTO_NO_PICTURE;
    }
    if (connected_) {
        sendPicture();
    }
}

void PictureService::onTick()
{
    take();
    if (!connected_ || !next_ || !pixels_) {
        return;
    }
    for (unsigned i = 0; i < kPartsPerTick && next_; ++i) {
        ProtoMessage msg{};
        msg.command = PROTO_CMD_PICTURE_PART;
        if (Proto_PicturePart(pixels_->data(), picId_, *next_, &msg.u.art) == 0) {
            next_.reset();
            ++stats_.sendFailures;
            return;
        }
        send(msg);
        if (*next_ + 1U >= PROTO_PICTURE_PARTS) {
            next_.reset();
            ++stats_.picturesSent;
        } else {
            next_ = static_cast<std::uint16_t>(*next_ + 1U);
        }
    }
}

void PictureService::sendPicture()
{
    ProtoMessage msg{};
    msg.command = PROTO_CMD_PICTURE;
    msg.u.pic_id = picId_;
    send(msg);
    next_.reset();
    if (pixels_) {
        next_ = 0;
    }
}

void PictureService::send(const ProtoMessage& msg)
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
