// Which HID interface is the controller (docs/decisions/ADR-003).
// Portable so the rule can be unit tested; the Windows layer fills in
// HidInfo from HidD_GetAttributes and HidP_GetCaps.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

#include "protocol.h"

namespace stm32_audio::host {

constexpr std::uint16_t kControllerVid = 0x1209;
constexpr std::uint16_t kControllerPid = 0x0002;  // 0x0001 is stm32-audio-controller
constexpr std::uint16_t kControllerUsagePage = 0xFF00;
constexpr std::uint16_t kControllerUsage = 0x0001;
// USB string descriptor index of the configuration string, which carries
// the firmware version (ADR-011).
constexpr std::uint8_t kVersionStringIndex = 4;  // USBD_IDX_CONFIG_STR
// Windows report length: report ID byte (always 0) + one protocol frame.
constexpr std::uint16_t kReportBytes = PROTO_FRAME_SIZE + 1U;

struct HidInfo {
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::uint16_t inputReportBytes = 0;   // HIDP_CAPS::InputReportByteLength
    std::uint16_t outputReportBytes = 0;  // HIDP_CAPS::OutputReportByteLength
};

inline bool isController(const HidInfo& info)
{
    return info.vendorId == kControllerVid && info.productId == kControllerPid &&
           info.usagePage == kControllerUsagePage && info.usage == kControllerUsage &&
           info.inputReportBytes == kReportBytes && info.outputReportBytes == kReportBytes;
}

// The board's firmware version and protocol version from the USB
// configuration string "Desk Dashboard fw <version> p<protocol>"
// (ADR-011). Older firmware has another string: both stay empty / 0.
struct BoardVersion {
    std::string firmware;      // e.g. "0.9.0", "dev+1a2b3c4"; empty: unknown
    unsigned protocol = 0;     // PROTO_VERSION of the firmware; 0: unknown
};

inline BoardVersion parseBoardVersion(const std::string& text)
{
    static const std::string kPrefix = "Desk Dashboard fw ";
    BoardVersion v;
    if (text.compare(0, kPrefix.size(), kPrefix) != 0) {
        return v;
    }
    std::string rest = text.substr(kPrefix.size());
    const std::size_t p = rest.rfind(" p");
    if (p != std::string::npos && p + 2U < rest.size()) {
        char* end = nullptr;
        const unsigned long n = std::strtoul(rest.c_str() + p + 2U, &end, 10);
        if (end != nullptr && *end == '\0' && n > 0UL && n < 256UL) {
            v.protocol = static_cast<unsigned>(n);
            rest.resize(p);
        }
    }
    v.firmware = rest;
    return v;
}

}  // namespace stm32_audio::host
