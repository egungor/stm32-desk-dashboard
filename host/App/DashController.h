// Dashboard side of protocol v3 on the host (ADR-006; protocol.md,
// "Dashboard records" and "Exchanges").
//
// Holds the text record of every card slot and sends it to the board:
// all cards (and the time) after GET_CARDS or a new connection, one card
// when it changes. Details and actions are passed to handlers set by the
// features (weather, menu, ...); without a handler the board gets
// ERROR OPERATION_FAILED. The time goes out on connect, every 10 minutes
// and when the local clock jumps (time zone, daylight saving, manual
// change).
//
// Platform independent; the Windows layer supplies the local time and
// calls onTick() from the thread that also passes the frames. Not
// thread-safe.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "IFrameSink.h"
#include "protocol.h"

namespace stm32_audio::host {

// ------------------------------------------------------------ record text

// One line of a record: the fields joined by PROTO_FIELD_SEPARATOR. Line
// ends, separators, NUL and CR inside a field become a space (protocol.md,
// "Record text").
std::string recordLine(std::initializer_list<std::string_view> fields);

// @p text cut to at most @p maxBytes bytes, never inside a UTF-8 character.
std::string cutUtf8(std::string text, std::size_t maxBytes);

// Seconds since 1970-01-01 00:00 of a local date and time, counted as if
// it were UTC (the value of TIME). Valid for 1970..2105.
std::uint32_t localTimeValue(int year, int month, int day, int hour, int minute, int second);

// ---------------------------------------------------------------- controller

class DashController {
public:
    struct Stats {
        unsigned framesSent = 0;
        unsigned sendFailures = 0;
        unsigned timesSent = 0;
        unsigned errorsReceived = 0;
    };

    // Local time now, as localTimeValue().
    using ClockFn = std::function<std::uint32_t()>;
    // The detail of a card slot (arg: 0, or -1/+1 for previous/next), or
    // nullopt if the slot has none.
    using DetailFn = std::function<std::optional<std::string>(std::uint8_t slot, std::int8_t arg)>;
    // A button of a detail or the media strip; false: could not be done.
    using ActionFn = std::function<bool(const ProtoAction& action)>;

    static constexpr std::uint64_t kTimeIntervalMs = 10U * 60U * 1000U;
    static constexpr std::uint32_t kClockJumpSeconds = 2U;

    DashController(IFrameSink& sink, ClockFn clock);

    // A decoded dashboard message (0x20..0x3F) or an ERROR about one.
    void onMessage(const ProtoMessage& msg);

    // The HID device was opened: send the time and every card without
    // waiting for GET_CARDS (as HostController does with the list).
    void onConnected();
    // The HID device went away: send nothing until the next connect or
    // GET_CARDS.
    void onDisconnected();

    // Call regularly (e.g. every 500 ms) with a monotonic clock.
    void onTick(std::uint64_t nowMs);

    // Set the record of a card slot (0..PROTO_MAX_CARDS-1); "" empties it.
    // Longer records are cut to PROTO_CARD_MAX_BYTES. Sent at once if the
    // board is synced and the text changed.
    void setCard(std::uint8_t slot, std::string text);
    const std::string& card(std::uint8_t slot) const { return cards_.at(slot).text; }

    // The detail of @p slot changed on the host: send it again if it is the
    // one the board has open.
    void refreshDetail(std::uint8_t slot);

    // Show prompt @p id on the board (PROMPT_PART, ADR-007), replacing the
    // one shown. Cut to PROTO_PROMPT_MAX_BYTES. Kept and sent again after a
    // GET_CARDS; sent at once if the board is synced.
    void showPrompt(std::uint8_t id, std::string text);
    // Close prompt @p id (PROMPT_CLOSE) and forget it.
    void closePrompt(std::uint8_t id);

    // The board asked for the cards since it was connected: frames reach it.
    bool synced() const { return synced_; }

    void setDetailHandler(DetailFn handler) { detail_ = std::move(handler); }
    void setActionHandler(ActionFn handler) { action_ = std::move(handler); }

    const Stats& stats() const { return stats_; }

private:
    struct Slot {
        std::string text;
        std::uint8_t generation = 0;
    };

    void sendAll();
    void sendCard(std::uint8_t slot);
    void sendTime(std::optional<std::uint64_t> nowMs);
    bool sendDetail(std::uint8_t slot, std::int8_t arg);
    void sendRecord(std::uint8_t command, std::uint8_t id, std::uint8_t generation, const std::string& text);
    void sendError(ProtoErrorCode code, std::uint8_t refCommand, std::uint8_t refSequence);
    void send(const ProtoMessage& msg);

    IFrameSink& sink_;
    ClockFn clock_;
    DetailFn detail_;
    ActionFn action_;
    Stats stats_;
    std::array<Slot, PROTO_MAX_CARDS> cards_{};
    bool synced_ = false;
    std::optional<std::uint64_t> lastTick_;
    std::optional<std::uint64_t> timeSentAtMs_;  // nullopt: sent before the first tick
    std::uint32_t timeSent_ = 0;
    std::optional<std::uint8_t> openSlot_;  // detail the board asked for last
    struct Prompt {
        std::uint8_t id = 0;
        std::uint8_t generation = 0;
        std::string text;
    };
    std::optional<Prompt> prompt_;
    std::uint8_t promptGeneration_ = 0;
    std::uint8_t detailGeneration_ = 0;
};

}  // namespace stm32_audio::host
