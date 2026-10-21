#include "FrameRouter.h"

namespace stm32_audio::host {

void FrameRouter::onFrame(const Frame& frame)
{
    ProtoMessage msg{};
    const ProtoStatus status = Proto_Decode(frame.data(), &msg);

    if (status != PROTO_OK) {
        ++stats_.framesRejected;
        if (msg.command != PROTO_CMD_ERROR) {
            ProtoMessage error{};
            Frame out{};
            error.command = PROTO_CMD_ERROR;
            error.u.error.code = static_cast<std::uint8_t>(Proto_ErrorCodeFor(status));
            error.u.error.ref_command = msg.command;
            error.u.error.ref_sequence = msg.sequence;
            if (Proto_Encode(&error, out.data()) == PROTO_OK) {
                (void)sink_.sendFrame(out);
            }
        }
        return;
    }

    const std::uint8_t about = (msg.command == PROTO_CMD_ERROR) ? msg.u.error.ref_command : msg.command;
    if (isDashboardCommand(about)) {
        ++stats_.toDashboard;
        dash_.onMessage(msg);
    } else {
        ++stats_.toAudio;
        audio_.onMessage(msg);
    }
}

}  // namespace stm32_audio::host
