// Unit tests for the host device model and protocol controller.
// No test framework; returns non-zero if a check fails.

#include <atomic>
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
#include "DashController.h"
#include "DeviceRegistry.h"
#include "EventQueue.h"
#include "FrameRouter.h"
#include "FrameText.h"
#include "HidMatch.h"
#include "HostController.h"
#include "MediaCover.h"
#include "MediaKeys.h"
#include "MediaService.h"
#include "MediaSession.h"
#include "MenuData.h"
#include "MenuImport.h"
#include "MenuService.h"
#include "nlohmann/json.hpp"
#include "PreferenceFile.h"
#include "PreferenceStore.h"
#include "Preferences.h"
#include "SequencedSink.h"
#include "Startup.h"
#include "VolumeLimit.h"
#include "BackgroundFetch.h"
#include "ApprovalService.h"
#include "ClaudeHook.h"
#include "WeatherCard.h"
#include "WeatherLocation.h"
#include "WeatherReport.h"
#include "WeatherService.h"
#include "WeatherSamples.h"

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
    s.startup_screen = 1;
    (void)p.setSettings(s);

    const std::string text = serializePreferences(p);
    CHECK(text.find("startup_screen\t1\n") != std::string::npos);
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

void testEventQueueHook()
{
    EventQueue q;
    q.pushHook(HostEvent::HookRequest, 7, "{\"tool_name\":\"Bash\"}");
    q.pushHook(HostEvent::HookRequest, 8, "x");
    q.pushHook(HostEvent::HookCancelled, 7);
    std::optional<QueuedEvent> e = q.pop(std::chrono::milliseconds(1));
    CHECK(e && e->event == HostEvent::HookRequest && e->ticket == 7U && e->text.find("Bash") != std::string::npos);
    e = q.pop(std::chrono::milliseconds(1));
    CHECK(e && e->event == HostEvent::HookRequest && e->ticket == 8U);  // never merged
    e = q.pop(std::chrono::milliseconds(1));
    CHECK(e && e->event == HostEvent::HookCancelled && e->ticket == 7U && e->text.empty());
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
          "SETTINGS seq=0 layout=0 theme=0 accent=0 grid=0 startup_tab=0 last_tab=0 timeout=0 flipped=0 language=0 limit=off startup_screen=0");
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
    for (int id = 0; id <= static_cast<int>(TrayText::HookSettingsCopied); ++id) {
        CHECK(std::strlen(trayText(static_cast<TrayText>(id), 0)) > 0);
        CHECK(std::strlen(trayText(static_cast<TrayText>(id), 1)) > 0);
    }
    // Tooltips are limited to 127 UTF-16 units: title + state must fit.
    CHECK(std::strlen(trayText(TrayText::Title, 0)) + std::strlen(trayText(TrayText::Waiting, 0)) < 100);
}

// ------------------------------------------------------------ dashboard (ADR-006)

namespace {

struct DashRig {
    FakeSink sink;
    std::uint32_t now = 1000000U;
    DashController dash{sink, [this] { return now; }};
};

ProtoMessage dashMessage(std::uint8_t command, std::uint8_t sequence = 0)
{
    ProtoMessage m{};
    m.command = command;
    m.sequence = sequence;
    return m;
}

// Collect the parts of one record from the sink (they are sent in order).
std::string popRecord(FakeSink& sink, std::uint8_t command, std::uint8_t* id = nullptr)
{
    std::string text;
    std::uint8_t parts = 0;
    std::uint8_t part = 0;
    do {
        const ProtoMessage m = sink.pop();
        CHECK(m.command == command);
        if (m.command != command) {
            return text;
        }
        CHECK(m.u.part.part == part);
        parts = m.u.part.parts;
        if (id != nullptr) {
            *id = m.u.part.id;
        }
        text.append(reinterpret_cast<const char*>(m.u.part.data), m.u.part.len);
        ++part;
    } while (part < parts);
    return text;
}

}  // namespace

void testRecordText()
{
    const std::string line = recordLine({"3", "Hava", ""});
    CHECK(line == std::string("3\x1F" "Hava\x1F"));
    CHECK(recordLine({"a\nb", "c\x1F" "d", std::string_view("e\0f", 3), "g\rh"}) ==
          std::string("a b\x1F" "c d\x1F" "e f\x1F" "g h"));
    CHECK(recordLine({}).empty());
    CHECK(cutUtf8("abc", 5) == "abc");
    CHECK(cutUtf8("abcdef", 4) == "abcd");
    CHECK(cutUtf8("ab\xC3\xB6x", 3) == "ab");      // never half of "ö"
    CHECK(cutUtf8("ab\xC3\xB6x", 4) == "ab\xC3\xB6");
    CHECK(cutUtf8("\xE2\x82\xAC", 2).empty());       // "€" is 3 bytes
}

void testLocalTimeValue()
{
    CHECK(localTimeValue(1970, 1, 1, 0, 0, 0) == 0U);
    CHECK(localTimeValue(2000, 1, 1, 0, 0, 0) == 946684800U);
    CHECK(localTimeValue(2026, 10, 21, 14, 32, 5) == 1792593125U);
    CHECK(localTimeValue(2028, 2, 29, 23, 59, 59) == 1835481599U);  // leap day
}

void testSequencedSink()
{
    FakeSink inner;
    SequencedSink sink(inner);
    Frame f{};
    ProtoMessage m = dashMessage(PROTO_CMD_GET_CARDS, 77);
    CHECK(Proto_Encode(&m, f.data()) == PROTO_OK);
    CHECK(sink.sendFrame(f));
    CHECK(sink.sendFrame(f));
    CHECK(inner.sent.size() == 2U);  // FakeSink checks the CRC by decoding
    CHECK(inner.pop().sequence == 0U);
    CHECK(inner.pop().sequence == 1U);
    sink.reset();
    CHECK(sink.sendFrame(f) && inner.pop().sequence == 0U);
}

void testDashNothingBeforeSync()
{
    DashRig r;
    r.dash.setCard(0, "x");
    r.dash.onTick(5000);
    CHECK(r.sink.sent.empty());
}

void testDashGetCards()
{
    DashRig r;
    r.dash.setCard(0, recordLine({"0", "Hava", ""}) + "\n14\xC2\xB0");
    r.dash.onMessage(dashMessage(PROTO_CMD_GET_CARDS));

    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_TIME && m.u.local_time == 1000000U);
    std::uint8_t id = 0xFF;
    CHECK(popRecord(r.sink, PROTO_CMD_CARD_PART, &id) == r.dash.card(0));
    CHECK(id == 0U);
    for (std::uint8_t slot = 1; slot < PROTO_MAX_CARDS; ++slot) {
        m = r.sink.pop();
        CHECK(m.command == PROTO_CMD_CARD_CLEAR && m.u.slot == slot);
    }
    CHECK(r.sink.sent.empty());
}

void testDashCardUpdates()
{
    DashRig r;
    r.dash.onConnected();  // TIME + 8 clears
    CHECK(r.sink.sent.size() == 1U + PROTO_MAX_CARDS);
    r.sink.sent.clear();

    // A long card goes out in parts, cut to 256 bytes; the same text again
    // sends nothing; a new text gets a new generation.
    const std::string longText(300, 'x');
    r.dash.setCard(2, longText);
    CHECK(r.dash.card(2).size() == PROTO_CARD_MAX_BYTES);
    CHECK(r.sink.sent.size() == PROTO_CARD_MAX_PARTS);
    const std::uint8_t gen = r.sink.sent.front().u.part.generation;
    CHECK(popRecord(r.sink, PROTO_CMD_CARD_PART) == std::string(PROTO_CARD_MAX_BYTES, 'x'));
    r.dash.setCard(2, longText);
    CHECK(r.sink.sent.empty());
    r.dash.setCard(2, "y");
    CHECK(r.sink.sent.size() == 1U && r.sink.sent.front().u.part.generation != gen);
    r.sink.sent.clear();

    // Emptying a slot sends CARD_CLEAR; a slot out of range is ignored.
    r.dash.setCard(2, "");
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_CARD_CLEAR && m.u.slot == 2U);
    r.dash.setCard(PROTO_MAX_CARDS, "z");
    CHECK(r.sink.sent.empty());

    // After a disconnect nothing goes out until the next connect.
    r.dash.onDisconnected();
    r.dash.setCard(1, "w");
    CHECK(r.sink.sent.empty());
    r.dash.onConnected();
    CHECK(r.sink.sent.size() == 1U + PROTO_MAX_CARDS);
}

void testDashDetailAndAction()
{
    DashRig r;
    std::vector<std::pair<std::uint8_t, std::int8_t>> asked;
    std::vector<ProtoAction> actions;
    r.dash.onConnected();
    r.sink.sent.clear();

    // Without handlers: OPEN_DETAIL and ACTION fail.
    ProtoMessage open = dashMessage(PROTO_CMD_OPEN_DETAIL, 9);
    open.u.open_detail.slot = 1;
    r.dash.onMessage(open);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_OPERATION_FAILED &&
          m.u.error.ref_command == PROTO_CMD_OPEN_DETAIL && m.u.error.ref_sequence == 9U);
    ProtoMessage action = dashMessage(PROTO_CMD_ACTION, 10);
    action.u.action.kind = PROTO_ACTION_MEDIA;
    action.u.action.action = PROTO_MEDIA_NEXT;
    r.dash.onMessage(action);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.ref_command == PROTO_CMD_ACTION);

    // With handlers.
    const std::string detail = recordLine({"Yemek", "5 Ekim"}) + "\n" + std::string(200, 'd');
    r.dash.setDetailHandler([&](std::uint8_t slot, std::int8_t arg) -> std::optional<std::string> {
        asked.emplace_back(slot, arg);
        if (slot == 1) {
            return detail;
        }
        return std::nullopt;
    });
    r.dash.setActionHandler([&](const ProtoAction& a) {
        actions.push_back(a);
        return a.kind == PROTO_ACTION_MEDIA;
    });
    open.u.open_detail.arg = -1;
    r.dash.onMessage(open);
    CHECK(asked.size() == 1U && asked[0].first == 1U && asked[0].second == -1);
    std::uint8_t id = 0;
    CHECK(popRecord(r.sink, PROTO_CMD_DETAIL_PART, &id) == detail);
    CHECK(id == 1U);

    open.u.open_detail.slot = 3;  // no detail for this slot
    r.dash.onMessage(open);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_OPERATION_FAILED);

    r.dash.onMessage(action);
    CHECK(actions.size() == 1U && actions[0].action == PROTO_MEDIA_NEXT && r.sink.sent.empty());
    action.u.action.kind = PROTO_ACTION_CARD;  // refused by the handler
    action.u.action.ref = 0;
    action.u.action.action = 1;
    r.dash.onMessage(action);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.ref_command == PROTO_CMD_ACTION);

    // refreshDetail sends only the detail the board opened last (slot 1).
    asked.clear();
    r.dash.refreshDetail(2);
    CHECK(asked.empty() && r.sink.sent.empty());
    r.dash.refreshDetail(1);
    CHECK(asked.size() == 1U && asked[0].second == 0);
    CHECK(popRecord(r.sink, PROTO_CMD_DETAIL_PART) == detail);
}

void testDashTime()
{
    DashRig r;
    r.dash.onTick(1000);
    r.dash.onConnected();
    CHECK(r.dash.stats().timesSent == 1U);
    r.sink.sent.clear();

    // The clock runs on with the tick: nothing is sent.
    for (std::uint64_t t = 1000; t <= 1000 + 5 * 60 * 1000; t += 500) {
        r.now = 1000000U + static_cast<std::uint32_t>((t - 1000) / 1000);
        r.dash.onTick(t);
    }
    CHECK(r.sink.sent.empty());

    // A jump of the local clock (e.g. daylight saving) is sent at once.
    r.now += 3600U;
    r.dash.onTick(1000 + 5 * 60 * 1000 + 500);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_TIME && m.u.local_time == r.now);

    // And every 10 minutes anyway.
    const std::uint64_t sentAt = 1000 + 5 * 60 * 1000 + 500;
    r.now += 599U;
    r.dash.onTick(sentAt + DashController::kTimeIntervalMs - 1000);
    CHECK(r.sink.sent.empty());
    r.now += 1U;
    r.dash.onTick(sentAt + DashController::kTimeIntervalMs);
    m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_TIME);
}

void testFrameRouter()
{
    Rig audioRig;
    FakeSink dashSink;
    std::uint32_t now = 5U;
    DashController dash(dashSink, [&now] { return now; });
    FrameRouter router(audioRig.controller, dash, audioRig.sink);

    // Audio commands go to HostController (a list), dashboard commands to
    // DashController.
    audioRig.audio.endpoints = {ep("{a}", "A")};
    audioRig.controller.onDevicesChanged();
    router.onFrame(frameFor(PROTO_CMD_GET_DEVICE_LIST, 1));
    CHECK(!audioRig.sink.sent.empty() && audioRig.sink.sent.front().command == PROTO_CMD_DEVICE_LIST);
    CHECK(dashSink.sent.empty());
    audioRig.sink.sent.clear();

    router.onFrame(frameFor(PROTO_CMD_GET_CARDS, 2));
    CHECK(dashSink.sent.size() == 1U + PROTO_MAX_CARDS && audioRig.sink.sent.empty());
    dashSink.sent.clear();

    // An ERROR goes by ref_command.
    ProtoMessage e{};
    Frame f{};
    e.command = PROTO_CMD_ERROR;
    e.u.error.code = PROTO_ERROR_BAD_PAYLOAD;
    e.u.error.ref_command = PROTO_CMD_CARD_PART;
    CHECK(Proto_Encode(&e, f.data()) == PROTO_OK);
    router.onFrame(f);
    CHECK(dash.stats().errorsReceived == 1U);
    e.u.error.ref_command = PROTO_CMD_DEVICE_LIST;
    CHECK(Proto_Encode(&e, f.data()) == PROTO_OK);
    router.onFrame(f);
    CHECK(audioRig.controller.stats().errorsReceived == 1U);

    // An invalid frame is answered by the router, once.
    Frame bad = frameFor(PROTO_CMD_GET_CARDS, 3);
    bad[10] ^= 0x01U;  // CRC no longer matches
    router.onFrame(bad);
    CHECK(router.stats().framesRejected == 1U);
    ProtoMessage m = audioRig.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_BAD_FRAME);
    CHECK(audioRig.sink.sent.empty() && dashSink.sent.empty());
    CHECK(router.stats().toAudio == 2U && router.stats().toDashboard == 2U);
}

void testFrameTextDashboard()
{
    ProtoMessage m{};
    m.command = PROTO_CMD_CARD_PART;
    m.u.part.id = 2;
    m.u.part.generation = 5;
    m.u.part.part = 0;
    m.u.part.parts = 3;
    m.u.part.len = 53;
    CHECK(describeMessage(m) == "CARD_PART seq=0 id=2 gen=5 part=1/3 bytes=53");
    m = ProtoMessage{};
    m.command = PROTO_CMD_ACTION;
    m.u.action.kind = PROTO_ACTION_MEDIA;
    m.u.action.action = PROTO_MEDIA_PLAY_PAUSE;
    CHECK(describeMessage(m) == "ACTION seq=0 kind=2 ref=0 action=2");
    m = ProtoMessage{};
    m.command = PROTO_CMD_OPEN_DETAIL;
    m.u.open_detail.slot = 1;
    m.u.open_detail.arg = -1;
    CHECK(describeMessage(m) == "OPEN_DETAIL seq=0 slot=1 arg=-1");
    m = ProtoMessage{};
    m.command = PROTO_CMD_TIME;
    m.u.local_time = 42;
    CHECK(describeMessage(m) == "TIME seq=0 local=42");
}

struct FakeMediaKeys : IMediaKeys {
    std::vector<MediaKey> pressed;
    bool ok = true;
    bool press(MediaKey key) override
    {
        pressed.push_back(key);
        return ok;
    }
};

void testMediaKeys()
{
    CHECK(mediaKeyFor(PROTO_MEDIA_PREVIOUS) == MediaKey::Previous);
    CHECK(mediaKeyFor(PROTO_MEDIA_PLAY_PAUSE) == MediaKey::PlayPause);
    CHECK(mediaKeyFor(PROTO_MEDIA_NEXT) == MediaKey::Next);
    CHECK(!mediaKeyFor(0).has_value());
    CHECK(!mediaKeyFor(4).has_value());

    FakeMediaKeys keys;
    ProtoAction a{};
    a.kind = PROTO_ACTION_MEDIA;
    a.action = PROTO_MEDIA_PLAY_PAUSE;
    CHECK(handleMediaAction(a, keys));
    CHECK(keys.pressed.size() == 1U && keys.pressed[0] == MediaKey::PlayPause);

    a.action = 9;  // unknown media action: no key
    CHECK(!handleMediaAction(a, keys));
    a.kind = PROTO_ACTION_CARD;  // not the media strip
    a.action = PROTO_MEDIA_NEXT;
    CHECK(!handleMediaAction(a, keys));
    CHECK(keys.pressed.size() == 1U);

    keys.ok = false;  // Windows refused the input
    a.kind = PROTO_ACTION_MEDIA;
    CHECK(!handleMediaAction(a, keys));
    CHECK(keys.pressed.size() == 2U && keys.pressed[1] == MediaKey::Next);

    // Through DashController: a pressed key is answered by nothing, a
    // refused one by ERROR OPERATION_FAILED.
    DashRig r;
    r.dash.setActionHandler([&](const ProtoAction& act) { return handleMediaAction(act, keys); });
    r.dash.onConnected();
    r.sink.sent.clear();
    ProtoMessage action = dashMessage(PROTO_CMD_ACTION, 20);
    action.u.action.kind = PROTO_ACTION_MEDIA;
    action.u.action.action = PROTO_MEDIA_PREVIOUS;
    keys.ok = true;
    r.dash.onMessage(action);
    CHECK(r.sink.sent.empty() && keys.pressed.back() == MediaKey::Previous);
    keys.ok = false;
    r.dash.onMessage(action);
    CHECK(r.sink.sent.size() == 1U);
    const ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.ref_command == PROTO_CMD_ACTION &&
          m.u.error.ref_sequence == 20U && m.u.error.code == PROTO_ERROR_OPERATION_FAILED);
}

void testWeatherParse()
{
    const std::optional<WeatherReport> r = parseForecast(weather_samples::kForecast);
    CHECK(r.has_value());
    if (!r) {
        return;
    }
    CHECK(std::fabs(r->now.temperatureC - 18.4) < 1e-9 && r->now.code == 2 && r->now.isDay);
    CHECK(r->now.humidityPercent == 65 && std::fabs(r->now.windKmh - 12.3) < 1e-9);
    CHECK(std::fabs(r->now.feelsLikeC - 16.2) < 1e-9);
    CHECK(r->days.size() == 10U);
    CHECK(r->days[0].year == 2026 && r->days[0].month == 10 && r->days[0].day == 21);
    CHECK(r->days[1].code == 61 && r->days[1].precipitationPercent == 85);
    CHECK(r->days[7].minC < -3.5 && r->days[7].code == 71);
    CHECK(!r->days[9].precipitationPercent.has_value());

    const std::optional<WeatherReport> g = parseForecast(weather_samples::kForecastGaps);
    CHECK(g.has_value() && g->days.size() == 1U && !g->now.isDay && g->now.humidityPercent == 0);
    CHECK(!parseForecast(weather_samples::kForecastError).has_value());
    CHECK(!parseForecast("").has_value());
    CHECK(!parseForecast("{\"current\": {\"temperature_2m\": 3}").has_value());  // cut off
    CHECK(!parseForecast("[1,2]").has_value());

    const std::string url = forecastUrl(41.01384, 28.94966);
    CHECK(url.find("latitude=41.0138&longitude=28.9497") != std::string::npos);
    CHECK(url.find("forecast_days=10") != std::string::npos && url.find("timezone=auto") != std::string::npos);
}

void testWeatherCodes()
{
    CHECK(weatherIcon(0, true) == 1 && weatherIcon(0, false) == 2);
    CHECK(weatherIcon(2, true) == 3 && weatherIcon(1, false) == 4);
    CHECK(weatherIcon(3, false) == 5 && weatherIcon(48, true) == 6);
    CHECK(weatherIcon(53, true) == 7 && weatherIcon(81, true) == 7);
    CHECK(weatherIcon(65, true) == 8 && weatherIcon(82, true) == 8);
    CHECK(weatherIcon(75, true) == 9 && weatherIcon(86, true) == 9);
    CHECK(weatherIcon(95, true) == 10 && weatherIcon(99, true) == 10);
    CHECK(weatherIcon(4, true) == 0 && weatherIcon(-1, true) == 0);
    // Every code with an icon has a text in both languages.
    for (int code = 0; code < 100; ++code) {
        const bool icon = weatherIcon(code, true) != 0;
        CHECK(icon == !weatherText(code, 0).empty());
        CHECK(icon == !weatherText(code, 1).empty());
    }
    CHECK(weatherText(2, 0) == "Parçalı bulutlu" && weatherText(2, 1) == "Partly cloudy");
    CHECK(formatDegrees(18.4) == "18°" && formatDegrees(18.5) == "19°");
    CHECK(formatDegrees(-0.4) == "0°" && formatDegrees(-3.6) == "-4°");
}

void testWeatherLocation()
{
    const std::optional<WeatherLocation> a =
        parseLocationFile("\xEF\xBB\xBF# comment\r\nname = İstanbul \r\nlatitude=41.0138\nlongitude=28.9497\nother=1");
    CHECK(a.has_value() && a->name == "İstanbul" && std::fabs(a->latitude - 41.0138) < 1e-9);
    if (a) {
        const std::optional<WeatherLocation> b = parseLocationFile(formatLocationFile(*a));
        CHECK(b.has_value() && b->name == a->name && std::fabs(b->longitude - 28.9497) < 1e-9);
    }
    CHECK(!parseLocationFile("name=x\nlatitude=41\n").has_value());
    CHECK(!parseLocationFile("latitude=91\nlongitude=0\n").has_value());
    CHECK(!parseLocationFile("latitude=4x\nlongitude=0\n").has_value());
    CHECK(!parseLocationFile("").has_value());
    const std::optional<WeatherLocation> unnamed = parseLocationFile("latitude=-33.9\nlongitude=151.2");
    CHECK(unnamed.has_value() && unnamed->name.empty());

    CHECK(urlEncode("İstanbul a&b") == "%C4%B0stanbul%20a%26b");
    CHECK(geocodingUrl("Ankara", 1) ==
          "https://geocoding-api.open-meteo.com/v1/search?name=Ankara&count=5&format=json&language=en");
    const std::vector<Place> places = parseGeocoding(weather_samples::kGeocoding);
    CHECK(places.size() == 2U);
    if (places.size() == 2U) {
        CHECK(places[0].description == "İstanbul, İstanbul, Türkiye");
        CHECK(std::fabs(places[0].location.latitude - 41.01384) < 1e-9);
        CHECK(places[1].description == "Istanbul, United States");
    }
    CHECK(parseGeocoding("{\"generationtime_ms\": 0.3}").empty());
    CHECK(parseGeocoding("not json").empty());
}

void testWeatherCard()
{
    const std::uint32_t at14 = localTimeValue(2026, 10, 21, 14, 0, 0);
    const char sep = '\x1F';
    WeatherCard w;

    // No location: a hint, nothing to fetch.
    CHECK(w.card(at14).find("Konum ayarlanmadı") != std::string::npos);
    CHECK(!w.fetchDue(0).has_value() && !w.detail(at14).has_value());

    w.setLocation(WeatherLocation{"İstanbul", 41.0138, 28.9497});
    CHECK(w.card(at14) == std::string("0") + sep + "İstanbul" + sep + "\nBekleniyor…");
    const std::optional<std::string> url = w.fetchDue(1000);
    CHECK(url.has_value() && url->find("latitude=41.0138") != std::string::npos);
    CHECK(!w.fetchDue(1000).has_value());  // still running

    CHECK(w.onFetched(2000, at14, *url, std::string(weather_samples::kForecast)));
    const std::string card = w.card(at14);
    CHECK(card == std::string("3") + sep + "İstanbul" + sep + "\n18°  Parçalı bulutlu\n11° / 21° · Yağış %20\nRüzgâr 12 km/s");
    CHECK(!w.fetchDue(2000 + WeatherCard::kRefreshMs - 1).has_value());

    const std::optional<std::string> detail = w.detail(at14);
    CHECK(detail.has_value());
    if (detail) {
        CHECK(detail->rfind(std::string("Hava durumu") + sep + "İstanbul · 14:00" + sep + "\n#Şimdi\n", 0) == 0);
        CHECK(detail->find(std::string("\n0") + sep + "Hissedilen" + sep + "16°") != std::string::npos);
        CHECK(detail->find(std::string("\n0") + sep + "Nem" + sep + "%65") != std::string::npos);
        CHECK(detail->find("\n#10 gün\n") != std::string::npos);
        CHECK(detail->find(std::string("\n3") + sep + "Bugün" + sep + "11° / 21° · Yağış %20") != std::string::npos);
        CHECK(detail->find(std::string("\n7") + sep + "Yarın" + sep + "13° / 17° · Yağış %85") != std::string::npos);
        // 2026-10-23 is a Friday; the last day has no precipitation value.
        CHECK(detail->find(std::string("\n5") + sep + "Cum 23" + sep + "11° / 16° · Yağış %30") != std::string::npos);
        CHECK(detail->find(std::string("\n5") + sep + "Cum 30" + sep + "6° / 13°") != std::string::npos);
        const std::string last = "6° / 13°";
        CHECK(detail->size() >= last.size() && detail->compare(detail->size() - last.size(), last.size(), last) == 0);
        CHECK(detail->size() <= PROTO_DETAIL_MAX_BYTES);
    }

    // A failed refresh keeps the data and says since when.
    const std::uint32_t at15 = localTimeValue(2026, 10, 21, 15, 0, 0);
    CHECK(w.fetchDue(2000 + WeatherCard::kRefreshMs).has_value());
    CHECK(w.onFetched(3000 + WeatherCard::kRefreshMs, at15, *url, std::nullopt));
    CHECK(w.stale() && w.card(at15).find("\nGüncellendi 14:00") != std::string::npos);
    CHECK(!w.fetchDue(3000 + WeatherCard::kRefreshMs + WeatherCard::kRetryMs - 1).has_value());
    CHECK(w.fetchDue(3000 + WeatherCard::kRefreshMs + WeatherCard::kRetryMs).has_value());
    CHECK(!w.onFetched(4000 + WeatherCard::kRefreshMs, at15, *url, std::string("{}")));  // still stale: no change

    // After midnight the old report's first day is left out; English.
    w.setLanguage(1);
    const std::uint32_t nextDay = localTimeValue(2026, 10, 22, 0, 30, 0);
    const std::optional<std::string> later = w.detail(nextDay);
    CHECK(later.has_value() && later->find("Today") != std::string::npos &&
          later->find(std::string("\n7") + sep + "Today" + sep + "13° / 17° · Rain 85%") != std::string::npos);
    CHECK(later.has_value() && later->find("\n#9 days\n") != std::string::npos);
    CHECK(w.card(nextDay).find("13° / 17° · Rain 85%\nUpdated 14:00") != std::string::npos);

    // Same place renamed: no new fetch; another place: report dropped.
    w.setLocation(WeatherLocation{"Istanbul", 41.0138, 28.9497});
    CHECK(w.report().has_value() && w.location()->name == "Istanbul");
    w.setLocation(WeatherLocation{"Ankara", 39.92, 32.85});
    CHECK(!w.report().has_value() && !w.stale());
    const std::optional<std::string> ankara = w.fetchDue(0);  // at once
    CHECK(ankara.has_value() && ankara->find("latitude=39.9200") != std::string::npos);
    // The answer for the old place, still on its way, is dropped.
    CHECK(!w.onFetched(10, at14, *url, std::string(weather_samples::kForecast)) && !w.report().has_value());
    CHECK(w.onFetched(20, at14, *ankara, std::string(weather_samples::kForecast)) && w.report().has_value());
    w.setLocation(std::nullopt);
    CHECK(w.card(at14).find("No location set") != std::string::npos);
}

class FakeHttp : public IHttpClient {
public:
    std::optional<std::string> get(const std::string& url) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (url == "fail") {
            return std::nullopt;
        }
        return "body of " + url;
    }
    std::optional<HttpResponse> post(const std::string&, const HttpHeaders&, const std::string&) override
    {
        return std::nullopt;
    }
};

void testBackgroundFetch()
{
    FakeHttp http;
    BackgroundFetch fetch(http);
    CHECK(!fetch.poll().has_value() && !fetch.busy());
    CHECK(fetch.start("a"));
    CHECK(!fetch.start("b"));  // one at a time
    std::optional<BackgroundFetch::Result> r;
    for (int i = 0; i < 200 && !r; ++i) {
        r = fetch.poll();
        if (!r) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(r.has_value() && r->url == "a" && r->body == std::string("body of a"));
    CHECK(!fetch.busy() && !fetch.poll().has_value());
    CHECK(fetch.start("fail"));
    r.reset();
    for (int i = 0; i < 200 && !r; ++i) {
        r = fetch.poll();
        if (!r) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(r.has_value() && r->url == "fail" && !r->body.has_value());
    CHECK(fetch.start("c"));  // destroyed while running: joins
}

class MemoryFile : public IPreferenceFile {
public:
    std::optional<std::string> text;
    std::optional<std::string> read() override { return text; }
    bool write(const std::string& content) override
    {
        text = content;
        return true;
    }
    void keepUnreadable(const std::string&) override {}
    std::string location() const override { return "weather.txt"; }
};

class SampleHttp : public IHttpClient {
public:
    std::atomic<int> calls{0};
    std::atomic<bool> fail{false};
    std::optional<std::string> get(const std::string&) override
    {
        ++calls;
        if (fail) {
            return std::nullopt;
        }
        return std::string(weather_samples::kForecast);
    }
    std::optional<HttpResponse> post(const std::string&, const HttpHeaders&, const std::string&) override
    {
        return std::nullopt;
    }
};

void testWeatherService()
{
    DashRig r;
    r.now = localTimeValue(2026, 10, 21, 14, 0, 0);
    MemoryFile file;
    SampleHttp http;
    std::vector<std::string> logs;
    WeatherService weather(r.dash, file, http, [&r] { return r.now; },
                           [&logs](const std::string& line) { logs.push_back(line); });
    std::uint64_t ms = 1000;

    // No file: the hint card, no fetch.
    weather.onTick(ms);
    CHECK(r.dash.card(0).find("Konum ayarlanmadı") != std::string::npos && http.calls == 0);
    CHECK(!weather.detail(0).has_value());

    // The file appears; it is read again after a minute.
    file.text = "name=İstanbul\nlatitude=41.0138\nlongitude=28.9497\n";
    ms += 1000;
    weather.onTick(ms);
    CHECK(r.dash.card(0).find("Konum") != std::string::npos);
    ms += WeatherService::kFileIntervalMs;
    auto waitForCard = [&](const char* text) {
        for (int i = 0; i < 400 && r.dash.card(0).find(text) == std::string::npos; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ms += 5;
            weather.onTick(ms);
        }
        return r.dash.card(0).find(text) != std::string::npos;
    };
    CHECK(waitForCard("18°"));
    CHECK(http.calls == 1 && !logs.empty() && logs[0] == "Weather location: İstanbul");
    const std::optional<std::string> detail = weather.detail(0);
    CHECK(detail.has_value() && detail->find("#10 gün") != std::string::npos);
    CHECK(!weather.detail(1).has_value());

    // The board is synced: the open detail follows a language change.
    r.dash.onConnected();
    ProtoMessage open = dashMessage(PROTO_CMD_OPEN_DETAIL, 3);
    r.dash.setDetailHandler([&weather](std::uint8_t, std::int8_t arg) { return weather.detail(arg); });
    r.dash.onMessage(open);
    r.sink.sent.clear();
    weather.setLanguage(1);
    CHECK(r.dash.card(0).find("Wind 12 km/h") != std::string::npos);
    std::string sent = popRecord(r.sink, PROTO_CMD_CARD_PART);
    CHECK(sent.find("Wind 12 km/h") != std::string::npos);
    sent = popRecord(r.sink, PROTO_CMD_DETAIL_PART);
    CHECK(sent.find("#10 days") != std::string::npos);

    // A failure after 30 minutes: stale note, logged.
    http.fail = true;
    ms += WeatherCard::kRefreshMs;
    r.now += 1800U;
    CHECK(waitForCard("Updated 14:00"));
    CHECK(http.calls == 2 && logs.back().find("WARN: weather: no answer") == 0);

    // A broken file: warning, back to the hint.
    file.text = "latitude=x";
    ms += WeatherService::kFileIntervalMs;
    weather.onTick(ms);
    CHECK(r.dash.card(0).find("No location set") != std::string::npos);
    CHECK(logs.back() == "WARN: weather.txt has no valid latitude and longitude.");
}

// ------------------------------------------------------------------ menu
// Made-up menu data only (the real menu images stay out of the repo).

const char* const kMonthFile = R"({
  "ay": "2026-10",
  "gunler": {
    "2026-10-05": {
      "durum": "var",
      "ana_yemek": "Kağıt kebabı",
      "yan_yemek": "Kalem börek",
      "corba": "Tarhana çorba",
      "tatli": "Meyve / fıstıklı baklava",
      "yaninda": "Yoğurt / üzüm komposto",
      "salata_bar": ["Akdeniz salata", "Havuç tarator", "Zyt. börülce", "Cevizli biber", "Makarna salata", "Zeytin salata"]
    },
    "2026-10-28": { "durum": "tatil" },
    "2026-10-29": { "durum": "bilinmiyor" },
    "2026-11-02": { "durum": "var", "ana_yemek": "başka ay" },
    "2026-10-32": { "durum": "tatil" }
  }
})";

class MemoryMenuFiles : public IMenuFiles {
public:
    std::map<std::string, std::string> files;
    int reads = 0;
    bool failWrite = false;
    std::optional<std::string> read(const std::string& name) override
    {
        ++reads;
        const auto it = files.find(name);
        return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool write(const std::string& name, const std::string& text) override
    {
        if (failWrite) {
            return false;
        }
        files[name] = text;
        return true;
    }
};

class FakeClaude : public IHttpClient {
public:
    std::optional<HttpResponse> answer;
    std::string url;
    HttpHeaders headers;
    std::string body;
    std::optional<std::string> get(const std::string&) override { return std::nullopt; }
    std::optional<HttpResponse> post(const std::string& to, const HttpHeaders& h, const std::string& b) override
    {
        url = to;
        headers = h;
        body = b;
        return answer;
    }
};

// A Messages API answer whose text is @p daysJson.
HttpResponse claudeAnswer(const std::string& daysJson, const std::string& stop = "end_turn")
{
    nlohmann::json root = {
        {"id", "msg_test"},
        {"type", "message"},
        {"role", "assistant"},
        {"model", "claude-sonnet-5-5"},
        {"content", nlohmann::json::array({{{"type", "text"}, {"text", daysJson}}})},
        {"stop_reason", stop},
    };
    return HttpResponse{200, root.dump()};
}

std::string apiDay(int month, int day, const char* status, const char* main, const char* soup,
                   std::vector<std::string> salad = {})
{
    nlohmann::json d = {{"month", month}, {"day", day},       {"status", status}, {"main", main},
                        {"side", ""},     {"soup", soup},     {"dessert", ""},    {"extra", ""},
                        {"salad_bar", salad}};
    return d.dump();
}

void testMenuDates()
{
    CHECK(daysFromCivil(1970, 1, 1) == 0 && daysFromCivil(2026, 10, 21) == 20747);
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(20747, y, m, d);
    CHECK(y == 2026 && m == 10 && d == 21);
    CHECK(weekdayOf(daysFromCivil(2026, 10, 21)) == 3);  // Wednesday
    CHECK(validDate(2028, 2, 29) && !validDate(2026, 2, 29) && !validDate(2026, 11, 31) && !validDate(2026, 0, 1));
    CHECK(std::string(monthName(10, 0)) == "Ekim" && std::string(weekdayName(1, 1)) == "Monday");
    CHECK(std::string(weekdayShort(4, 0)) == "Per" && std::string(monthName(13, 0)).empty());
}

void testMenuMonthFile()
{
    const std::optional<MenuMonth> m = parseMonthFile(kMonthFile);
    CHECK(m.has_value());
    if (!m) {
        return;
    }
    CHECK(m->year == 2026 && m->month == 10 && m->days.size() == 2U);  // other month, bad state, bad date left out
    const MenuDay& d5 = m->days.at(5);
    CHECK(!d5.holiday && d5.main == "Kağıt kebabı" && d5.extra == "Yoğurt / üzüm komposto" && d5.saladBar.size() == 6U);
    CHECK(m->days.at(28).holiday);

    // Round trip; the file keeps the design's field order.
    const std::string text = formatMonthFile(*m);
    CHECK(text.find("\"ay\": \"2026-10\"") != std::string::npos);
    CHECK(text.find("\"durum\": \"var\",\n      \"ana_yemek\"") != std::string::npos);
    const std::optional<MenuMonth> again = parseMonthFile(text);
    CHECK(again.has_value() && again->days.size() == 2U && again->days.at(5).saladBar == d5.saladBar);
    CHECK(menuFileName(2026, 1) == "menu-2026-01");
    CHECK(!parseMonthFile("{}").has_value() && !parseMonthFile("{\"ay\": \"2026-13\"}").has_value());
    CHECK(!parseMonthFile("[").has_value());
}

void testMenuMergeAndSummary()
{
    MenuMonth month;
    month.year = 2026;
    month.month = 10;
    MenuDay meal;
    meal.main = "Kuru fasulye";
    meal.soup = "Mercimek çorba";
    MenuDay salad;
    salad.saladBar = {"Çoban salata", "Turşu"};
    MenuDay holiday;
    holiday.holiday = true;

    mergeDay(month, 5, meal);
    mergeDay(month, 5, salad);  // the salad bar from a second import keeps the meal
    CHECK(month.days.at(5).main == "Kuru fasulye" && month.days.at(5).saladBar.size() == 2U);
    mergeDay(month, 6, meal);
    mergeDay(month, 7, salad);
    mergeDay(month, 28, meal);
    mergeDay(month, 28, holiday);  // a holiday replaces the day
    CHECK(month.days.at(28).holiday && month.days.at(28).main.empty());
    mergeDay(month, 29, holiday);
    mergeDay(month, 29, meal);  // a menu clears a holiday
    CHECK(!month.days.at(29).holiday);
    month.days.at(29).holiday = true;

    CHECK(monthSummary(month, 0) ==
          "Ekim 2026: 2 gün yemek, 2 gün salata bar, 2 tatil; salata bar eksik: 6 Ekim; yemek eksik: 7 Ekim");
    CHECK(monthSummary(month, 1) == "October 2026: 2 days with a meal, 2 with the salad bar, 2 holidays; "
                                    "no salad bar: 6 October; no meal: 7 October");
}

void testMenuRequest()
{
    CHECK(base64Encode("") == "" && base64Encode("f") == "Zg==" && base64Encode("fo") == "Zm8=");
    CHECK(base64Encode("foo") == "Zm9v" && base64Encode("foobar") == "Zm9vYmFy");
    CHECK(base64Encode(std::string("\xFF\x00\x80", 3)) == "/wCA");
    CHECK(imageMediaType("Ekim.PNG") == std::string("image/png") && imageMediaType("a.jpeg") == std::string("image/jpeg"));
    CHECK(imageMediaType("a.webp").has_value() && !imageMediaType("menu.pdf").has_value() && !imageMediaType("png"));

    const ClaudeConfig file = parseClaudeConfig(std::string("\xEF\xBB\xBF# key\napi_key = sk-test \nmodel=claude-x\n"), "sk-env");
    CHECK(file.apiKey == "sk-test" && file.model == "claude-x");
    const ClaudeConfig env = parseClaudeConfig(std::nullopt, " sk-env\n");
    CHECK(env.apiKey == "sk-env" && env.model == kDefaultMenuModel);
    CHECK(parseClaudeConfig(std::string("model=\n"), "").apiKey.empty());

    const std::string body = menuRequestBody({MenuImage{"a.png", "image/png", "foo"}}, "claude-sonnet-5-5");
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    CHECK(j.is_object() && j["model"] == "claude-sonnet-5-5" && j["max_tokens"].is_number_integer());
    const nlohmann::json& content = j["messages"][0]["content"];
    CHECK(content.size() == 2U && content[0]["source"]["media_type"] == "image/png" &&
          content[0]["source"]["data"] == "Zm9v" && content[1]["type"] == "text");
    const nlohmann::json& schema = j["output_config"]["format"]["schema"];
    CHECK(j["output_config"]["format"]["type"] == "json_schema" && schema["additionalProperties"] == false);
    CHECK(schema["properties"]["days"]["items"]["additionalProperties"] == false &&
          schema["properties"]["days"]["items"]["required"].size() == 9U);
}

void testMenuAnswer()
{
    const std::string days = "{\"days\": [" + apiDay(10, 5, "menu", "  Kağıt\n kebabı ", "Tarhana çorba", {"Turşu", " "}) +
                             "," + apiDay(10, 28, "holiday", "", "") + "," + apiDay(10, 30, "none", "", "") + "," +
                             apiDay(10, 5, "menu", "tekrar", "") + "," + apiDay(11, 31, "menu", "Yok", "") + "," +
                             apiDay(1, 4, "menu", "Ocak yemeği", "") + "," + apiDay(10, 6, "menu", "", "") + "]}";
    const MenuAnswer a = parseMenuAnswer(claudeAnswer(days), 2026, 12, 0);
    CHECK(a.error.empty() && a.days.size() == 3U);
    if (a.days.size() == 3U) {
        CHECK(a.days[0].year == 2026 && a.days[0].day == 5 && a.days[0].content.main == "Kağıt kebabı");
        CHECK(a.days[0].content.saladBar.size() == 1U);
        CHECK(a.days[1].content.holiday && a.days[1].day == 28);
        CHECK(a.days[2].year == 2027 && a.days[2].month == 1);  // January after a December import
    }

    CHECK(parseMenuAnswer(HttpResponse{401, "{}"}, 2026, 10, 0).error == "Claude API anahtarı geçersiz (401).");
    const MenuAnswer bad = parseMenuAnswer(
        HttpResponse{400, R"({"type":"error","error":{"type":"invalid_request_error","message":"image too big"}})"}, 2026,
        10, 1);
    CHECK(bad.error == "Claude API error 400: image too big");
    CHECK(parseMenuAnswer(claudeAnswer("{\"days\": []}", "max_tokens"), 2026, 10, 1).error ==
          "Claude API answer incomplete (max_tokens).");
    CHECK(!parseMenuAnswer(claudeAnswer("{\"days\": []}"), 2026, 10, 0).error.empty());
    CHECK(!parseMenuAnswer(claudeAnswer("not json"), 2026, 10, 0).error.empty());
    CHECK(!parseMenuAnswer(HttpResponse{200, "<html>"}, 2026, 10, 0).error.empty());
}

void testMenuImport()
{
    MemoryMenuFiles files;
    files.files["menu-2026-10"] = kMonthFile;
    FakeClaude http;
    const std::vector<MenuImage> images = {MenuImage{"salata.png", "image/png", "png bytes"}};
    ClaudeConfig config;

    // No key: nothing sent.
    MenuImportResult r = importMenu(images, config, http, files, 2026, 10, 0);
    CHECK(!r.ok && r.message.find("api_key") != std::string::npos && http.url.empty());

    config.apiKey = "sk-test";
    CHECK(!importMenu({}, config, http, files, 2026, 10, 0).ok);
    CHECK(!importMenu({MenuImage{"big.png", "image/png", std::string(kMaxMenuImageBytes + 1U, 'x')}}, config, http,
                      files, 2026, 10, 0)
               .ok);
    r = importMenu(images, config, http, files, 2026, 10, 1);  // no answer
    CHECK(!r.ok && r.message == "Could not reach the Claude API.");
    CHECK(http.url == kClaudeApiUrl && http.headers.size() == 3U && http.headers[0].second == "sk-test");

    // Salad bar for the 5th (kept meal), a new day, and a day in November.
    http.answer = claudeAnswer("{\"days\": [" + apiDay(10, 5, "menu", "", "", {"Roka", "Turşu"}) + "," +
                               apiDay(10, 6, "menu", "Etli nohut", "Ezogelin çorba") + "," +
                               apiDay(11, 2, "menu", "Mantı", "Yayla çorba", {"Cacık"}) + "]}");
    r = importMenu(images, config, http, files, 2026, 10, 0);
    CHECK(r.ok);
    CHECK(r.message == "Ekim 2026: 2 gün yemek, 1 gün salata bar, 1 tatil; salata bar eksik: 6 Ekim\n"
                       "Kasım 2026: 1 gün yemek, 1 gün salata bar, 0 tatil");
    const std::optional<MenuMonth> oct = parseMonthFile(files.files["menu-2026-10"]);
    CHECK(oct.has_value() && oct->days.at(5).main == "Kağıt kebabı" && oct->days.at(5).saladBar.size() == 2U);
    CHECK(files.files.count("menu-2026-11") == 1U);

    files.failWrite = true;
    r = importMenu(images, config, http, files, 2026, 10, 0);
    CHECK(!r.ok && r.message.find("menu-2026-10.json") != std::string::npos);

    // A damaged month file is replaced by the imported days.
    files.failWrite = false;
    files.files["menu-2026-10"] = "{ broken";
    r = importMenu(images, config, http, files, 2026, 10, 0);
    CHECK(r.ok && parseMonthFile(files.files["menu-2026-10"]).has_value());
}

void testMenuService()
{
    DashRig r;
    MemoryMenuFiles files;
    std::vector<std::string> logs;
    r.now = localTimeValue(2026, 10, 5, 9, 0, 0);  // Monday
    MenuService menu(r.dash, files, [&r] { return r.now; }, [&logs](const std::string& l) { logs.push_back(l); });
    const char sep = '\x1F';
    std::uint64_t ms = 1000;

    // No file for the month.
    menu.onTick(ms);
    CHECK(r.dash.card(1) == std::string("11") + sep + "Yemek" + sep + "\nMenü yüklenmedi\nTepside: Menü görüntülerini yükle…");

    files.files["menu-2026-10"] = kMonthFile;
    menu.reload();
    menu.onTick(ms += 500);
    CHECK(r.dash.card(1) == std::string("11") + sep + "Yemek" + sep +
                                "\nKağıt kebabı\nTarhana çorba\nSalata bar: 6 çeşit");
    CHECK(logs.size() == 1U && logs[0] == "Menu files changed.");  // the file appeared

    // Detail, then the next working days (6th: no entry in the file).
    std::optional<std::string> d = menu.detail(0);
    CHECK(d.has_value() && d->rfind(std::string("Yemek") + sep + "5 Ekim Pazartesi" + sep + "n\n", 0) == 0);
    CHECK(d->find(std::string("\n") + sep + "Ana yemek" + sep + "Kağıt kebabı") != std::string::npos);
    CHECK(d->find(std::string("\n#Salata bar\n") + sep + sep + "Akdeniz salata · Havuç tarator") != std::string::npos);
    d = menu.detail(1);
    CHECK(d->find("6 Ekim Salı") != std::string::npos && d->find("Bu gün için menü yok") != std::string::npos);
    d = menu.detail(-1);
    d = menu.detail(-1);  // back over the weekend: Friday 2 October
    CHECK(d->find("2 Ekim Cuma") != std::string::npos);

    // Holiday; weekend shows Monday with a badge; English.
    r.now = localTimeValue(2026, 10, 28, 12, 0, 0);
    menu.onTick(ms += 500);
    CHECK(r.dash.card(1).find("\nResmi tatil") != std::string::npos);
    r.now = localTimeValue(2026, 10, 3, 12, 0, 0);  // Saturday
    menu.onTick(ms += 500);
    CHECK(r.dash.card(1).rfind(std::string("11") + sep + "Yemek" + sep + "Pzt 5\nKağıt kebabı", 0) == 0);
    CHECK(MenuService::cardDay(daysFromCivil(2026, 10, 4)) == daysFromCivil(2026, 10, 5));
    menu.setLanguage(1);
    CHECK(r.dash.card(1).rfind(std::string("11") + sep + "Menu" + sep + "Mon 5\n", 0) == 0);
    CHECK(r.dash.card(1).find("Salad bar: 6 items") != std::string::npos);
    CHECK(menu.detail(0)->find("Monday 5 October") != std::string::npos);

    // A changed file is read again after a minute; the open detail follows.
    r.dash.onConnected();
    r.dash.setDetailHandler([&menu](std::uint8_t, std::int8_t arg) { return menu.detail(arg); });
    ProtoMessage open = dashMessage(PROTO_CMD_OPEN_DETAIL, 4);
    open.u.open_detail.slot = 1;
    r.dash.onMessage(open);
    r.sink.sent.clear();
    files.files["menu-2026-10"] = std::string(kMonthFile).replace(std::string(kMonthFile).find("Kağıt kebabı"),
                                                                    std::string("Kağıt kebabı").size(), "Hünkar beğendi");
    menu.onTick(ms += 500);
    CHECK(r.sink.sent.empty());  // not yet
    menu.onTick(ms += MenuService::kFileIntervalMs);
    CHECK(!logs.empty() && logs.back() == "Menu files changed.");
    const std::string detailSent = popRecord(r.sink, PROTO_CMD_DETAIL_PART);
    CHECK(detailSent.find("Hünkar beğendi") != std::string::npos);
    CHECK(popRecord(r.sink, PROTO_CMD_CARD_PART).find("Hünkar beğendi") != std::string::npos);

    // A file of another month under this name is ignored with a warning.
    files.files["menu-2026-11"] = kMonthFile;
    r.now = localTimeValue(2026, 11, 2, 12, 0, 0);
    menu.onTick(ms += 500);
    CHECK(r.dash.card(1).find("No menu loaded") != std::string::npos);
    CHECK(logs.back().find("menu-2026-11.json is not a menu file") != std::string::npos);
}

void testClaudeHook()
{
    CHECK(permissionDecisionJson(HookAnswer::None).empty());
    const nlohmann::json allow = nlohmann::json::parse(permissionDecisionJson(HookAnswer::Allow));
    CHECK(allow["hookSpecificOutput"]["hookEventName"] == "PermissionRequest" &&
          allow["hookSpecificOutput"]["decision"]["behavior"] == "allow");
    const nlohmann::json deny = nlohmann::json::parse(permissionDecisionJson(HookAnswer::Deny));
    CHECK(deny["hookSpecificOutput"]["decision"]["behavior"] == "deny" &&
          deny["hookSpecificOutput"]["decision"].size() == 1U);

    HookProbeConfig c = parseHookProbeConfig(std::nullopt);
    CHECK(c.answer == HookAnswer::None && c.waitSeconds == 0 && c.exitCode == 0);
    c = parseHookProbeConfig(std::string("\xEF\xBB\xBF# probe\r\nanswer = deny\r\nwait=30\nexit=3\nother=1\n"));
    CHECK(c.answer == HookAnswer::Deny && c.waitSeconds == 30 && c.exitCode == 3);
    c = parseHookProbeConfig(std::string("answer=maybe\nwait=601\nexit=-1\nwait=x"));
    CHECK(c.answer == HookAnswer::None && c.waitSeconds == 0 && c.exitCode == 0);
    CHECK(std::string(hookAnswerName(HookAnswer::Allow)) == "allow");

    const std::string input = R"({"hook_event_name":"PermissionRequest","tool_name":"Bash","tool_input":{"command":"npm test"}})";
    const nlohmann::json start = nlohmann::json::parse(hookProbeLogLine("start", "2026-10-21 15:00:00.123", 0, 42, input, c));
    CHECK(start["phase"] == "start" && start["pid"] == 42 && start["input"]["tool_input"]["command"] == "npm test");
    const nlohmann::json raw = nlohmann::json::parse(hookProbeLogLine("start", "t", 0, 1, "not json\xFF", c));
    CHECK(raw.contains("input_raw") && !raw.contains("input"));
    c.answer = HookAnswer::Allow;
    c.waitSeconds = 5;
    const std::string endLine = hookProbeLogLine("end", "t", 5003, 42, input, c);
    CHECK(endLine.find('\n') == std::string::npos);
    const nlohmann::json end = nlohmann::json::parse(endLine);
    CHECK(end["answer"] == "allow" && end["wait_s"] == 5 && end["elapsed_ms"] == 5003 && !end.contains("input"));
}

void testHookPipeAndSnippet()
{
    const std::optional<std::string> line = hookRequestLine("{\n  \"tool_name\": \"Bash\",\n  \"x\": \"a\\nb\"\n}\n");
    CHECK(line.has_value() && line->back() == '\n' && line->find('\n') == line->size() - 1U);
    CHECK(line.has_value() && nlohmann::json::parse(*line)["x"] == "a\nb");
    CHECK(!hookRequestLine("[1]").has_value() && !hookRequestLine("").has_value() && !hookRequestLine("{").has_value());
    CHECK(hookReplyLine(HookAnswer::Allow) == "allow\n" && hookReplyLine(HookAnswer::None) == "none\n");
    CHECK(parseHookReply("allow\r\n") == HookAnswer::Allow && parseHookReply(" deny") == HookAnswer::Deny);
    CHECK(parseHookReply("") == HookAnswer::None && parseHookReply("Allow") == HookAnswer::None);

    const nlohmann::json snippet =
        nlohmann::json::parse(hookSettingsSnippet("C:\\Users\\e\\AppData\\Local\\Programs\\STM32 Desk Dashboard\\dashboard-host.exe"));
    const nlohmann::json& entry = snippet["hooks"]["PermissionRequest"][0];
    CHECK(entry["matcher"].get<std::string>().find("Bash") != std::string::npos);
    const nlohmann::json& hook = entry["hooks"][0];
    CHECK(hook["type"] == "command" && hook["timeout"] == kHookTimeoutSeconds);
    CHECK(hook["command"].get<std::string>().find("dashboard-host.exe") != std::string::npos);
    CHECK(hook["args"].is_array() && hook["args"].size() == 2U && hook["args"][1] == "permission");
    CHECK(!hook.contains("onFailure"));  // a closed tray must not refuse every request (ADR-007)
}

void testDashPrompt()
{
    DashRig r;
    r.dash.showPrompt(3, "before sync");  // kept, not sent
    CHECK(r.sink.sent.empty() && !r.dash.synced());
    r.dash.onConnected();
    CHECK(r.dash.synced());
    // TIME, 8 slots, then the kept prompt.
    while (!r.sink.sent.empty() && r.sink.sent.front().command != PROTO_CMD_PROMPT_PART) {
        r.sink.pop();
    }
    std::uint8_t id = 0;
    CHECK(popRecord(r.sink, PROTO_CMD_PROMPT_PART, &id) == "before sync" && id == 3U);
    r.dash.showPrompt(4, std::string(PROTO_PROMPT_MAX_BYTES + 50U, 'x'));
    CHECK(popRecord(r.sink, PROTO_CMD_PROMPT_PART, &id).size() == PROTO_PROMPT_MAX_BYTES && id == 4U);
    r.dash.closePrompt(4);
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_PROMPT_CLOSE && m.u.prompt_id == 4U);
    // After a GET_CARDS nothing is shown again.
    r.dash.onMessage(dashMessage(PROTO_CMD_GET_CARDS));
    bool prompt = false;
    while (!r.sink.sent.empty()) {
        prompt = prompt || r.sink.pop().command == PROTO_CMD_PROMPT_PART;
    }
    CHECK(!prompt);
}

std::string hookInput(const std::string& tool, const nlohmann::json& toolInput, const std::string& cwd = "C:\\src\\ayrac")
{
    nlohmann::json j = {{"session_id", "s1"},       {"hook_event_name", "PermissionRequest"}, {"cwd", cwd},
                        {"tool_name", tool},        {"tool_input", toolInput},               {"permission_mode", "default"}};
    return j.dump();
}

void testApprovalPrompt()
{
    const char sep = '\x1F';
    std::optional<ApprovalInfo> info =
        parseApprovalInput(hookInput("Bash", {{"command", "npm install\nnpm test"}, {"description", "Kur"}}), 0);
    CHECK(info.has_value() && info->tool == "Bash" && info->project == "ayrac" && info->rows.size() == 3U);
    CHECK(info->rows[1].label == "Komut" && info->rows[1].value == "npm install ¶ npm test" && info->rows[1].essential);
    CHECK(!info->rows[2].essential);
    bool approvable = false;
    std::string text = approvalPrompt(*info, 1, 2, 60, 0, approvable);
    CHECK(approvable);
    CHECK(text == std::string("Claude Code izin istiyor") + sep + "ayrac · 1/2" + sep + "60\n" + sep + "Araç" + sep +
                      "Bash\n" + sep + "Komut" + sep + "npm install ¶ npm test\n" + sep + "Açıklama" + sep + "Kur\n" +
                      "!1" + sep + "Onayla\n!2" + sep + "Reddet\n!3" + sep + "Terminalde sor");

    info = parseApprovalInput(hookInput("Edit", {{"file_path", "a.txt"}, {"old_string", "x"}, {"new_string", "y"},
                                                {"replace_all", true}}, "/home/e/proj/"),
                              1);
    CHECK(info.has_value() && info->project == "proj" && info->rows.size() == 5U && info->rows[4].label == "All");
    info = parseApprovalInput(hookInput("mcp__x__y", {{"a", 1}}), 0);
    CHECK(info.has_value() && info->rows.size() == 2U && info->rows[1].value == "{\"a\":1}");
    CHECK(!parseApprovalInput("{\"tool_input\":{}}", 0).has_value() && !parseApprovalInput("x", 0).has_value());

    // Long context only: dropped, still approvable.
    info = parseApprovalInput(hookInput("Bash", {{"command", "ls"}, {"description", std::string(1200, 'd')}}), 0);
    text = approvalPrompt(*info, 1, 1, 60, 0, approvable);
    CHECK(approvable && text.find("ddd") == std::string::npos && text.size() <= PROTO_PROMPT_MAX_BYTES);
    // A long command: cut, not approvable, no "Onayla".
    info = parseApprovalInput(hookInput("Write", {{"file_path", "big.ts"}, {"content", std::string(3000, 'c')}}), 0);
    text = approvalPrompt(*info, 1, 1, 60, 0, approvable);
    CHECK(!approvable && text.size() <= PROTO_PROMPT_MAX_BYTES);
    CHECK(text.find("!1") == std::string::npos && text.find("!2") != std::string::npos &&
          text.find("!3") != std::string::npos);
    CHECK(text.find("kısaltıldı") != std::string::npos && text.find("big.ts") != std::string::npos &&
          text.find("ccc") != std::string::npos);
}

void testApprovalService()
{
    DashRig r;
    std::vector<std::pair<std::uint64_t, HookAnswer>> replies;
    std::vector<std::string> logs;
    ApprovalService approvals(r.dash, [&r] { return r.now; },
                              [&replies](std::uint64_t t, HookAnswer a) { replies.emplace_back(t, a); },
                              [&logs](const std::string& l) { logs.push_back(l); });
    const std::string bash = hookInput("Bash", {{"command", "npm test"}});
    std::uint64_t ms = 1000;

    // No board: no decision at once.
    approvals.onRequest(1, bash, ms);
    CHECK(replies.size() == 1U && replies[0] == std::make_pair(std::uint64_t{1}, HookAnswer::None));
    CHECK(r.dash.card(3).find("kart yok") != std::string::npos);

    r.dash.onConnected();
    r.sink.sent.clear();
    approvals.onRequest(2, bash, ms);
    std::uint8_t id = 0;
    std::string shown = popRecord(r.sink, PROTO_CMD_PROMPT_PART, &id);
    const std::uint8_t first = id;
    CHECK(shown.find("1/1") != std::string::npos && shown.find("60\n") != std::string::npos);
    CHECK(r.dash.card(3).find("1 onay bekliyor") != std::string::npos);

    // A second request: the first is shown again as "1/2" with the time left.
    r.sink.sent.clear();
    approvals.onRequest(3, hookInput("Write", {{"file_path", "a.txt"}, {"content", "hi"}}), ms + 10000);
    shown = popRecord(r.sink, PROTO_CMD_PROMPT_PART, &id);
    CHECK(id == first && shown.find("1/2") != std::string::npos && shown.find("50\n") != std::string::npos);
    CHECK(approvals.waiting() == 2U);

    // "Onayla" on the board: allow, close, the next is shown.
    r.sink.sent.clear();
    ProtoAction a{};
    a.kind = PROTO_ACTION_PROMPT;
    a.ref = first;
    a.action = 1;
    CHECK(approvals.onAction(a, ms + 11000));
    CHECK(replies.back() == std::make_pair(std::uint64_t{2}, HookAnswer::Allow));
    ProtoMessage m = r.sink.pop();
    CHECK(m.command == PROTO_CMD_PROMPT_CLOSE && m.u.prompt_id == first);
    shown = popRecord(r.sink, PROTO_CMD_PROMPT_PART, &id);
    const std::uint8_t second = id;
    CHECK(second != first && shown.find("Write") != std::string::npos && shown.find("1/1") != std::string::npos);
    CHECK(approvals.onAction(a, ms + 11000) && replies.size() == 2U);  // stale answer: nothing

    // "Reddet"; then "Terminalde sor".
    a.ref = second;
    a.action = 2;
    CHECK(approvals.onAction(a, ms + 12000) && replies.back() == std::make_pair(std::uint64_t{3}, HookAnswer::Deny));
    approvals.onRequest(4, bash, ms + 13000);
    a.ref = static_cast<std::uint8_t>(second + 1U);
    a.action = 3;
    CHECK(approvals.onAction(a, ms + 14000) && replies.back() == std::make_pair(std::uint64_t{4}, HookAnswer::None));

    // A cut request: "allow" (not offered) counts as no decision.
    approvals.onRequest(5, hookInput("Write", {{"file_path", "b"}, {"content", std::string(2000, 'z')}}), ms + 15000);
    a.ref = static_cast<std::uint8_t>(second + 2U);
    a.action = 1;
    CHECK(approvals.onAction(a, ms + 16000) && replies.back() == std::make_pair(std::uint64_t{5}, HookAnswer::None));

    // Timeout, cancel, other kinds, a bad request.
    approvals.onRequest(6, bash, ms + 20000);
    approvals.onRequest(7, bash, ms + 21000);
    approvals.onTick(ms + 20000 + ApprovalService::kAnswerTimeoutMs - 1);
    CHECK(approvals.waiting() == 2U);
    approvals.onTick(ms + 20000 + ApprovalService::kAnswerTimeoutMs);
    CHECK(approvals.waiting() == 1U && replies.back() == std::make_pair(std::uint64_t{6}, HookAnswer::None));
    approvals.onCancelled(7, ms + 82000);
    CHECK(approvals.waiting() == 0U && replies.back() == std::make_pair(std::uint64_t{7}, HookAnswer::None));
    ProtoAction media{};
    media.kind = PROTO_ACTION_MEDIA;
    CHECK(!approvals.onAction(media, ms));
    approvals.onRequest(8, "not json", ms);
    CHECK(replies.back() == std::make_pair(std::uint64_t{8}, HookAnswer::None));

    // Full queue, then the board goes away: all "none".
    for (std::uint64_t t = 10; t < 10 + ApprovalService::kMaxQueue + 1; ++t) {
        approvals.onRequest(t, bash, ms + 90000);
    }
    CHECK(approvals.waiting() == ApprovalService::kMaxQueue &&
          replies.back() == std::make_pair(std::uint64_t{10 + ApprovalService::kMaxQueue}, HookAnswer::None));
    const std::size_t before = replies.size();
    approvals.onDisconnected();
    CHECK(approvals.waiting() == 0U && replies.size() == before + ApprovalService::kMaxQueue);
    for (std::size_t i = before; i < replies.size(); ++i) {
        CHECK(replies[i].second == HookAnswer::None);
    }

    // Card and detail.
    CHECK(r.dash.card(3).find("Onay bekleyen yok") != std::string::npos);
    const std::optional<std::string> detail = approvals.detail();
    CHECK(detail.has_value() && detail->find("#Son kararlar") != std::string::npos);
    CHECK(detail->find("kart yok · npm test") != std::string::npos && detail->size() <= PROTO_DETAIL_MAX_BYTES);
    approvals.setLanguage(1);
    CHECK(r.dash.card(3).find("No approval waiting") != std::string::npos);
    CHECK(!logs.empty());
}

// ------------------------------------------------------------ now playing

void testMediaCover()
{
    SquareCrop c = centerSquare(300, 200);
    CHECK(c.x == 50U && c.y == 0U && c.size == 200U);
    c = centerSquare(120, 160);
    CHECK(c.x == 0U && c.y == 20U && c.size == 120U);
    c = centerSquare(0, 10);
    CHECK(c.size == 0U);

    // B, G, R, A -> RGB565 little endian.
    const std::uint8_t px[] = {
        0x00, 0x00, 0xFF, 0xFF,  // red   -> 0xF800
        0x00, 0xFF, 0x00, 0x80,  // green -> 0x07E0 (alpha ignored)
        0xFF, 0x00, 0x00, 0x00,  // blue  -> 0x001F
        0xFF, 0xFF, 0xFF, 0xFF,  // white -> 0xFFFF
        0x08, 0x04, 0x10, 0xFF,  // r=0x10 g=0x04 b=0x08 -> (2<<11)|(1<<5)|1 = 0x1021
    };
    const std::vector<std::uint8_t> out = rgb565FromBgra(px, sizeof(px));
    const std::vector<std::uint8_t> expected = {0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF, 0x21, 0x10};
    CHECK(out == expected);
    CHECK(rgb565FromBgra(px, 3).empty());
    CHECK(rgb565FromBgra(nullptr, 4).empty());

    CHECK(mediaAppName("Spotify.exe") == "Spotify");
    CHECK(mediaAppName("C:\\Program Files\\Spotify\\Spotify.exe") == "Spotify");
    CHECK(mediaAppName("chrome") == "Chrome");
    CHECK(mediaAppName("MSEdge") == "Edge");
    CHECK(mediaAppName("308046B0AF4A39CB") == "Firefox");
    CHECK(mediaAppName("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == "Media Player");
    CHECK(mediaAppName("Contoso.Player_abc123!App") == "App");
    CHECK(mediaAppName("myplayer.exe") == "Myplayer");
    CHECK(mediaAppName("") == "");
}

struct FakeMediaSession : IMediaSession {
    MediaSnapshot snap;
    std::vector<MediaKey> commands;
    bool ok = true;
    MediaSnapshot snapshot() const override { return snap; }
    bool command(MediaKey key) override
    {
        commands.push_back(key);
        return ok;
    }
};

std::shared_ptr<const std::vector<std::uint8_t>> testCover(std::uint8_t seed)
{
    auto cover = std::make_shared<std::vector<std::uint8_t>>(PROTO_ART_BYTES);
    for (std::size_t i = 0; i < cover->size(); ++i) {
        (*cover)[i] = static_cast<std::uint8_t>(i * 3U + seed);
    }
    return cover;
}

struct MediaRig {
    FakeSink sink;
    FakeMediaSession session;
    FakeMediaKeys keys;
    MediaService media{sink, session, keys};

    std::size_t count(std::uint8_t command) const
    {
        std::size_t n = 0;
        for (const ProtoMessage& m : sink.sent) {
            n += m.command == command ? 1U : 0U;
        }
        return n;
    }
    std::optional<ProtoMediaState> lastState() const
    {
        std::optional<ProtoMediaState> s;
        for (const ProtoMessage& m : sink.sent) {
            if (m.command == PROTO_CMD_MEDIA_STATE) {
                s = m.u.media;
            }
        }
        return s;
    }
    // Every tick until the cover is out (at most @p ticks).
    void drainArt(std::uint64_t nowMs, int ticks = 200)
    {
        while (media.sendingArt() && ticks-- > 0) {
            media.onTick(nowMs);
        }
    }
};

void testMediaServiceConnect()
{
    MediaRig r;
    r.session.snap.status = PROTO_MEDIA_STATUS_PLAYING;
    r.session.snap.controls = PROTO_MEDIA_CAN_PREVIOUS | PROTO_MEDIA_CAN_PLAY_PAUSE | PROTO_MEDIA_CAN_NEXT;
    r.session.snap.title = "Şarkı";
    r.session.snap.artist = "Sanatçı";
    r.session.snap.album = "Albüm";
    r.session.snap.app = "Spotify";
    r.session.snap.positionS = 10;
    r.session.snap.durationS = 200;
    r.session.snap.sampledAtMs = 1000;
    r.session.snap.coverVersion = 1;
    r.session.snap.cover = testCover(1);

    // Nothing before the board is connected.
    r.media.onTick(1000);
    CHECK(r.sink.sent.empty());
    CHECK(!r.media.sendingArt());

    // Connect: the record, the state, then the first cover parts.
    r.media.onConnected(3500);
    CHECK(!r.sink.sent.empty() && r.sink.sent.front().command == PROTO_CMD_MEDIA_PART);
    std::string record;
    std::size_t i = 0;
    for (; i < r.sink.sent.size() && r.sink.sent[i].command == PROTO_CMD_MEDIA_PART; ++i) {
        const ProtoRecordPart& part = r.sink.sent[i].u.part;
        CHECK(part.id == 0U);
        record.append(reinterpret_cast<const char*>(part.data), part.len);
    }
    CHECK(record == recordLine({"Şarkı", "Sanatçı", "Albüm", "Spotify"}));
    CHECK(i < r.sink.sent.size() && r.sink.sent[i].command == PROTO_CMD_MEDIA_STATE);
    const ProtoMediaState st = r.sink.sent[i].u.media;
    CHECK(st.status == PROTO_MEDIA_STATUS_PLAYING && st.controls == 7U);
    CHECK(st.position == 12U && st.duration == 200U);  // 10 s + 2.5 s since the sample
    CHECK(st.art_id != PROTO_NO_ART);
    CHECK(r.count(PROTO_CMD_ART_PART) == MediaService::kArtPartsPerTick);
    CHECK(r.media.sendingArt());

    // The cover follows a few parts per tick, in order, complete.
    r.drainArt(3600);
    CHECK(!r.media.sendingArt());
    CHECK(r.count(PROTO_CMD_ART_PART) == PROTO_ART_PARTS);
    static std::uint8_t got[PROTO_ART_BYTES];
    ProtoArtAssembler a;
    Proto_ArtAssemblerInit(&a, got);
    Proto_ArtAssemblerStart(&a, st.art_id);
    ProtoAsmResult last = PROTO_ASM_PARTIAL;
    for (const ProtoMessage& m : r.sink.sent) {
        if (m.command == PROTO_CMD_ART_PART) {
            last = Proto_ArtAssemblerAdd(&a, &m.u.art);
        }
    }
    CHECK(last == PROTO_ASM_COMPLETE);
    CHECK(std::memcmp(got, r.session.snap.cover->data(), PROTO_ART_BYTES) == 0);
    CHECK(r.media.stats().coversSent == 1U);

    // Steady playback: nothing new until the 30 s refresh.
    r.sink.sent.clear();
    r.media.onTick(10000);
    CHECK(r.sink.sent.empty());
    r.media.onTick(3500 + MediaService::kStateIntervalMs);
    CHECK(r.count(PROTO_CMD_MEDIA_STATE) == 1U && r.sink.sent.size() == 1U);

    // GET_MEDIA sends everything again, the same cover id.
    r.sink.sent.clear();
    r.media.onRequest(40000);
    CHECK(r.count(PROTO_CMD_MEDIA_PART) >= 1U && r.count(PROTO_CMD_MEDIA_STATE) == 1U);
    CHECK(r.lastState()->art_id == st.art_id);
    r.drainArt(40000);
    CHECK(r.count(PROTO_CMD_ART_PART) == PROTO_ART_PARTS);

    // Disconnected: nothing, also no cover.
    r.media.onDisconnected();
    r.sink.sent.clear();
    r.session.snap.title = "Başka";
    r.media.onTick(41000);
    CHECK(r.sink.sent.empty() && !r.media.sendingArt());
}

void testMediaServiceChanges()
{
    MediaRig r;
    r.session.snap.status = PROTO_MEDIA_STATUS_PAUSED;
    r.session.snap.title = "A";
    r.session.snap.positionS = 30;
    r.session.snap.durationS = 100;
    r.session.snap.sampledAtMs = 0;
    r.media.onConnected(0);
    CHECK(r.lastState()->status == PROTO_MEDIA_STATUS_PAUSED && r.lastState()->art_id == PROTO_NO_ART);
    CHECK(!r.media.sendingArt());

    // Paused: the position does not move, nothing is sent.
    r.sink.sent.clear();
    r.media.onTick(60000);
    CHECK(r.sink.sent.empty());

    // Play: a new state at once.
    r.session.snap.status = PROTO_MEDIA_STATUS_PLAYING;
    r.session.snap.sampledAtMs = 60000;
    r.media.onTick(60000);
    CHECK(r.count(PROTO_CMD_MEDIA_STATE) == 1U && r.count(PROTO_CMD_MEDIA_PART) == 0U);
    CHECK(r.lastState()->status == PROTO_MEDIA_STATUS_PLAYING && r.lastState()->position == 30U);

    // The reader's position follows the clock: no state.
    r.sink.sent.clear();
    r.session.snap.positionS = 35;
    r.session.snap.sampledAtMs = 65000;
    r.media.onTick(65500);
    CHECK(r.sink.sent.empty());

    // A seek (more than 2 s off): a state.
    r.session.snap.positionS = 80;
    r.session.snap.sampledAtMs = 66000;
    r.media.onTick(66000);
    CHECK(r.count(PROTO_CMD_MEDIA_STATE) == 1U && r.lastState()->position == 80U);

    // Counting past the end stops at the duration.
    r.sink.sent.clear();
    r.session.snap.positionS = 99;
    r.session.snap.sampledAtMs = 85000;
    r.media.onTick(95000);  // 99 + 10 s
    CHECK(r.sink.sent.empty() || r.lastState()->position <= 100U);

    // New track with a cover: the record, the state with a new cover id, the cover.
    r.sink.sent.clear();
    r.session.snap.title = "B";
    r.session.snap.positionS = 0;
    r.session.snap.sampledAtMs = 96000;
    r.session.snap.coverVersion = 5;
    r.session.snap.cover = testCover(2);
    r.media.onTick(96000);
    CHECK(r.count(PROTO_CMD_MEDIA_PART) == 1U && r.count(PROTO_CMD_MEDIA_STATE) == 1U);
    CHECK(r.sink.sent.front().command == PROTO_CMD_MEDIA_PART);
    const std::uint8_t first = r.lastState()->art_id;
    CHECK(first != PROTO_NO_ART && r.media.sendingArt());

    // Another cover while the first is being sent: a new id, sent from part 0.
    r.session.snap.coverVersion = 6;
    r.session.snap.cover = testCover(3);
    r.sink.sent.clear();
    r.media.onTick(96100);
    CHECK(r.lastState()->art_id != first && r.lastState()->art_id != PROTO_NO_ART);
    bool fromZero = false;
    for (const ProtoMessage& m : r.sink.sent) {
        if (m.command == PROTO_CMD_ART_PART) {
            fromZero = m.u.art.index == 0U && m.u.art.art_id == r.lastState()->art_id;
            break;
        }
    }
    CHECK(fromZero);

    // A track without a cover: art_id 0, the cover stops.
    r.session.snap.coverVersion = 7;
    r.session.snap.cover.reset();
    r.sink.sent.clear();
    r.media.onTick(96200);
    CHECK(r.lastState()->art_id == PROTO_NO_ART);
    CHECK(r.count(PROTO_CMD_ART_PART) == 0U && !r.media.sendingArt());

    // No session: status none.
    r.session.snap = MediaSnapshot{};
    r.session.snap.coverVersion = 8;
    r.sink.sent.clear();
    r.media.onTick(97000);
    CHECK(r.lastState()->status == PROTO_MEDIA_STATUS_NONE);
    CHECK(r.media.stats().sendFailures == 0U);

    // Cover ids run 1..255 and skip 0.
    MediaRig ids;
    ids.media.onConnected(0);
    std::uint8_t previous = 0;
    bool neverZero = true;
    for (std::uint32_t v = 1; v <= 300; ++v) {
        ids.session.snap.coverVersion = v;
        ids.session.snap.cover = testCover(0);
        ids.media.onTick(v);
        neverZero = neverZero && ids.media.artId() != PROTO_NO_ART && ids.media.artId() != previous;
        previous = ids.media.artId();
    }
    CHECK(neverZero);
}

void testMediaServiceActions()
{
    MediaRig r;
    ProtoAction a{};
    a.kind = PROTO_ACTION_MEDIA;
    a.action = PROTO_MEDIA_NEXT;

    // No session: the media keys.
    CHECK(r.media.onAction(a));
    CHECK(r.keys.pressed.size() == 1U && r.keys.pressed[0] == MediaKey::Next && r.session.commands.empty());

    // A session: its command, no key.
    r.session.snap.status = PROTO_MEDIA_STATUS_PAUSED;
    a.action = PROTO_MEDIA_PLAY_PAUSE;
    CHECK(r.media.onAction(a));
    CHECK(r.session.commands.size() == 1U && r.session.commands[0] == MediaKey::PlayPause);
    CHECK(r.keys.pressed.size() == 1U);

    // The session refuses: the key.
    r.session.ok = false;
    a.action = PROTO_MEDIA_PREVIOUS;
    CHECK(r.media.onAction(a));
    CHECK(r.keys.pressed.size() == 2U && r.keys.pressed[1] == MediaKey::Previous);

    // Not the media strip, or an unknown action: false.
    a.kind = PROTO_ACTION_PROMPT;
    CHECK(!r.media.onAction(a));
    a.kind = PROTO_ACTION_MEDIA;
    a.action = 9;
    CHECK(!r.media.onAction(a));

    // GET_MEDIA through DashController: the handler, or ERROR without one.
    DashRig d;
    d.dash.onConnected();
    d.sink.sent.clear();
    d.dash.onMessage(dashMessage(PROTO_CMD_GET_MEDIA, 4));
    ProtoMessage m = d.sink.pop();
    CHECK(m.command == PROTO_CMD_ERROR && m.u.error.code == PROTO_ERROR_OPERATION_FAILED &&
          m.u.error.ref_command == PROTO_CMD_GET_MEDIA);
    int requests = 0;
    d.dash.setMediaRequestHandler([&requests] { ++requests; });
    d.dash.onMessage(dashMessage(PROTO_CMD_GET_MEDIA, 5));
    CHECK(requests == 1 && d.sink.sent.empty());

    // The router sends GET_MEDIA to the dashboard side.
    CHECK(FrameRouter::isDashboardCommand(PROTO_CMD_GET_MEDIA));

    // MediaChanged is merged like the other state events.
    EventQueue q;
    q.push(HostEvent::MediaChanged);
    q.push(HostEvent::MediaChanged);
    CHECK(q.size() == 1U);

    // Log lines.
    ProtoMessage s{};
    s.command = PROTO_CMD_MEDIA_STATE;
    s.u.media.status = PROTO_MEDIA_STATUS_PLAYING;
    s.u.media.controls = 7;
    s.u.media.art_id = 3;
    s.u.media.position = 12;
    s.u.media.duration = 200;
    CHECK(describeMessage(s) == "MEDIA_STATE seq=0 status=playing controls=7 art=3 pos=12/200");
    s = ProtoMessage{};
    s.command = PROTO_CMD_ART_PART;
    s.u.art.art_id = 3;
    s.u.art.index = 533;
    CHECK(describeMessage(s) == "ART_PART seq=0 art=3 part=534/534");
    s = ProtoMessage{};
    s.command = PROTO_CMD_GET_MEDIA;
    CHECK(describeMessage(s) == "GET_MEDIA seq=0");
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
    testRecordText();
    testLocalTimeValue();
    testSequencedSink();
    testDashNothingBeforeSync();
    testDashGetCards();
    testDashCardUpdates();
    testDashDetailAndAction();
    testDashTime();
    testFrameRouter();
    testFrameTextDashboard();
    testMediaKeys();
    testWeatherParse();
    testWeatherCodes();
    testWeatherLocation();
    testWeatherCard();
    testBackgroundFetch();
    testWeatherService();
    testMenuDates();
    testMenuMonthFile();
    testMenuMergeAndSummary();
    testMenuRequest();
    testMenuAnswer();
    testMenuImport();
    testMenuService();
    testClaudeHook();
    testHookPipeAndSnippet();
    testDashPrompt();
    testApprovalPrompt();
    testApprovalService();
    testEventQueueHook();
    testMediaCover();
    testMediaServiceConnect();
    testMediaServiceChanges();
    testMediaServiceActions();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
