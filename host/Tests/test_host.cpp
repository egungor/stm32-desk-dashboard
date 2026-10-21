// Unit tests for the host device model and protocol controller.
// No test framework; returns non-zero if a check fails.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "AudioMapping.h"
#include "DeviceRegistry.h"
#include "EventQueue.h"
#include "FrameText.h"
#include "HidMatch.h"
#include "HostController.h"
#include "PreferenceFile.h"
#include "PreferenceStore.h"
#include "Preferences.h"
#include "Startup.h"
#include "VolumeLimit.h"

using namespace stm32_audio::host;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

// ---------------------------------------------------------------- fakes

class FakeAudio : public IAudioSystem {
public:
    std::vector<AudioEndpoint> endpoints;
    std::string defaultId;
    std::optional<unsigned> volume = 50;
    std::optional<bool> mute = false;
    bool failSetDefault = false;
    bool failSetVolume = false;
    std::vector<std::string> setDefaultCalls;

    std::vector<AudioEndpoint> renderEndpoints() override { return endpoints; }
    std::string defaultEndpointId() override { return defaultId; }
    bool setDefaultEndpoint(const std::string& id) override
    {
        setDefaultCalls.push_back(id);
        if (failSetDefault) {
            return false;
        }
        defaultId = id;
        return true;
    }
    std::optional<unsigned> volumePercent() override { return volume; }
    bool setVolumePercent(unsigned percent) override
    {
        if (failSetVolume || !volume) {
            return false;
        }
        volume = percent;
        return true;
    }
    std::optional<bool> muted() override { return mute; }
    bool setMuted(bool m) override
    {
        if (!mute) {
            return false;
        }
        mute = m;
        return true;
    }

    // Per-device volume; the default device's is `volume`.
    std::map<std::string, unsigned> endpointVolumes;
    std::vector<std::string> endpointVolumeDefaults;  // the default at each setEndpointVolumePercent
    std::optional<unsigned> endpointVolumePercent(const std::string& id) override
    {
        if (id == defaultId) {
            return volume;
        }
        const auto it = endpointVolumes.find(id);
        return it == endpointVolumes.end() ? std::nullopt : std::optional<unsigned>(it->second);
    }
    bool setEndpointVolumePercent(const std::string& id, unsigned percent) override
    {
        endpointVolumeDefaults.push_back(defaultId);
        if (id == defaultId) {
            return setVolumePercent(percent);
        }
        if (endpointVolumes.count(id) == 0U) {
            return false;
        }
        endpointVolumes[id] = percent;
        return true;
    }
};

// In-memory preference file.
class FakeFile : public IPreferenceFile {
public:
    std::optional<std::string> content;
    std::optional<std::string> kept;
    bool failWrite = false;
    int writes = 0;

    std::optional<std::string> read() override { return content; }
    bool write(const std::string& c) override
    {
        if (failWrite) {
            return false;
        }
        content = c;
        ++writes;
        return true;
    }
    void keepUnreadable(const std::string& c) override { kept = c; }
    std::string location() const override { return "fake"; }
};

class FakeSink : public IFrameSink {
public:
    std::deque<ProtoMessage> sent;
    bool fail = false;

    bool sendFrame(const Frame& frame) override
    {
        if (fail) {
            return false;
        }
        ProtoMessage msg{};
        // Every frame the controller sends must be a valid protocol frame.
        if (Proto_Decode(frame.data(), &msg) != PROTO_OK) {
            std::printf("FAIL controller sent an invalid frame\n");
            ++g_failures;
        }
        sent.push_back(msg);
        return true;
    }

    ProtoMessage pop()
    {
        ProtoMessage m{};
        if (!sent.empty()) {
            m = sent.front();
            sent.pop_front();
        }
        return m;
    }
};

AudioEndpoint ep(const std::string& id, const std::string& name, FormFactor ff = FormFactor::Speakers)
{
    return AudioEndpoint{id, name, ff};
}

Frame frameFor(std::uint8_t command, std::uint8_t sequence, std::uint8_t value = 0)
{
    ProtoMessage msg{};
    Frame frame{};
    msg.command = command;
    msg.sequence = sequence;
    msg.u.volume = value;  // one-byte payloads share the union's first byte
    (void)Proto_Encode(&msg, frame.data());
    return frame;
}

// SELECT_DEVICE, SET_FAVORITE, MOVE_DEVICE: a request about one device of
// the list with @p generation.
Frame requestFrame(std::uint8_t command, std::uint8_t sequence, std::uint8_t generation, std::uint8_t index,
                   std::uint8_t value = 0)
{
    ProtoMessage msg{};
    Frame frame{};
    msg.command = command;
    msg.sequence = sequence;
    msg.u.request.generation = generation;
    msg.u.request.device_index = index;
    msg.u.request.value = value;
    (void)Proto_Encode(&msg, frame.data());
    return frame;
}

Frame aliasFrame(std::uint8_t sequence, std::uint8_t generation, std::uint8_t index, const std::string& alias)
{
    ProtoMessage msg{};
    Frame frame{};
    msg.command = PROTO_CMD_SET_ALIAS;
    msg.sequence = sequence;
    msg.u.request.generation = generation;
    msg.u.request.device_index = index;
    msg.u.request.alias_len = static_cast<std::uint8_t>(alias.size());
    std::memcpy(msg.u.request.alias, alias.data(), alias.size());
    (void)Proto_Encode(&msg, frame.data());
    return frame;
}

Frame settingsFrame(std::uint8_t sequence, const ProtoSettings& settings)
{
    ProtoMessage msg{};
    Frame frame{};
    msg.command = PROTO_CMD_SET_SETTINGS;
    msg.sequence = sequence;
    msg.u.settings = settings;
    (void)Proto_Encode(&msg, frame.data());
    return frame;
}

// ----------------------------------------------------- registry tests

void testTruncateUtf8()
{
    CHECK(truncateUtf8("abc", 48) == "abc");
    CHECK(truncateUtf8("abcdef", 4) == "abcd");
    // "ö" is two bytes: cutting after 3 bytes must not split it.
    CHECK(truncateUtf8("ab\xC3\xB6x", 3) == "ab");
    CHECK(truncateUtf8("ab\xC3\xB6x", 4) == "ab\xC3\xB6");
    // Four-byte sequence.
    CHECK(truncateUtf8("a\xF0\x9F\x8E\xA7", 3) == "a");
    std::string withNul("a\0b", 3);
    CHECK(truncateUtf8(withNul, 48) == "ab");
}

void testIcons()
{
    CHECK(iconFor(FormFactor::Speakers) == PROTO_ICON_SPEAKERS);
    CHECK(iconFor(FormFactor::Headphones) == PROTO_ICON_HEADPHONES);
    CHECK(iconFor(FormFactor::Headset) == PROTO_ICON_HEADSET);
    CHECK(iconFor(FormFactor::Display) == PROTO_ICON_DISPLAY);
    CHECK(iconFor(FormFactor::DigitalOutput) == PROTO_ICON_DIGITAL);
    CHECK(iconFor(FormFactor::Unknown) == PROTO_ICON_GENERIC);
}

void testRegistryIndices()
{
    DeviceRegistry reg;
    CHECK(reg.generation() == 0);

    CHECK(reg.update({ep("A", "Speakers"), ep("B", "Headphones")}, "B"));
    CHECK(reg.generation() == 1);
    const std::uint8_t a = *reg.indexOf("A");
    const std::uint8_t b = *reg.indexOf("B");
    CHECK(a != b);

    // Same content: no new generation.
    CHECK(!reg.update({ep("A", "Speakers"), ep("B", "Headphones")}, "B"));
    CHECK(reg.generation() == 1);

    // B removed, C added: A keeps its index, C does not get B's index.
    CHECK(reg.update({ep("A", "Speakers"), ep("C", "Monitor")}, "A"));
    CHECK(reg.generation() == 2);
    CHECK(*reg.indexOf("A") == a);
    CHECK(!reg.indexOf("B"));
    CHECK(*reg.indexOf("C") != b);
    CHECK(reg.findByIndex(b) == nullptr);

    // Rename changes the content.
    CHECK(reg.update({ep("A", "Speakers 2"), ep("C", "Monitor")}, "A"));
    CHECK(reg.generation() == 3);
    // Order change changes the content.
    CHECK(reg.update({ep("C", "Monitor"), ep("A", "Speakers 2")}, "A"));
    CHECK(reg.devices().front().endpoint.id == "C");
}

void testRegistryIndexWraps()
{
    DeviceRegistry reg;
    // Churn through more than 255 endpoints: indices stay in 0..254 and
    // never collide with an endpoint still present.
    for (int i = 0; i < 600; ++i) {
        CHECK(reg.update({ep("keep", "Keep"), ep("tmp" + std::to_string(i), "Tmp")}, "keep"));
        const std::uint8_t keep = *reg.indexOf("keep");
        const std::uint8_t tmp = *reg.indexOf("tmp" + std::to_string(i));
        CHECK(keep != tmp);
        CHECK(tmp != PROTO_NO_DEVICE);
    }
}

void testRegistryLimit()
{
    DeviceRegistry reg;
    std::vector<AudioEndpoint> many;
    for (unsigned i = 0; i < PROTO_MAX_DEVICES + 5U; ++i) {
        many.push_back(ep("id" + std::to_string(i), "Device " + std::to_string(i)));
    }
    const std::string lastId = many.back().id;

    reg.update(many, lastId);
    CHECK(reg.devices().size() == PROTO_MAX_DEVICES);
    CHECK(reg.indexOf(lastId).has_value());  // the default is kept
    CHECK(reg.devices().back().endpoint.id == lastId);

    reg.update(many, "id0");
    CHECK(reg.indexOf("id0").has_value());
    CHECK(!reg.indexOf(lastId).has_value());
}

void testRegistryEntries()
{
    DeviceRegistry reg;
    std::vector<ProtoDeviceEntry> e = reg.entries("");
    CHECK(e.size() == 1);
    CHECK(e[0].total == 0 && e[0].device_index == PROTO_NO_DEVICE && e[0].name_len == 0);

    const std::string longName(60, 'x');
    reg.update({ep("A", "Hoparl\xC3\xB6rler", FormFactor::Speakers),
                ep("B", longName, FormFactor::Headphones)},
               "B");
    e = reg.entries("B");
    CHECK(e.size() == 2);
    CHECK(e[0].total == 2 && e[0].position == 0 && e[1].position == 1);
    CHECK(e[0].generation == reg.generation());
    CHECK(e[0].flags == 0 && e[1].flags == PROTO_DEVICE_FLAG_DEFAULT);
    CHECK(e[0].icon == PROTO_ICON_SPEAKERS && e[1].icon == PROTO_ICON_HEADPHONES);
    CHECK(std::strcmp(e[0].name, "Hoparl\xC3\xB6rler") == 0);
    CHECK(e[1].name_len == PROTO_MAX_NAME_BYTES);
}

// --------------------------------------------------- controller tests

struct Rig {
    FakeAudio audio;
    FakeSink sink;
    HostController controller{audio, sink};

    Rig()
    {
        audio.endpoints = {ep("spk", "Speakers"), ep("hp", "Headphones", FormFactor::Headphones),
                           ep("tv", "TV", FormFactor::Display)};
        audio.defaultId = "hp";
        controller.onDevicesChanged();  // before GET_DEVICE_LIST: nothing sent
    }

    std::uint8_t index(const std::string& id) { return *controller.registry().indexOf(id); }
    std::uint8_t gen() const { return controller.registry().generation(); }
};

void testNothingBeforeFirstRequest()
{
    Rig r;
    CHECK(r.sink.sent.empty());
    r.audio.volume = 70;
    r.controller.onVolumeChanged();
    r.controller.onDefaultChanged();
    CHECK(r.sink.sent.empty());
}

void testGetDeviceList()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));

    CHECK(r.sink.sent.size() == 3 + 4);
    for (unsigned i = 0; i < 3; ++i) {
        const ProtoMessage m = r.sink.pop();
        CHECK(m.command == PROTO_CMD_DEVICE_LIST);
        CHECK(m.u.device.total == 3 && m.u.device.position == i);
    }
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("hp"));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 50);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_MUTE_CHANGED && m.u.muted == 0);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_SETTINGS);
    ProtoSettings defaults{};
    Proto_DefaultSettings(&defaults);
    CHECK(std::memcmp(&m.u.settings, &defaults, sizeof(defaults)) == 0);

    // A second request is answered in full again.
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 2));
    CHECK(r.sink.sent.size() == 7);
}

void testSequenceNumbers()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    std::uint8_t expected = 0;
    while (!r.sink.sent.empty()) {
        CHECK(r.sink.pop().sequence == expected);
        ++expected;
    }
}

void testEmptyList()
{
    Rig r;
    r.audio.endpoints.clear();
    r.audio.defaultId.clear();
    r.audio.volume.reset();
    r.audio.mute.reset();
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    CHECK(r.sink.sent.size() == 3);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.total == 0);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == PROTO_NO_DEVICE);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_SETTINGS);
}

void testSelectDevice()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 5, r.gen(), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.size() == 1 && r.audio.setDefaultCalls[0] == "tv");
    CHECK(r.sink.sent.size() == 1);
    const ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("tv"));

    // The Windows notification that follows does not repeat it.
    r.controller.onDefaultChanged();
    CHECK(r.sink.sent.empty());
}

// Indices belong to a session: a SELECT_DEVICE before the STM32 has asked
// for the list since the last connect may carry an index from an earlier
// session (host restarted, board kept plugged in) and is refused.
// Indices belong to a session: a SELECT_DEVICE before the list has been
// sent since the last connect may carry an index from an earlier session
// and is refused.
void testSelectBeforeListSent()
{
    Rig r;
    r.controller.onDevicesChanged();  // the registry is filled, nothing sent
    CHECK(r.sink.sent.empty());

    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 4, r.gen(), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.empty());
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_UNKNOWN_DEVICE);
    CHECK(m.u.error.ref_command == PROTO_CMD_SELECT_DEVICE && m.u.error.ref_sequence == 4);

    // Same after a disconnect: the old session's list no longer counts.
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 5));
    r.sink.sent.clear();
    r.controller.onDisconnected();
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 6, r.gen(), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.empty());
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_UNKNOWN_DEVICE);

    // Once the list has been sent (requested or on connect), it works.
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 7));
    r.sink.sent.clear();
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 8, r.gen(), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.size() == 1 && r.audio.setDefaultCalls[0] == "tv");
}

void testSelectErrors()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 9, r.gen(), 200));  // not in the list
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_UNKNOWN_DEVICE);
    CHECK(m.u.error.ref_command == PROTO_CMD_SELECT_DEVICE && m.u.error.ref_sequence == 9);
    CHECK(r.audio.setDefaultCalls.empty());

    r.audio.failSetDefault = true;
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 10, r.gen(), r.index("spk")));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_OPERATION_FAILED);
    CHECK(m.u.error.ref_sequence == 10);
    CHECK(r.sink.sent.empty());
}

void testVolumeAndMute()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    r.controller.onFrame(frameFor(PROTO_CMD_SET_VOLUME, 3, 80));
    CHECK(r.audio.volume == 80U);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 80);
    CHECK(r.sink.sent.empty());

    r.controller.onFrame(frameFor(PROTO_CMD_SET_MUTE, 4, 1));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_MUTE_CHANGED && m.u.muted == 1);

    // Windows reports what was already sent: nothing new.
    r.controller.onVolumeChanged();
    CHECK(r.sink.sent.empty());

    // Changed from the Windows side.
    r.audio.volume = 20;
    r.controller.onVolumeChanged();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 20);
    CHECK(r.sink.sent.empty());

    r.audio.failSetVolume = true;
    r.controller.onFrame(frameFor(PROTO_CMD_SET_VOLUME, 6, 30));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_OPERATION_FAILED);
}

// "No default device" makes the STM32 forget volume and mute; when a
// default device appears again they are sent even if unchanged.
void testVolumeResentAfterNoDefault()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    r.audio.defaultId.clear();
    r.audio.volume.reset();
    r.audio.mute.reset();
    r.controller.onDefaultChanged();
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == PROTO_NO_DEVICE);
    CHECK(r.sink.sent.empty());

    r.audio.defaultId = "hp";
    r.audio.volume = 50;  // same values as before
    r.audio.mute = false;
    r.controller.onDefaultChanged();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("hp"));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 50);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_MUTE_CHANGED && m.u.muted == 0);
    CHECK(r.sink.sent.empty());
}

void testWindowsChanges()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();
    const std::uint8_t generation = r.controller.registry().generation();

    // No change: nothing sent.
    r.controller.onDevicesChanged();
    CHECK(r.sink.sent.empty());

    // Device removed: new list with a new generation, then DEVICE_CHANGED.
    r.audio.endpoints.pop_back();
    r.controller.onDevicesChanged();
    CHECK(r.sink.sent.size() == 3);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.total == 2);
    CHECK(m.u.device.generation != generation);
    r.sink.pop();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("hp"));

    // Default changed in Windows.
    r.audio.defaultId = "spk";
    r.audio.volume = 30;
    r.controller.onDefaultChanged();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("spk"));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 30);
    CHECK(r.sink.sent.empty());

    // New default that is also a new device: list first.
    r.audio.endpoints.push_back(ep("usb", "USB DAC", FormFactor::DigitalOutput));
    r.audio.defaultId = "usb";
    r.controller.onDefaultChanged();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.total == 3);
    r.sink.pop();
    r.sink.pop();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("usb"));
}

void testReconnect()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();
    r.controller.onDisconnected();

    r.audio.volume = 90;
    r.controller.onVolumeChanged();  // nothing while disconnected
    r.controller.onDevicesChanged();
    CHECK(r.sink.sent.empty());
}

// The host opens the device: the full answer to GET_DEVICE_LIST goes out
// without a request, so a board that kept the previous session's list
// (host restarted, board stayed plugged in) is brought up to date.
void testConnectSendsList()
{
    Rig r;
    r.controller.onConnected();
    CHECK(r.sink.sent.size() == 3 + 4);
    for (unsigned i = 0; i < 3; ++i) {
        const ProtoMessage m = r.sink.pop();
        CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.total == 3 && m.u.device.position == i);
    }
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED && m.u.device_index == r.index("hp"));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 50);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_MUTE_CHANGED);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_SETTINGS);

    // Synced: changes are sent, and a selection is accepted.
    r.audio.volume = 70;
    r.controller.onVolumeChanged();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_VOLUME_CHANGED && m.u.volume == 70);
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 3, r.gen(), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.size() == 1 && r.audio.setDefaultCalls[0] == "tv");

    // A GET_DEVICE_LIST that crossed the pushed list is answered in full
    // again (the board may also ask right after USB enumeration).
    r.sink.sent.clear();
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 4));
    CHECK(r.sink.sent.size() == 7);

    // Every connect sends the list again.
    r.sink.sent.clear();
    r.controller.onDisconnected();
    r.controller.onConnected();
    CHECK(r.sink.sent.size() == 7);
}

void testBadFrames()
{
    Rig r;
    Frame frame = frameFor(PROTO_CMD_SET_VOLUME, 7, 10);
    frame[10] ^= 0x01U;  // corrupt the payload area: CRC fails
    r.controller.onFrame(frame);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_BAD_FRAME);
    CHECK(m.u.error.ref_command == PROTO_CMD_SET_VOLUME && m.u.error.ref_sequence == 7);

    // Unsupported version (a version 1 board).
    frame = frameFor(PROTO_CMD_GET_DEVICE_LIST, 8);
    frame[1] = 1;
    const std::uint16_t crc = Proto_Crc16(frame.data(), PROTO_CRC_OFFSET);
    frame[PROTO_CRC_OFFSET] = static_cast<std::uint8_t>(crc & 0xFFU);
    frame[PROTO_CRC_OFFSET + 1] = static_cast<std::uint8_t>(crc >> 8);
    r.controller.onFrame(frame);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_UNSUPPORTED_VERSION);

    // A damaged ERROR frame is not answered.
    ProtoMessage err{};
    err.command = PROTO_CMD_ERROR;
    err.u.error.code = PROTO_ERROR_BUSY;
    (void)Proto_Encode(&err, frame.data());
    frame[20] ^= 0x80U;
    r.controller.onFrame(frame);
    CHECK(r.sink.sent.empty());
    CHECK(r.controller.stats().framesRejected == 3);

    // A valid ERROR frame is counted, not answered.
    (void)Proto_Encode(&err, frame.data());
    r.controller.onFrame(frame);
    CHECK(r.sink.sent.empty());
    CHECK(r.controller.stats().errorsReceived == 1);

    // Wrong direction (host -> STM32 command) is dropped.
    r.controller.onFrame(frameFor(PROTO_CMD_VOLUME_CHANGED, 1, 10));
    CHECK(r.sink.sent.empty());
    CHECK(r.controller.stats().framesIgnored == 1);
}

void testSendFailure()
{
    Rig r;
    r.sink.fail = true;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    CHECK(r.controller.stats().sendFailures == 7);
    CHECK(r.controller.stats().framesSent == 0);
}


// ------------------------------------------------ preferences (v2)

void testPreferencesArrangeAndMove()
{
    Preferences p;
    const std::vector<AudioEndpoint> windows = {ep("a", "A"), ep("b", "B"), ep("c", "C")};

    // First sight: Windows order, remembered.
    std::vector<AudioEndpoint> shown = p.arrange(windows);
    CHECK(shown.size() == 3 && shown[0].id == "a" && shown[2].id == "c");
    CHECK(p.order() == std::vector<std::string>({"a", "b", "c"}));

    // Move "c" to the front.
    CHECK(p.move({"a", "b", "c"}, "c", 0));
    shown = p.arrange(windows);
    CHECK(shown[0].id == "c" && shown[1].id == "a" && shown[2].id == "b");
    CHECK(!p.move({"c", "a", "b"}, "c", 0));  // already there
    CHECK(!p.move({"c", "a", "b"}, "x", 0));  // not shown
    CHECK(!p.move({"c", "a", "b"}, "a", 3));  // out of range

    // "a" unplugged; "b" moved before "c"; "a" returns to its own slot.
    shown = p.arrange({ep("b", "B"), ep("c", "C")});
    CHECK(shown.size() == 2 && shown[0].id == "c");
    CHECK(p.move({"c", "b"}, "b", 0));
    CHECK(p.order() == std::vector<std::string>({"b", "a", "c"}));
    shown = p.arrange(windows);
    CHECK(shown[0].id == "b" && shown[1].id == "a" && shown[2].id == "c");

    // A new device is appended.
    shown = p.arrange({ep("d", "D"), ep("a", "A"), ep("b", "B"), ep("c", "C")});
    CHECK(shown.back().id == "d");
}

void testPreferencesAliasFavoriteSettings()
{
    Preferences p;
    CHECK(p.device("a") == nullptr);
    CHECK(!p.setAlias("a", ""));       // nothing to remove
    CHECK(p.setAlias("a", "Masa\xC3\xBCst\xC3\xBC"));
    CHECK(!p.setAlias("a", "Masa\xC3\xBCst\xC3\xBC"));
    CHECK(p.device("a")->alias == "Masa\xC3\xBCst\xC3\xBC");
    // Longer than 32 bytes: cut at a UTF-8 boundary.
    CHECK(p.setAlias("b", std::string(31, 'x') + "\xC3\xB6"));
    CHECK(p.device("b")->alias == std::string(31, 'x'));
    CHECK(p.setFavorite("a", true));
    CHECK(!p.setFavorite("a", true));
    CHECK(p.device("a")->favorite && p.device("a")->alias.size() == 10);  // "ü" is two bytes
    CHECK(p.setAlias("a", ""));
    CHECK(p.device("a")->alias.empty() && p.device("a")->favorite);

    ProtoSettings s = p.settings();
    CHECK(s.volume_limit == PROTO_DEFAULT_VOLUME_LIMIT);
    CHECK(!p.setSettings(s));  // unchanged
    s.theme = 1;
    CHECK(p.setSettings(s));
    CHECK(p.settings().theme == 1);
    s.accent = PROTO_ACCENT_COUNT;  // out of range: ignored
    CHECK(!p.setSettings(s));
    CHECK(p.settings().accent == 0);
}

void testRegistryAliasesAndFavorites()
{
    DeviceRegistry reg;
    Preferences p;
    (void)p.setAlias("B", "Kulakl\xC4\xB1k");
    (void)p.setFavorite("A", true);

    reg.update({ep("A", "Speakers"), ep("B", "Headphones")}, "B", &p);
    std::vector<ProtoDeviceEntry> e = reg.entries("B");
    CHECK(e[0].flags == PROTO_DEVICE_FLAG_FAVORITE);
    CHECK(e[1].flags == (PROTO_DEVICE_FLAG_DEFAULT | PROTO_DEVICE_FLAG_ALIAS));
    CHECK(std::strcmp(e[1].name, "Headphones") == 0);  // the Windows name stays
    std::vector<ProtoAliasEntry> a = reg.aliasEntries();
    CHECK(a.size() == 1 && a[0].position == 1 && a[0].generation == reg.generation());
    CHECK(std::strcmp(a[0].alias, "Kulakl\xC4\xB1k") == 0);

    // An alias change is a content change: new generation.
    const std::uint8_t gen = reg.generation();
    CHECK(!reg.update({ep("A", "Speakers"), ep("B", "Headphones")}, "B", &p));
    (void)p.setAlias("B", "HP");
    CHECK(reg.update({ep("A", "Speakers"), ep("B", "Headphones")}, "B", &p));
    CHECK(reg.generation() != gen);
    reg.bumpGeneration();
    CHECK(reg.generation() == static_cast<std::uint8_t>(gen + 2U));
}

void testSelectStaleGeneration()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    // An index from an older list is refused, the current one accepted.
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 2, static_cast<std::uint8_t>(r.gen() - 1U), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.empty());
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_UNKNOWN_DEVICE);
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 3, r.gen(), r.index("tv")));
    CHECK(r.audio.setDefaultCalls.size() == 1);
}

void testEditsAnsweredWithNewList()
{
    Rig r;
    int saved = 0;
    r.controller.setPreferencesChangedHandler([&saved] { ++saved; });
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    // Alias for "tv": new list, the alias frame, DEVICE_CHANGED.
    std::uint8_t gen = r.gen();
    r.controller.onFrame(aliasFrame(2, gen, r.index("tv"), "Televizyon"));
    CHECK(saved == 1);
    CHECK(r.sink.sent.size() == 3 + 1 + 1);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.generation != gen);
    r.sink.pop();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.flags == PROTO_DEVICE_FLAG_ALIAS);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_ALIAS && m.u.alias.position == 2);
    CHECK(std::strcmp(m.u.alias.alias, "Televizyon") == 0);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_CHANGED);

    // The old generation is now stale.
    r.controller.onFrame(requestFrame(PROTO_CMD_SET_FAVORITE, 3, gen, r.index("tv"), 1));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_UNKNOWN_DEVICE);
    CHECK(saved == 1);

    // Favorite, then move "tv" to the front.
    r.controller.onFrame(requestFrame(PROTO_CMD_SET_FAVORITE, 4, r.gen(), r.index("tv"), 1));
    CHECK(saved == 2);
    r.sink.sent.clear();
    r.controller.onFrame(requestFrame(PROTO_CMD_MOVE_DEVICE, 5, r.gen(), r.index("tv"), 0));
    CHECK(saved == 3);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.position == 0);
    CHECK(m.u.device.device_index == r.index("tv"));
    CHECK(m.u.device.flags == (PROTO_DEVICE_FLAG_FAVORITE | PROTO_DEVICE_FLAG_ALIAS));
    CHECK(r.controller.registry().devices()[0].endpoint.id == "tv");

    // An edit that changes nothing still gets a new list, but is not saved.
    r.sink.sent.clear();
    gen = r.gen();
    r.controller.onFrame(requestFrame(PROTO_CMD_SET_FAVORITE, 6, gen, r.index("tv"), 1));
    CHECK(saved == 3);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.generation != gen);

    // A position beyond the list.
    r.sink.sent.clear();
    r.controller.onFrame(requestFrame(PROTO_CMD_MOVE_DEVICE, 7, r.gen(), r.index("tv"), 3));
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_BAD_PAYLOAD);
    CHECK(r.sink.sent.empty());

    // The order and alias survive a reconnect (same Preferences).
    r.controller.onDisconnected();
    r.sink.sent.clear();
    r.controller.onConnected();
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_LIST && m.u.device.device_index == r.index("tv"));
}

void testSettingsStoredAndEchoed()
{
    Rig r;
    int saved = 0;
    r.controller.setPreferencesChangedHandler([&saved] { ++saved; });
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    ProtoSettings s = r.controller.preferences().settings();
    s.layout = 1;
    s.flipped = 1;
    s.volume_limit_on = 1;
    s.volume_limit = 30;
    r.controller.onFrame(settingsFrame(2, s));
    CHECK(saved == 1);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_SETTINGS && std::memcmp(&m.u.settings, &s, sizeof(s)) == 0);
    CHECK(r.sink.sent.empty());
    CHECK(r.controller.preferences().settings().flipped == 1);

    // The same record again: echoed, not saved.
    r.controller.onFrame(settingsFrame(3, s));
    CHECK(saved == 1);
    CHECK(r.sink.pop().command == PROTO_CMD_SETTINGS);

    // Sent with every list at the start of a session.
    r.sink.sent.clear();
    r.controller.onConnected();
    while (r.sink.sent.size() > 1U) {
        r.sink.pop();
    }
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_SETTINGS && m.u.settings.layout == 1);
}

void testPreferencesReplacedFromOutside()
{
    Rig r;
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    r.sink.sent.clear();

    // Nothing changed: only the settings are sent.
    r.controller.onPreferencesChanged();
    CHECK(r.sink.sent.size() == 1 && r.sink.pop().command == PROTO_CMD_SETTINGS);

    // An alias set from the command line reaches the board.
    (void)r.controller.preferences().setAlias("spk", "Hoparl\xC3\xB6r");
    r.controller.onPreferencesChanged();
    CHECK(r.sink.sent.size() == 3 + 1 + 1 + 1);
    CHECK(r.sink.pop().command == PROTO_CMD_DEVICE_LIST);
    r.sink.pop();
    r.sink.pop();
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_DEVICE_ALIAS && m.u.alias.position == 0);

    // Not synced: nothing.
    r.controller.onDisconnected();
    r.sink.sent.clear();
    r.controller.onPreferencesChanged();
    CHECK(r.sink.sent.empty());
}

// ------------------------------------------- preference file (#59)

void testPreferenceFileRoundTrip()
{
    Preferences p;
    (void)p.setAlias("{0.0.0}.{a}", "Masa\xC3\xBCst\xC3\xBC\thoparl\xC3\xB6r\\1");  // TAB and backslash
    (void)p.setFavorite("{0.0.0}.{a}", true);
    (void)p.setFavorite("{0.0.0}.{b}", true);
    (void)p.arrange({ep("{0.0.0}.{b}", "B"), ep("{0.0.0}.{a}", "A"), ep("{0.0.0}.{c}", "C")});
    ProtoSettings s = p.settings();
    s.layout = 2;
    s.accent = 4;
    s.flipped = 1;
    s.volume_limit_on = 1;
    s.volume_limit = 35;
    (void)p.setSettings(s);

    const std::string text = serializePreferences(p);
    CHECK(text.rfind("# stm32-desk-dashboard preferences\nversion\t1\n", 0) == 0);
    Preferences q;
    CHECK(parsePreferences(text, q));
    CHECK(std::memcmp(&q.settings(), &p.settings(), sizeof(ProtoSettings)) == 0);
    CHECK(q.order() == p.order());
    CHECK(q.device("{0.0.0}.{a}") != nullptr && q.device("{0.0.0}.{a}")->alias == p.device("{0.0.0}.{a}")->alias);
    CHECK(q.device("{0.0.0}.{a}")->favorite && q.device("{0.0.0}.{b}")->favorite);
    CHECK(q.device("{0.0.0}.{b}")->alias.empty());
    CHECK(serializePreferences(q) == text);

    // CR LF line ends (file edited on Windows) read the same.
    std::string crlf;
    for (char c : text) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    Preferences r;
    CHECK(parsePreferences(crlf, r));
    CHECK(serializePreferences(r) == text);
}

void testPreferenceFileDamaged()
{
    Preferences p;
    (void)p.setAlias("x", "keep");

    // Not a preference file, or an unknown version: rejected, p unchanged.
    CHECK(!parsePreferences("", p));
    CHECK(!parsePreferences("hello\n", p));
    CHECK(!parsePreferences("version\t2\nsetting\ttheme\t1\n", p));
    CHECK(p.device("x") != nullptr && p.device("x")->alias == "keep");

    // Damaged lines inside a valid file are skipped.
    const std::string text =
        "version\t1\n"
        "setting\ttheme\t1\n"
        "setting\taccent\t9\n"         // out of range: default kept
        "setting\tlayout\tabc\n"       // not a number
        "setting\tfuture_option\t3\n"  // unknown name
        "device\n"                     // no ID
        "device\tid1\tfavorite\t1\talias\t" + std::string(40, 'a') + "\n"  // alias cut to 32 bytes
        "order\tid1\norder\tid1\norder\tid2\n"                              // duplicate ID
        "something-new\tx\n";
    CHECK(parsePreferences(text, p));
    CHECK(p.settings().theme == 1 && p.settings().accent == 0 && p.settings().layout == 0);
    CHECK(p.device("x") == nullptr);
    CHECK(p.device("id1")->favorite && p.device("id1")->alias == std::string(32, 'a'));
    CHECK(p.order() == std::vector<std::string>({"id1", "id2"}));
}

void testPreferenceStore()
{
    FakeFile file;
    Preferences prefs;
    PreferenceStore store(file, prefs);

    CHECK(store.load() == PreferenceStore::LoadResult::Missing);
    CHECK(!store.poll(0));
    CHECK(file.writes == 0);

    // Changes are written at most once per second.
    (void)prefs.setAlias("a", "A");
    store.markChanged();
    CHECK(!store.poll(100));
    CHECK(file.writes == 1);
    (void)prefs.setAlias("a", "B");
    store.markChanged();
    CHECK(!store.poll(600));
    CHECK(file.writes == 1 && store.dirty());
    CHECK(!store.poll(1100));
    CHECK(file.writes == 2 && !store.dirty());
    CHECK(file.content->find("alias\tB") != std::string::npos);

    // Another program replaced the file: reloaded at the next check, and
    // the file wins over unsaved changes.
    Preferences other;
    (void)other.setAlias("a", "From CLI");
    file.content = serializePreferences(other);
    (void)prefs.setFavorite("a", true);
    store.markChanged();
    CHECK(store.poll(2200));
    CHECK(prefs.device("a")->alias == "From CLI" && !prefs.device("a")->favorite);
    CHECK(!store.dirty());
    CHECK(!store.poll(3300));  // no change since

    // Our own writes do not count as external changes.
    (void)prefs.setFavorite("a", true);
    store.markChanged();
    CHECK(!store.poll(4400));
    CHECK(!store.poll(5500));
    CHECK(prefs.device("a")->favorite);

    // A write failure keeps the change for the next try; flush reports it.
    file.failWrite = true;
    (void)prefs.setFavorite("a", false);
    store.markChanged();
    (void)store.poll(6600);
    CHECK(store.dirty() && store.writeFailures() == 1);
    CHECK(!store.flush());
    file.failWrite = false;
    CHECK(store.flush());
    CHECK(!store.dirty());

    // An unreadable file is kept aside and replaced by defaults.
    FakeFile bad;
    bad.content = "garbage";
    Preferences fresh;
    (void)fresh.setAlias("z", "Z");
    PreferenceStore badStore(bad, fresh);
    CHECK(badStore.load() == PreferenceStore::LoadResult::Unreadable);
    CHECK(bad.kept == std::string("garbage"));
    CHECK(fresh.device("z") == nullptr);
    // ... and is not retried every second.
    CHECK(!badStore.poll(0));
    CHECK(!badStore.poll(2000));
}

void testVolumeLimit()
{
    FakeAudio audio;
    audio.endpoints = {ep("spk", "Speakers"), ep("hp", "Headphones"), ep("tv", "TV"), ep("x", "No volume")};
    audio.defaultId = "hp";
    audio.endpointVolumes = {{"spk", 30}, {"tv", 100}};  // "x" has no volume control

    ProtoSettings s{};
    Proto_DefaultSettings(&s);
    s.volume_limit = 40;
    CHECK(!applyVolumeLimit(audio, s, audio.endpoints[2]));  // off
    CHECK(audio.endpointVolumes["tv"] == 100U);

    s.volume_limit_on = 1;
    const std::optional<VolumeLimited> lowered = applyVolumeLimit(audio, s, audio.endpoints[2]);
    CHECK(lowered && lowered->endpointId == "tv" && lowered->name == "TV");
    CHECK(lowered && lowered->from == 100 && lowered->to == 40);
    CHECK(audio.endpointVolumes["tv"] == 40U);
    CHECK(!applyVolumeLimit(audio, s, audio.endpoints[0]));  // 30 %: never raised
    CHECK(audio.endpointVolumes["spk"] == 30U);
    CHECK(!applyVolumeLimit(audio, s, audio.endpoints[3]));  // no volume control
    CHECK(audio.mute == false);                               // mute untouched
}

void testVolumeLimitOnSelect()
{
    Rig r;
    std::vector<VolumeLimited> logged;
    r.controller.setVolumeLimitedHandler([&logged](const VolumeLimited& v) { logged.push_back(v); });
    r.audio.volume = 90;  // "hp", the default
    r.audio.endpointVolumes = {{"spk", 20}, {"tv", 80}};
    ProtoSettings s = r.controller.preferences().settings();
    s.volume_limit_on = 1;
    s.volume_limit = 25;
    (void)r.controller.preferences().setSettings(s);

    // A board session starts: nothing is limited.
    r.controller.onConnected();
    r.controller.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    CHECK(r.audio.volume == 90U && r.audio.endpointVolumes["tv"] == 80U && logged.empty());

    // Selecting a louder device lowers it before it becomes the default.
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 2, r.gen(), r.index("tv")));
    CHECK(r.audio.endpointVolumes["tv"] == 25U);
    CHECK(r.audio.endpointVolumeDefaults.size() == 1 && r.audio.endpointVolumeDefaults[0] == "hp");
    CHECK(r.audio.defaultId == "tv");
    CHECK(logged.size() == 1 && logged[0].endpointId == "tv" && logged[0].from == 80 && logged[0].to == 25);

    // A quieter device is not raised.
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 3, r.gen(), r.index("spk")));
    CHECK(r.audio.endpointVolumes["spk"] == 20U && logged.size() == 1);

    // A switch made in Windows is not limited.
    r.audio.endpointVolumes["tv"] = 70;
    r.audio.defaultId = "tv";
    r.controller.onDefaultChanged();
    CHECK(r.audio.endpointVolumes["tv"] == 70U && logged.size() == 1);

    // Off: a selection is not limited.
    s.volume_limit_on = 0;
    (void)r.controller.preferences().setSettings(s);
    r.audio.defaultId = "spk";
    r.controller.onDefaultChanged();
    r.controller.onFrame(requestFrame(PROTO_CMD_SELECT_DEVICE, 4, r.gen(), r.index("tv")));
    CHECK(r.audio.defaultId == "tv" && r.audio.endpointVolumes["tv"] == 70U && logged.size() == 1);
}

// ------------------------------------------------- helper tests

void testAudioMapping()
{
    CHECK(formFactorFromWindows(1) == FormFactor::Speakers);
    CHECK(formFactorFromWindows(3) == FormFactor::Headphones);
    CHECK(formFactorFromWindows(5) == FormFactor::Headset);
    CHECK(formFactorFromWindows(6) == FormFactor::Headset);
    CHECK(formFactorFromWindows(7) == FormFactor::DigitalOutput);
    CHECK(formFactorFromWindows(8) == FormFactor::DigitalOutput);
    CHECK(formFactorFromWindows(9) == FormFactor::Display);
    CHECK(formFactorFromWindows(0) == FormFactor::Unknown);
    CHECK(formFactorFromWindows(2) == FormFactor::Unknown);
    CHECK(formFactorFromWindows(10) == FormFactor::Unknown);
    CHECK(formFactorFromWindows(-1) == FormFactor::Unknown);
    CHECK(formFactorFromWindows(99) == FormFactor::Unknown);

    CHECK(percentFromScalar(0.0F) == 0U);
    CHECK(percentFromScalar(1.0F) == 100U);
    CHECK(percentFromScalar(0.5F) == 50U);
    CHECK(percentFromScalar(0.004F) == 0U);
    CHECK(percentFromScalar(0.006F) == 1U);
    CHECK(percentFromScalar(-0.5F) == 0U);
    CHECK(percentFromScalar(1.5F) == 100U);
    CHECK(percentFromScalar(std::numeric_limits<float>::quiet_NaN()) == 0U);
    CHECK(scalarFromPercent(0U) == 0.0F);
    CHECK(scalarFromPercent(100U) == 1.0F);
    CHECK(scalarFromPercent(250U) == 1.0F);
    // Every percent survives a round trip through the scalar.
    for (unsigned p = 0; p <= 100U; ++p) {
        CHECK(percentFromScalar(scalarFromPercent(p)) == p);
    }
}

std::optional<HostEvent> popEvent(EventQueue& q)
{
    const std::optional<QueuedEvent> e = q.pop(std::chrono::milliseconds(1));
    if (!e) {
        return std::nullopt;
    }
    return e->event;
}

void testEventQueue()
{
    EventQueue q;
    CHECK(!q.pop(std::chrono::milliseconds(1)).has_value());

    // Duplicates of a waiting audio event are dropped; order is kept.
    q.push(HostEvent::DevicesChanged);
    q.push(HostEvent::VolumeChanged);
    q.push(HostEvent::DevicesChanged);
    q.push(HostEvent::VolumeChanged);
    CHECK(q.size() == 2);
    CHECK(popEvent(q) == HostEvent::DevicesChanged);
    CHECK(popEvent(q) == HostEvent::VolumeChanged);
    CHECK(q.size() == 0);

    // A popped event can be queued again.
    q.push(HostEvent::DevicesChanged);
    CHECK(q.size() == 1);
    (void)q.pop(std::chrono::milliseconds(1));

    // Connect / disconnect are never merged: each one is a state change.
    q.push(HostEvent::HidConnected);
    q.push(HostEvent::HidDisconnected);
    q.push(HostEvent::HidConnected);
    q.push(HostEvent::HidDisconnected);
    CHECK(q.size() == 4);
    CHECK(popEvent(q) == HostEvent::HidConnected);
    CHECK(popEvent(q) == HostEvent::HidDisconnected);
    CHECK(popEvent(q) == HostEvent::HidConnected);
    CHECK(popEvent(q) == HostEvent::HidDisconnected);

    // Push from another thread wakes the waiting consumer.
    std::thread producer([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (int i = 0; i < 1000; ++i) {
            q.push(HostEvent::VolumeChanged);
        }
        q.push(HostEvent::Quit);
    });
    bool sawQuit = false;
    int volumeEvents = 0;
    while (!sawQuit) {
        const std::optional<QueuedEvent> e = q.pop(std::chrono::seconds(5));
        CHECK(e.has_value());
        if (!e) {
            break;
        }
        sawQuit = (e->event == HostEvent::Quit);
        volumeEvents += (e->event == HostEvent::VolumeChanged) ? 1 : 0;
    }
    producer.join();
    CHECK(sawQuit);
    CHECK(volumeEvents >= 1 && volumeEvents <= 1000);
}

void testEventQueueFrames()
{
    EventQueue q(3);
    Frame a{};
    Frame b{};
    a[0] = 0x11;
    b[0] = 0x22;

    // Frames are carried with their content, in order, and never merged
    // (two identical frames are two requests).
    CHECK(q.pushFrame(a));
    q.push(HostEvent::DevicesChanged);
    CHECK(q.pushFrame(b));
    CHECK(q.pushFrame(b));
    std::optional<QueuedEvent> e = q.pop(std::chrono::milliseconds(1));
    CHECK(e && e->event == HostEvent::FrameReceived && e->frame == a);
    CHECK(popEvent(q) == HostEvent::DevicesChanged);
    e = q.pop(std::chrono::milliseconds(1));
    CHECK(e && e->event == HostEvent::FrameReceived && e->frame == b);

    // Bounded: with 3 frames waiting the next is dropped and counted;
    // other events still get in.
    CHECK(q.pushFrame(a));
    CHECK(q.pushFrame(a));
    CHECK(!q.pushFrame(b));
    CHECK(q.droppedFrames() == 1);
    q.push(HostEvent::HidDisconnected);
    CHECK(q.size() == 4);

    // Popping a frame frees a slot.
    for (int i = 0; i < 3; ++i) {
        CHECK(popEvent(q) == HostEvent::FrameReceived);
    }
    CHECK(q.pushFrame(b));
    CHECK(q.droppedFrames() == 1);
    CHECK(popEvent(q) == HostEvent::HidDisconnected);
    e = q.pop(std::chrono::milliseconds(1));
    CHECK(e && e->frame == b);
    CHECK(q.size() == 0);
}

void testHidMatch()
{
    HidInfo info;
    info.vendorId = 0x1209;
    info.productId = 0x0002;
    info.usagePage = 0xFF00;
    info.usage = 0x0001;
    info.inputReportBytes = 65;
    info.outputReportBytes = 65;
    CHECK(isController(info));
    // Compile-time constant: a runtime CHECK trips C4127 on MSVC 17.11 (/WX).
    static_assert(kReportBytes == 65, "HID report = report ID + 64-byte frame");

    HidInfo other = info;
    other.vendorId = 0x0483;  // ST
    CHECK(!isController(other));
    other = info;
    other.productId = 0x0001;  // stm32-audio-controller board
    CHECK(!isController(other));
    other = info;
    other.usagePage = 0x0001;  // e.g. a keyboard collection of the same device
    CHECK(!isController(other));
    other = info;
    other.usage = 0x0002;
    CHECK(!isController(other));
    other = info;
    other.inputReportBytes = 64;  // a firmware with a report ID or another size
    CHECK(!isController(other));
    other = info;
    other.outputReportBytes = 0;  // input-only interface
    CHECK(!isController(other));
}

void testFrameText()
{
    ProtoMessage m{};
    m.command = PROTO_CMD_DEVICE_LIST;
    m.sequence = 3;
    m.u.device.generation = 1;
    m.u.device.total = 3;
    m.u.device.position = 1;
    m.u.device.device_index = 4;
    m.u.device.flags = PROTO_DEVICE_FLAG_DEFAULT;
    m.u.device.icon = PROTO_ICON_HEADPHONES;
    std::memcpy(m.u.device.name, "Kulakl\xC4\xB1k", 9);
    m.u.device.name_len = 9;
    CHECK(describeMessage(m) == "DEVICE_LIST seq=3 gen=1 2/3 index=4 default icon=headphones \"Kulakl\xC4\xB1k\"");

    m = ProtoMessage{};
    m.command = PROTO_CMD_DEVICE_LIST;
    m.u.device.device_index = PROTO_NO_DEVICE;
    CHECK(describeMessage(m) == "DEVICE_LIST seq=0 gen=0 empty");

    m = ProtoMessage{};
    m.command = PROTO_CMD_DEVICE_CHANGED;
    m.u.device_index = PROTO_NO_DEVICE;
    CHECK(describeMessage(m) == "DEVICE_CHANGED seq=0 index=none");

    m = ProtoMessage{};
    m.command = PROTO_CMD_ERROR;
    m.sequence = 7;
    m.u.error.code = PROTO_ERROR_UNKNOWN_DEVICE;
    m.u.error.ref_command = PROTO_CMD_SELECT_DEVICE;
    m.u.error.ref_sequence = 2;
    CHECK(describeMessage(m) == "ERROR seq=7 code=UNKNOWN_DEVICE ref_cmd=3 ref_seq=2");

    m = ProtoMessage{};
    m.command = PROTO_CMD_MUTE_CHANGED;
    m.u.muted = 1;
    CHECK(describeMessage(m) == "MUTE_CHANGED seq=0 muted=1");

    m = ProtoMessage{};
    m.command = PROTO_CMD_SELECT_DEVICE;
    m.u.request.generation = 4;
    m.u.request.device_index = 2;
    CHECK(describeMessage(m) == "SELECT_DEVICE seq=0 gen=4 index=2");

    m.command = PROTO_CMD_MOVE_DEVICE;
    m.u.request.value = 0;
    CHECK(describeMessage(m) == "MOVE_DEVICE seq=0 gen=4 index=2 to=1");

    m = ProtoMessage{};
    m.command = PROTO_CMD_DEVICE_ALIAS;
    m.u.alias.generation = 4;
    m.u.alias.position = 1;
    m.u.alias.alias_len = 3;
    std::memcpy(m.u.alias.alias, "DAC", 3);
    CHECK(describeMessage(m) == "DEVICE_ALIAS seq=0 gen=4 position=2 \"DAC\"");

    m = ProtoMessage{};
    m.command = PROTO_CMD_SETTINGS;
    Proto_DefaultSettings(&m.u.settings);
    CHECK(describeMessage(m) ==
          "SETTINGS seq=0 layout=0 theme=0 accent=0 grid=0 startup_tab=0 last_tab=0 timeout=0 flipped=0 language=0 limit=off");
}

}  // namespace

// Start with Windows (#74): the Run value and its comparison.
void testStartupCommand()
{
    const std::string exe = "C:\\Program Files\\STM32 Audio\\dashboard-tray.exe";

    CHECK(startupCommand(exe) == "\"" + exe + "\"");
    CHECK(startupCommandMatches(startupCommand(exe), exe));
    CHECK(startupCommandMatches("  \"c:\\program files\\stm32 audio\\DASHBOARD-TRAY.EXE\"  ", exe));
    CHECK(startupCommandMatches("\"" + exe + "\" --minimized", exe));
    CHECK(!startupCommandMatches("\"C:\\Other\\dashboard-tray.exe\"", exe));
    CHECK(!startupCommandMatches("\"" + exe, exe));  // no closing quote
    CHECK(!startupCommandMatches("", exe));
    CHECK(!startupCommandMatches(startupCommand(exe), ""));

    const std::string plain = "C:\\Tools\\dashboard-tray.exe";
    CHECK(startupCommandMatches(plain, plain));
    CHECK(startupCommandMatches(plain + " --x", plain));
    CHECK(!startupCommandMatches("C:\\Tools\\dashboard-tray.exe.bak", plain));
}

void testTrayText()
{
    CHECK(std::strcmp(trayText(TrayText::Exit, 1), "Exit") == 0);
    CHECK(std::strcmp(trayText(TrayText::Exit, 0), "\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F") == 0);
    CHECK(std::strcmp(trayText(TrayText::Exit, 7), trayText(TrayText::Exit, 0)) == 0);  // unknown: Turkish
    for (int id = 0; id <= static_cast<int>(TrayText::StartupFailed); ++id) {
        CHECK(std::strlen(trayText(static_cast<TrayText>(id), 0)) > 0);
        CHECK(std::strlen(trayText(static_cast<TrayText>(id), 1)) > 0);
    }
    // Tooltips are limited to 127 UTF-16 units: title + state must fit.
    CHECK(std::strlen(trayText(TrayText::Title, 0)) + std::strlen(trayText(TrayText::Waiting, 0)) < 100);
}

int main()
{
    testTruncateUtf8();
    testIcons();
    testRegistryIndices();
    testRegistryIndexWraps();
    testRegistryLimit();
    testRegistryEntries();
    testNothingBeforeFirstRequest();
    testGetDeviceList();
    testSequenceNumbers();
    testEmptyList();
    testSelectDevice();
    testSelectBeforeListSent();
    testSelectErrors();
    testVolumeAndMute();
    testWindowsChanges();
    testVolumeResentAfterNoDefault();
    testReconnect();
    testConnectSendsList();
    testBadFrames();
    testSendFailure();
    testPreferencesArrangeAndMove();
    testPreferencesAliasFavoriteSettings();
    testRegistryAliasesAndFavorites();
    testSelectStaleGeneration();
    testEditsAnsweredWithNewList();
    testSettingsStoredAndEchoed();
    testPreferencesReplacedFromOutside();
    testPreferenceFileRoundTrip();
    testPreferenceFileDamaged();
    testPreferenceStore();
    testVolumeLimit();
    testVolumeLimitOnSelect();
    testAudioMapping();
    testEventQueue();
    testEventQueueFrames();
    testHidMatch();
    testFrameText();
    testStartupCommand();
    testTrayText();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
