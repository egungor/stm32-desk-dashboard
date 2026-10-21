#include "ToastText.h"

#include <cstdlib>

namespace stm32_audio::host {

namespace {

void appendUtf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80UL) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800UL) {
        out += static_cast<char>(0xC0UL | (cp >> 6));
        out += static_cast<char>(0x80UL | (cp & 0x3FUL));
    } else if (cp < 0x10000UL) {
        out += static_cast<char>(0xE0UL | (cp >> 12));
        out += static_cast<char>(0x80UL | ((cp >> 6) & 0x3FUL));
        out += static_cast<char>(0x80UL | (cp & 0x3FUL));
    } else if (cp < 0x110000UL) {
        out += static_cast<char>(0xF0UL | (cp >> 18));
        out += static_cast<char>(0x80UL | ((cp >> 12) & 0x3FUL));
        out += static_cast<char>(0x80UL | ((cp >> 6) & 0x3FUL));
        out += static_cast<char>(0x80UL | (cp & 0x3FUL));
    }
}

// Decode the entities of @p raw and fold whitespace.
std::string cleanText(std::string_view raw)
{
    std::string decoded;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            decoded += raw[i];
            continue;
        }
        const std::size_t semi = raw.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10U) {
            decoded += '&';
            continue;
        }
        const std::string_view name = raw.substr(i + 1U, semi - i - 1U);
        if (name == "amp") {
            decoded += '&';
        } else if (name == "lt") {
            decoded += '<';
        } else if (name == "gt") {
            decoded += '>';
        } else if (name == "quot") {
            decoded += '"';
        } else if (name == "apos") {
            decoded += '\'';
        } else if (name.size() > 1U && name[0] == '#') {
            const std::string digits(name.substr(1U));
            const bool hex = !digits.empty() && (digits[0] == 'x' || digits[0] == 'X');
            const unsigned long cp = std::strtoul(digits.c_str() + (hex ? 1 : 0), nullptr, hex ? 16 : 10);
            appendUtf8(decoded, cp);
        } else {
            decoded += '&';
            continue;
        }
        i = semi;
    }

    std::string out;
    bool space = false;
    for (char c : decoded) {
        const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (ws) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += c;
    }
    return out;
}

}  // namespace

std::vector<std::string> toastTexts(std::string_view xml)
{
    std::vector<std::string> texts;
    std::size_t pos = 0;
    for (;;) {
        pos = xml.find("<text", pos);
        if (pos == std::string_view::npos) {
            break;
        }
        const std::size_t after = pos + 5U;
        // "<text>" or "<text ...>", not "<textBox" etc.
        if (after >= xml.size() || (xml[after] != '>' && xml[after] != ' ' && xml[after] != '/' &&
                                    xml[after] != '\t' && xml[after] != '\n' && xml[after] != '\r')) {
            pos = after;
            continue;
        }
        const std::size_t close = xml.find('>', after);
        if (close == std::string_view::npos) {
            break;
        }
        if (xml[close - 1U] == '/') {  // <text/>
            pos = close + 1U;
            continue;
        }
        const std::size_t end = xml.find("</text>", close + 1U);
        if (end == std::string_view::npos) {
            break;
        }
        std::string text = cleanText(xml.substr(close + 1U, end - close - 1U));
        if (!text.empty()) {
            texts.push_back(std::move(text));
        }
        pos = end + 7U;
    }
    return texts;
}

std::int64_t unixFromFiletime(std::int64_t filetime)
{
    constexpr std::int64_t kEpochDiff = 11644473600LL;  // seconds 1601 -> 1970
    const std::int64_t seconds = filetime / 10000000LL - kEpochDiff;
    return seconds < 0 ? 0 : seconds;
}

}  // namespace stm32_audio::host
