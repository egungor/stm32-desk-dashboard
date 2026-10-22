#include "WinPicture.h"

#include <windows.h>
#include <wincodec.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "MediaCover.h"
#include "WinPreferenceFile.h"
#include "WinStrings.h"
#include "protocol.h"

namespace stm32_audio::host::win {

namespace {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr()
    {
        if (p_ != nullptr) {
            p_->Release();
        }
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return p_; }
    T** put() { return &p_; }
    T* operator->() const { return p_; }

private:
    T* p_ = nullptr;
};

std::string hr(const char* what, HRESULT code)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%s failed (0x%08lX)", what, static_cast<unsigned long>(code));
    return text;
}

// The EXIF orientation of @p frame (1..8), 1 when it has none. Phone
// photos keep their pixels as the sensor saw them and only store this tag.
std::uint16_t exifOrientation(IWICBitmapFrameDecode* frame)
{
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(reader.put()))) {
        return 1U;
    }
    static const wchar_t* const kQueries[] = {L"System.Photo.Orientation", L"/app1/ifd/{ushort=274}",
                                              L"/ifd/{ushort=274}"};
    for (const wchar_t* query : kQueries) {
        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT h = reader->GetMetadataByName(query, &value);
        std::uint16_t orientation = 0U;
        if (SUCCEEDED(h) && value.vt == VT_UI2) {
            orientation = value.uiVal;
        }
        PropVariantClear(&value);
        if (orientation >= 1U && orientation <= 8U) {
            return orientation;
        }
    }
    return 1U;
}

// The WIC transform that turns an image stored with EXIF @p orientation
// upright (WIC rotates clockwise first, then flips).
WICBitmapTransformOptions uprightTransform(std::uint16_t orientation)
{
    switch (orientation) {
    case 2U:
        return WICBitmapTransformFlipHorizontal;
    case 3U:
        return WICBitmapTransformRotate180;
    case 4U:
        return WICBitmapTransformFlipVertical;
    case 5U:
        return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6U:
        return WICBitmapTransformRotate90;
    case 7U:
        return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8U:
        return WICBitmapTransformRotate270;
    default:
        return WICBitmapTransformRotate0;
    }
}

}  // namespace

std::optional<std::string> storedPicturePath()
{
    WinPreferenceFile file(L"picture");
    std::optional<std::string> text = file.read();
    if (!text) {
        return std::nullopt;
    }
    // The first line, without the line end.
    const std::size_t end = text->find_first_of("\r\n");
    std::string path = text->substr(0, end);
    if (path.size() >= 3U && path.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        path.erase(0, 3);
    }
    if (path.empty()) {
        return std::nullopt;
    }
    return path;
}

bool storePicturePath(const std::string& path)
{
    WinPreferenceFile file(L"picture");
    return file.write(path + "\n");
}

PictureLoad loadPicture(const std::wstring& path)
{
    PictureLoad result;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(com);  // S_FALSE: already, also counts
    do {
        ComPtr<IWICImagingFactory> wic;
        HRESULT h = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()));
        if (FAILED(h)) {
            result.error = hr("WIC", h);
            break;
        }
        ComPtr<IWICBitmapDecoder> decoder;
        h = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                           decoder.put());
        if (FAILED(h)) {
            result.error = hr("open", h);
            break;
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        h = decoder->GetFrame(0, frame.put());
        // Decode once into memory: the flip rotator reads its source a
        // column at a time when it turns by 90 degrees, and on the JPEG
        // decoder itself that decodes the file again for every row
        // (minutes for a phone photo, #57).
        ComPtr<IWICBitmap> decoded;
        if (SUCCEEDED(h)) {
            h = wic->CreateBitmapFromSource(frame.get(), WICBitmapCacheOnLoad, decoded.put());
        }
        // Turn the picture upright first, so the crop is taken from the
        // picture as it is seen (a portrait phone photo stays portrait).
        ComPtr<IWICBitmapFlipRotator> upright;
        if (SUCCEEDED(h)) {
            h = wic->CreateBitmapFlipRotator(upright.put());
        }
        if (SUCCEEDED(h)) {
            h = upright->Initialize(decoded.get(), uprightTransform(exifOrientation(frame.get())));
        }
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(h)) {
            h = upright->GetSize(&width, &height);
        }
        if (FAILED(h) || width == 0U || height == 0U) {
            result.error = hr("decode", h);
            break;
        }
        const CropRect crop = centerCrop(width, height, PROTO_PICTURE_W, PROTO_PICTURE_H);
        ComPtr<IWICBitmapClipper> clipper;
        h = wic->CreateBitmapClipper(clipper.put());
        const WICRect rect = {static_cast<INT>(crop.x), static_cast<INT>(crop.y), static_cast<INT>(crop.width),
                              static_cast<INT>(crop.height)};
        if (SUCCEEDED(h)) {
            h = clipper->Initialize(upright.get(), &rect);
        }
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(h)) {
            h = wic->CreateBitmapScaler(scaler.put());
        }
        if (SUCCEEDED(h)) {
            h = scaler->Initialize(clipper.get(), PROTO_PICTURE_W, PROTO_PICTURE_H,
                                   WICBitmapInterpolationModeHighQualityCubic);
        }
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(h)) {
            h = wic->CreateFormatConverter(converter.put());
        }
        if (SUCCEEDED(h)) {
            h = converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                      0.0, WICBitmapPaletteTypeCustom);
        }
        std::vector<std::uint8_t> bgra(static_cast<std::size_t>(PROTO_PICTURE_W) * PROTO_PICTURE_H * 4U);
        if (SUCCEEDED(h)) {
            h = converter->CopyPixels(nullptr, PROTO_PICTURE_W * 4U, static_cast<UINT>(bgra.size()), bgra.data());
        }
        if (FAILED(h)) {
            result.error = hr("scale", h);
            break;
        }
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(rgb565FromBgra(bgra.data(), bgra.size()));
        if (pixels->size() != PROTO_PICTURE_BYTES) {
            result.error = "convert failed";
            break;
        }
        result.pixels = std::move(pixels);
    } while (false);
    if (comInitialized) {
        CoUninitialize();
    }
    return result;
}

WinPicture::WinPicture(LogFn log) : log_(std::move(log)) {}

void WinPicture::reload()
{
    path_ = storedPicturePath();
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    if (path_) {
        const PictureLoad loaded = loadPicture(toWide(*path_));
        pixels = loaded.pixels;
        if (log_) {
            log_(pixels ? "Picture: " + *path_ : "WARN: picture " + *path_ + ": " + loaded.error);
        }
    }
    snapshot_.pixels = std::move(pixels);
    ++snapshot_.version;
    if (snapshot_.version == 0U) {
        snapshot_.version = 1U;
    }
}

void WinPicture::reloadIfChanged()
{
    if (storedPicturePath() != path_) {
        reload();
    }
}

}  // namespace stm32_audio::host::win
