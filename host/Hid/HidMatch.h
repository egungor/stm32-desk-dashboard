// Which HID interface is the controller (docs/decisions/ADR-003).
// Portable so the rule can be unit tested; the Windows layer fills in
// HidInfo from HidD_GetAttributes and HidP_GetCaps.
#pragma once

#include <cstdint>

#include "protocol.h"

namespace stm32_audio::host {

constexpr std::uint16_t kControllerVid = 0x1209;
constexpr std::uint16_t kControllerPid = 0x0002;  // 0x0001 is stm32-audio-controller
constexpr std::uint16_t kControllerUsagePage = 0xFF00;
constexpr std::uint16_t kControllerUsage = 0x0001;
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

}  // namespace stm32_audio::host
