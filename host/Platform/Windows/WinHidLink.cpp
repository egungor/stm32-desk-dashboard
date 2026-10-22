#include "WinHidLink.h"

// Some SDKs (MinGW-w64) declare the HID functions without extern "C".
extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>

#include <cstring>
#include <vector>

#include "HidMatch.h"
#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

// Reads the identity of one HID interface. Opened without read/write
// access, which also works for interfaces another program (or Windows,
// for keyboards and mice) holds exclusively.
bool queryInfo(const wchar_t* path, HidInfo& info, std::string& product)
{
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = false;
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetAttributes(h, &attributes) && HidD_GetPreparsedData(h, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            info.vendorId = attributes.VendorID;
            info.productId = attributes.ProductID;
            info.usagePage = caps.UsagePage;
            info.usage = caps.Usage;
            info.inputReportBytes = caps.InputReportByteLength;
            info.outputReportBytes = caps.OutputReportByteLength;
            ok = true;
        }
        HidD_FreePreparsedData(preparsed);
    }

    wchar_t name[128] = {};
    if (ok && HidD_GetProductString(h, name, static_cast<ULONG>(sizeof(name) - sizeof(wchar_t)))) {
        product = toUtf8(name);
    }
    CloseHandle(h);
    return ok;
}

// Calls @p visit with the device path of every present HID interface
// until it returns true.
template <typename Visit>
void forEachHidPath(Visit visit)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return;
    }
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<std::uint8_t> buffer(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr) &&
            visit(detail->DevicePath)) {
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

BoardVersion readBoardVersion(HANDLE device)
{
    wchar_t text[128] = {};
    if (!HidD_GetIndexedString(device, kVersionStringIndex, text, static_cast<ULONG>(sizeof(text) - sizeof(wchar_t)))) {
        return {};
    }
    return parseBoardVersion(toUtf8(text));
}

}  // namespace

std::optional<BoardVersion> findBoardVersion()
{
    std::optional<BoardVersion> found;
    forEachHidPath([&found](const wchar_t* path) {
        HidInfo info;
        std::string product;
        if (!queryInfo(path, info, product) || !isController(info)) {
            return false;
        }
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        found = BoardVersion{};
        if (h != INVALID_HANDLE_VALUE) {
            found = readBoardVersion(h);
            CloseHandle(h);
        }
        return true;
    });
    return found;
}

WinHidLink::WinHidLink(EventQueue& events) : events_(events) {}

WinHidLink::~WinHidLink()
{
    stop();
    if (writeEvent_ != nullptr) {
        CloseHandle(writeEvent_);
    }
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
    }
}

bool WinHidLink::start()
{
    if (thread_.joinable()) {
        return true;
    }
    if (stopEvent_ == nullptr) {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    if (writeEvent_ == nullptr) {
        writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    if (stopEvent_ == nullptr || writeEvent_ == nullptr) {
        return false;
    }
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] { run(); });
    return true;
}

void WinHidLink::stop()
{
    if (!thread_.joinable()) {
        return;
    }
    SetEvent(stopEvent_);
    thread_.join();
}

std::string WinHidLink::description() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return description_;
}

BoardVersion WinHidLink::boardVersion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return boardVersion_;
}

bool WinHidLink::sendFrame(const Frame& frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Report ID 0 (the report descriptor has none), then the frame.
    std::uint8_t report[kReportBytes] = {};
    std::memcpy(report + 1, frame.data(), frame.size());

    OVERLAPPED ov{};
    ov.hEvent = writeEvent_;
    ResetEvent(writeEvent_);
    DWORD written = 0;
    if (!WriteFile(device_, report, kReportBytes, nullptr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        if (WaitForSingleObject(writeEvent_, kWriteTimeoutMs) != WAIT_OBJECT_0) {
            // The STM32 is not taking OUT reports. Cancel, and wait for
            // the cancel so that ov and report stay valid until done.
            CancelIoEx(device_, &ov);
        }
    }
    return GetOverlappedResult(device_, &ov, &written, TRUE) && written == kReportBytes;
}

void WinHidLink::run()
{
    for (;;) {
        std::string description;
        BoardVersion version;
        HANDLE device = findAndOpen(description, version);
        if (device != INVALID_HANDLE_VALUE) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                device_ = device;
                description_ = description;
                boardVersion_ = version;
            }
            events_.push(HostEvent::HidConnected);
            readUntilGone(device);
            closeDevice();
            events_.push(HostEvent::HidDisconnected);
        }
        if (WaitForSingleObject(stopEvent_, kScanIntervalMs) == WAIT_OBJECT_0) {
            return;
        }
    }
}

HANDLE WinHidLink::findAndOpen(std::string& description, BoardVersion& version)
{
    HANDLE found = INVALID_HANDLE_VALUE;
    forEachHidPath([&](const wchar_t* path) {
        HidInfo info;
        std::string product;
        if (!queryInfo(path, info, product) || !isController(info)) {
            return false;
        }
        found = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (found == INVALID_HANDLE_VALUE) {
            return false;
        }
        // More buffered input reports than the default 32, so a slow
        // consumer loses fewer frames to the HID driver.
        HidD_SetNumInputBuffers(found, 64);
        version = readBoardVersion(found);
        description = (product.empty() ? std::string("(no product string)") : product) + " " + toUtf8(path);
        return true;
    });
    return found;
}

void WinHidLink::readUntilGone(HANDLE device)
{
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (readEvent == nullptr) {
        return;
    }

    for (;;) {
        std::uint8_t report[kReportBytes] = {};
        OVERLAPPED ov{};
        ov.hEvent = readEvent;
        ResetEvent(readEvent);
        DWORD got = 0;

        if (!ReadFile(device, report, kReportBytes, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
            break;  // unplugged, or the driver failed the read
        }
        const HANDLE waits[2] = {readEvent, stopEvent_};
        const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (which != WAIT_OBJECT_0) {
            CancelIoEx(device, &ov);
            (void)GetOverlappedResult(device, &ov, &got, TRUE);
            break;
        }
        if (!GetOverlappedResult(device, &ov, &got, FALSE)) {
            break;
        }
        if (got == kReportBytes && report[0] == 0) {
            Frame frame{};
            std::memcpy(frame.data(), report + 1, frame.size());
            (void)events_.pushFrame(frame);  // drops are counted by the queue
        }
    }
    CloseHandle(readEvent);
}

void WinHidLink::closeDevice()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ != INVALID_HANDLE_VALUE) {
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
    description_.clear();
}

}  // namespace stm32_audio::host::win
