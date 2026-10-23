#include "DashController.h"

#include <utility>

namespace stm32_audio::host {

namespace {

bool isContinuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d)
{
    // Howard Hinnant's days_from_civil: days since 1970-01-01.
    y -= m <= 2U ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153U * (m + (m > 2U ? 0U : 12U) - 3U) + 2U) / 5U + d - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

}  // namespace

std::string recordLine(std::initializer_list<std::string_view> fields)
{
    std::string out;
    bool first = true;

    for (std::string_view field : fields) {
        if (!first) {
            out += static_cast<char>(PROTO_FIELD_SEPARATOR);
        }
        first = false;
        for (char c : field) {
            const bool special = (c == '\n') || (c == '\r') || (c == '\0') ||
                                 (static_cast<unsigned char>(c) == PROTO_FIELD_SEPARATOR);
            out += special ? ' ' : c;
        }
    }
    return out;
}

std::string cutUtf8(std::string text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes) {
        return text;
    }
    std::size_t end = maxBytes;
    while ((end > 0U) && isContinuation(text[end])) {
        --end;
    }
    text.resize(end);
    return text;
}

std::uint32_t localTimeValue(int year, int month, int day, int hour, int minute, int second)
{
    const std::int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t secs = days * 86400 + static_cast<std::int64_t>(hour) * 3600 +
                              static_cast<std::int64_t>(minute) * 60 + second;
    return secs < 0 ? 0U : static_cast<std::uint32_t>(secs);
}

DashController::DashController(IFrameSink& sink, ClockFn clock) : sink_(sink), clock_(std::move(clock)) {}

void DashController::onConnected()
{
    sendAll();
}

void DashController::onDisconnected()
{
    synced_ = false;
    openSlot_.reset();
}

void DashController::onMessage(const ProtoMessage& msg)
{
    switch (msg.command) {
    case PROTO_CMD_GET_CARDS:
        sendAll();
        break;
    case PROTO_CMD_OPEN_DETAIL:
        if (!sendDetail(msg.u.open_detail.slot, msg.u.open_detail.arg)) {
            sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        }
        break;
    case PROTO_CMD_ACTION:
        if (!action_ || !action_(msg.u.action)) {
            sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        }
        break;
    case PROTO_CMD_GET_MEDIA:
        if (mediaRequest_) {
            mediaRequest_();
        } else {
            sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        }
        break;
    case PROTO_CMD_SEEK:
        if (!seek_ || !seek_(msg.u.seek_s)) {
            sendError(PROTO_ERROR_OPERATION_FAILED, msg.command, msg.sequence);
        }
        break;
    case PROTO_CMD_ERROR:
        ++stats_.errorsReceived;
        break;
    default:
        break;  // host -> STM32 commands coming the wrong way: drop
    }
}

void DashController::onTick(std::uint64_t nowMs)
{
    lastTick_ = nowMs;
    if (!synced_) {
        return;
    }
    // A card whose frames could not all be written (the board held the
    // host off too long) goes out again, else it would wait for its next
    // change (#68: the weather card stayed "Bekleniyor").
    for (std::uint8_t slot = 0; slot < PROTO_MAX_CARDS; ++slot) {
        if (cards_[slot].unsent) {
            (void)sendCard(slot);
        }
    }
    if (!timeSentAtMs_) {
        timeSentAtMs_ = nowMs;  // the time went out before the first tick
    }
    const std::uint64_t elapsedMs = nowMs - *timeSentAtMs_;
    if (elapsedMs >= kTimeIntervalMs) {
        sendTime(nowMs);
        return;
    }
    // The board counts on from the last TIME; a jump of the local clock
    // (time zone, daylight saving, the user setting it) is sent at once.
    const std::uint32_t expected = timeSent_ + static_cast<std::uint32_t>(elapsedMs / 1000U);
    const std::uint32_t actual = clock_();
    const std::uint32_t diff = actual > expected ? actual - expected : expected - actual;
    if (diff > kClockJumpSeconds) {
        sendTime(nowMs);
    }
}

void DashController::setCard(std::uint8_t slot, std::string text)
{
    if (slot >= PROTO_MAX_CARDS) {
        return;
    }
    text = cutUtf8(std::move(text), PROTO_CARD_MAX_BYTES);
    Slot& s = cards_[slot];
    if (text == s.text) {
        return;
    }
    s.text = std::move(text);
    ++s.generation;
    if (synced_) {
        sendCard(slot);
    }
}

void DashController::refreshDetail(std::uint8_t slot)
{
    if (synced_ && openSlot_ && (*openSlot_ == slot)) {
        (void)sendDetail(slot, 0);
    }
}

void DashController::sendAll()
{
    synced_ = true;
    sendTime(lastTick_);
    for (std::uint8_t slot = 0; slot < PROTO_MAX_CARDS; ++slot) {
        sendCard(slot);
    }
    sendIndicators();
    if (cardsSent_) {
        cardsSent_();
    }
    if (prompt_) {
        sendRecord(PROTO_CMD_PROMPT_PART, prompt_->id, prompt_->generation, prompt_->text);
    }
}

void DashController::showPrompt(std::uint8_t id, std::string text)
{
    Prompt p;
    p.id = id;
    p.generation = ++promptGeneration_;
    p.text = cutUtf8(std::move(text), PROTO_PROMPT_MAX_BYTES);
    prompt_ = std::move(p);
    if (synced_) {
        sendRecord(PROTO_CMD_PROMPT_PART, prompt_->id, prompt_->generation, prompt_->text);
    }
}

void DashController::closePrompt(std::uint8_t id)
{
    if (prompt_ && prompt_->id == id) {
        prompt_.reset();
    }
    if (synced_) {
        ProtoMessage msg{};
        msg.command = PROTO_CMD_PROMPT_CLOSE;
        msg.u.prompt_id = id;
        send(msg);
    }
}

void DashController::setIndicators(const ProtoIndicators& indicators)
{
    ProtoIndicators next{};
    next.count = indicators.count > PROTO_MAX_INDICATORS ? PROTO_MAX_INDICATORS : indicators.count;
    for (std::uint8_t i = 0; i < next.count; ++i) {
        next.items[i] = indicators.items[i];
    }
    bool same = next.count == indicators_.count;
    for (std::uint8_t i = 0; same && i < next.count; ++i) {
        const ProtoIndicator& a = next.items[i];
        const ProtoIndicator& b = indicators_.items[i];
        same = a.icon == b.icon && a.colour == b.colour && a.number == b.number && a.slot == b.slot;
    }
    if (same) {
        return;
    }
    indicators_ = next;
    if (synced_) {
        sendIndicators();
    }
}

void DashController::sendIndicators()
{
    ProtoMessage msg{};
    msg.command = PROTO_CMD_INDICATORS;
    msg.u.indicators = indicators_;
    send(msg);
}

void DashController::showBanner(std::uint8_t id, std::string text)
{
    if (!synced_) {
        return;
    }
    sendRecord(PROTO_CMD_BANNER_PART, id, ++bannerGeneration_, cutUtf8(std::move(text), PROTO_BANNER_MAX_BYTES));
}

bool DashController::sendCard(std::uint8_t slot)
{
    Slot& s = cards_[slot];

    if (s.text.empty()) {
        ProtoMessage msg{};
        msg.command = PROTO_CMD_CARD_CLEAR;
        msg.u.slot = slot;
        s.unsent = !send(msg);
    } else {
        s.unsent = !sendRecord(PROTO_CMD_CARD_PART, slot, s.generation, s.text);
    }
    return !s.unsent;
}

void DashController::sendTime(std::optional<std::uint64_t> nowMs)
{
    ProtoMessage msg{};
    msg.command = PROTO_CMD_TIME;
    msg.u.local_time = clock_();
    timeSent_ = msg.u.local_time;
    timeSentAtMs_ = nowMs;
    ++stats_.timesSent;
    send(msg);
}

bool DashController::sendDetail(std::uint8_t slot, std::int8_t arg)
{
    if (!detail_) {
        return false;
    }
    const std::optional<std::string> text = detail_(slot, arg);
    if (!text) {
        return false;
    }
    openSlot_ = slot;
    ++detailGeneration_;
    sendRecord(PROTO_CMD_DETAIL_PART, slot, detailGeneration_, cutUtf8(*text, PROTO_DETAIL_MAX_BYTES));
    return true;
}

bool DashController::sendRecord(std::uint8_t command, std::uint8_t id, std::uint8_t generation,
                                const std::string& text)
{
    // NUL bytes would make the record invalid; they cannot come from
    // recordLine, but a feature may build text by hand.
    std::string clean = text;
    for (char& c : clean) {
        if (c == '\0') {
            c = ' ';
        }
    }
    const auto length = static_cast<std::uint16_t>(clean.size());
    const std::uint8_t parts = Proto_RecordPartCount(length);
    bool all = true;
    for (std::uint8_t part = 0; part < parts; ++part) {
        ProtoMessage msg{};
        msg.command = command;
        if (Proto_RecordPart(reinterpret_cast<const std::uint8_t*>(clean.data()), length, id, generation, part,
                             &msg.u.part) == 0) {
            ++stats_.sendFailures;
            return false;
        }
        all = send(msg) && all;
    }
    return all;
}

void DashController::sendError(ProtoErrorCode code, std::uint8_t refCommand, std::uint8_t refSequence)
{
    ProtoMessage msg{};
    msg.command = PROTO_CMD_ERROR;
    msg.u.error.code = static_cast<std::uint8_t>(code);
    msg.u.error.ref_command = refCommand;
    msg.u.error.ref_sequence = refSequence;
    send(msg);
}

bool DashController::send(const ProtoMessage& msg)
{
    Frame frame{};
    if (Proto_Encode(&msg, frame.data()) != PROTO_OK) {
        // Only reachable through a bug in this class; never send garbage.
        ++stats_.sendFailures;
        return false;
    }
    if (sink_.sendFrame(frame)) {
        ++stats_.framesSent;
        return true;
    }
    ++stats_.sendFailures;
    return false;
}

}  // namespace stm32_audio::host
