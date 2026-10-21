#include "HostController.h"

#include <utility>
#include <vector>

namespace stm32_audio::host {

namespace {

ProtoMessage makeMessage(std::uint8_t command)
{
    ProtoMessage msg{};
    msg.command = command;
    return msg;
}

}  // namespace

HostController::HostController(IAudioSystem& audio, IFrameSink& sink) : audio_(audio), sink_(sink)
{
    refreshRegistry();
}

void HostController::onConnected()
{
    forgetSent();
    handleGetDeviceList();
}

void HostController::onDisconnected()
{
    forgetSent();
}

void HostController::forgetSent()
{
    sentGeneration_.reset();
    sentDefault_.reset();
    sentVolume_.reset();
    sentMuted_.reset();
}

void HostController::onFrame(const Frame& frame)
{
    ProtoMessage msg{};
    const ProtoStatus status = Proto_Decode(frame.data(), &msg);

    if (status != PROTO_OK) {
        ++stats_.framesRejected;
        // Never answer an ERROR with an ERROR, even a damaged one.
        if (msg.command != PROTO_CMD_ERROR) {
            sendError(Proto_ErrorCodeFor(status), msg.command, msg.sequence);
        }
        return;
    }
    onMessage(msg);
}

void HostController::onMessage(const ProtoMessage& msg)
{
    switch (msg.command) {
    case PROTO_CMD_GET_DEVICE_LIST:
        handleGetDeviceList();
        break;
    case PROTO_CMD_SELECT_DEVICE:
        handleSelectDevice(msg);
        break;
    case PROTO_CMD_SET_VOLUME:
        handleSetVolume(msg);
        break;
    case PROTO_CMD_SET_MUTE:
        handleSetMute(msg);
        break;
    case PROTO_CMD_SET_ALIAS:
    case PROTO_CMD_SET_FAVORITE:
    case PROTO_CMD_MOVE_DEVICE:
        handleEdit(msg);
        break;
    case PROTO_CMD_SET_SETTINGS:
        handleSetSettings(msg);
        break;
    case PROTO_CMD_ERROR:
        ++stats_.errorsReceived;
        break;
    default:
        // Host -> STM32 commands coming the wrong way: drop.
        ++stats_.framesIgnored;
        break;
    }
}

void HostController::onDevicesChanged()
{
    refreshRegistry();
    if (!synced()) {
        return;  // the STM32 has not asked yet; GET_DEVICE_LIST sends everything
    }
    if (*sentGeneration_ != registry_.generation()) {
        sendDeviceList();
        sendDefault(true);  // protocol: a new list is followed by DEVICE_CHANGED
    }
}

void HostController::onDefaultChanged()
{
    // A default change can come with an endpoint change (e.g. a headset
    // plugged in), so refresh the list first.
    refreshRegistry();
    if (!synced()) {
        return;
    }
    if (*sentGeneration_ != registry_.generation()) {
        sendDeviceList();
        sendDefault(true);
    } else {
        sendDefault(false);
    }
    sendVolumeAndMute(false);  // volume and mute belong to the new default
}

void HostController::onVolumeChanged()
{
    if (!synced()) {
        return;
    }
    sendVolumeAndMute(false);
}

void HostController::onPreferencesChanged()
{
    refreshRegistry();
    if (!synced()) {
        return;
    }
    if (*sentGeneration_ != registry_.generation()) {
        sendDeviceList();
        sendDefault(true);
    }
    sendSettings();
}

void HostController::handleGetDeviceList()
{
    refreshRegistry();
    sendDeviceList();
    sendDefault(true);
    sendVolumeAndMute(true);
    sendSettings();
}

const DeviceRegistry::Device* HostController::requestedDevice(const ProtoMessage& msg)
{
    // Indices are only valid within the list they were sent in. Before the
    // list has been sent since the last connect, an index may come from an
    // earlier session (host program restarted while the board stayed
    // plugged in) and could name another device now; after a newer list,
    // the board's index may be stale.
    const DeviceRegistry::Device* device = nullptr;

    if (synced() && msg.u.request.generation == *sentGeneration_) {
        device = registry_.findByIndex(msg.u.request.device_index);
    }
    if (device == nullptr) {
        sendError(PROTO_ERROR_UNKNOWN_DEVICE, msg.command, msg.sequence);
    }
    return device;
}

void HostController::handleSelectDevice(const ProtoMessage& msg)
{
    const DeviceRegistry::Device* device = requestedDevice(msg);

    if (device == nullptr) {
        return;
    }
    // Volume limit on switch (ADR-004): lower the device before it becomes
    // the default, so it never plays above the limit.
    const std::optional<VolumeLimited> lowered = applyVolumeLimit(audio_, prefs_.settings(), device->endpoint);
    if (lowered && volumeLimited_) {
        volumeLimited_(*lowered);
    }
    if (!audio_.setDefaultEndpoint(device->endpoint.id)) {
        sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        return;
    }
    // Confirm now; the Windows notification that follows is deduplicated.
    sendDefault(false);
    sendVolumeAndMute(false);
}

void HostController::handleEdit(const ProtoMessage& msg)
{
    const DeviceRegistry::Device* device = requestedDevice(msg);

    if (device == nullptr) {
        return;
    }
    const std::string id = device->endpoint.id;
    bool changed = false;

    switch (msg.command) {
    case PROTO_CMD_SET_ALIAS:
        changed = prefs_.setAlias(id, std::string(msg.u.request.alias, msg.u.request.alias_len));
        break;
    case PROTO_CMD_SET_FAVORITE:
        changed = prefs_.setFavorite(id, msg.u.request.value != 0U);
        break;
    default: {  // PROTO_CMD_MOVE_DEVICE
        std::vector<std::string> shown;
        for (const DeviceRegistry::Device& d : registry_.devices()) {
            shown.push_back(d.endpoint.id);
        }
        if (static_cast<std::size_t>(msg.u.request.value) >= shown.size()) {
            sendError(PROTO_ERROR_BAD_PAYLOAD, msg.command, msg.sequence);
            return;
        }
        changed = prefs_.move(shown, id, msg.u.request.value);
        break;
    }
    }
    sendEditedList();
    if (changed && prefsChanged_) {
        prefsChanged_();
    }
}

void HostController::handleSetSettings(const ProtoMessage& msg)
{
    const bool changed = prefs_.setSettings(msg.u.settings);

    sendSettings();
    if (changed && prefsChanged_) {
        prefsChanged_();
    }
}

void HostController::sendEditedList()
{
    // Every edit is answered with a new list (new generation), even if
    // nothing changed, so the board's request completes.
    refreshRegistry();
    if (*sentGeneration_ == registry_.generation()) {
        registry_.bumpGeneration();
    }
    sendDeviceList();
    sendDefault(true);
}

void HostController::sendSettings()
{
    ProtoMessage msg = makeMessage(PROTO_CMD_SETTINGS);
    msg.u.settings = prefs_.settings();
    send(msg);
}

void HostController::handleSetVolume(const ProtoMessage& msg)
{
    if (!audio_.setVolumePercent(msg.u.volume)) {
        sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        return;
    }
    sendVolumeAndMute(false);
}

void HostController::handleSetMute(const ProtoMessage& msg)
{
    if (!audio_.setMuted(msg.u.muted != 0U)) {
        sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        return;
    }
    sendVolumeAndMute(false);
}

void HostController::refreshRegistry()
{
    (void)registry_.update(prefs_.arrange(audio_.renderEndpoints()), audio_.defaultEndpointId(), &prefs_);
}

void HostController::sendDeviceList()
{
    for (const ProtoDeviceEntry& entry : registry_.entries(audio_.defaultEndpointId())) {
        ProtoMessage msg = makeMessage(PROTO_CMD_DEVICE_LIST);
        msg.u.device = entry;
        send(msg);
    }
    for (const ProtoAliasEntry& alias : registry_.aliasEntries()) {
        ProtoMessage msg = makeMessage(PROTO_CMD_DEVICE_ALIAS);
        msg.u.alias = alias;
        send(msg);
    }
    sentGeneration_ = registry_.generation();
}

void HostController::sendDefault(bool force)
{
    const std::optional<std::uint8_t> index = registry_.indexOf(audio_.defaultEndpointId());
    const std::uint8_t value = index ? *index : static_cast<std::uint8_t>(PROTO_NO_DEVICE);

    if (force || !sentDefault_ || *sentDefault_ != value) {
        ProtoMessage msg = makeMessage(PROTO_CMD_DEVICE_CHANGED);
        msg.u.device_index = value;
        send(msg);
        sentDefault_ = value;
        if (value == PROTO_NO_DEVICE) {
            // The STM32 forgets volume and mute with "no default device";
            // send them again when a default device appears.
            sentVolume_.reset();
            sentMuted_.reset();
        }
    }
}

void HostController::sendVolumeAndMute(bool force)
{
    const std::optional<unsigned> volume = audio_.volumePercent();
    const std::optional<bool> muted = audio_.muted();

    if (volume && *volume <= PROTO_MAX_VOLUME && (force || sentVolume_ != volume)) {
        ProtoMessage msg = makeMessage(PROTO_CMD_VOLUME_CHANGED);
        msg.u.volume = static_cast<std::uint8_t>(*volume);
        send(msg);
        sentVolume_ = volume;
    }
    if (muted && (force || sentMuted_ != muted)) {
        ProtoMessage msg = makeMessage(PROTO_CMD_MUTE_CHANGED);
        msg.u.muted = *muted ? 1U : 0U;
        send(msg);
        sentMuted_ = muted;
    }
}

void HostController::sendError(ProtoErrorCode code, std::uint8_t refCommand, std::uint8_t refSequence)
{
    ProtoMessage msg = makeMessage(PROTO_CMD_ERROR);
    msg.u.error.code = static_cast<std::uint8_t>(code);
    msg.u.error.ref_command = refCommand;
    msg.u.error.ref_sequence = refSequence;
    send(msg);
}

void HostController::send(ProtoMessage msg)
{
    Frame frame{};

    msg.sequence = sequence_++;
    if (Proto_Encode(&msg, frame.data()) != PROTO_OK) {
        // Only reachable through a bug in this class; never send garbage.
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
