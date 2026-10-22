#include "WinPicture.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
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

// A photo decoded once and turned upright (shared by the board's picture
// and the settings window's preview).
struct Upright {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmap> decoded;
    ComPtr<IWICBitmapFlipRotator> rotator;
    UINT width = 0;
    UINT height = 0;
};

// Decode @p path into memory and turn it upright by its EXIF orientation.
// Decoding first matters: the flip rotator reads its source a column at a
// time when it turns by 90 degrees, and on the JPEG decoder itself that
// decodes the file again for every row (minutes for a phone photo, #57).
std::string openUpright(const std::wstring& path, Upright& u)
{
    HRESULT h = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(u.wic.put()));
    if (FAILED(h)) {
        return hr("WIC", h);
    }
    h = u.wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                         u.decoder.put());
    if (FAILED(h)) {
        return hr("open", h);
    }
    h = u.decoder->GetFrame(0, u.frame.put());
    if (SUCCEEDED(h)) {
        h = u.wic->CreateBitmapFromSource(u.frame.get(), WICBitmapCacheOnLoad, u.decoded.put());
    }
    if (SUCCEEDED(h)) {
        h = u.wic->CreateBitmapFlipRotator(u.rotator.put());
    }
    if (SUCCEEDED(h)) {
        h = u.rotator->Initialize(u.decoded.get(), uprightTransform(exifOrientation(u.frame.get())));
    }
    if (SUCCEEDED(h)) {
        h = u.rotator->GetSize(&u.width, &u.height);
    }
    if (FAILED(h) || u.width == 0U || u.height == 0U) {
        return hr("decode", h);
    }
    return std::string();
}

// COM for the calling thread, as long as the object lives.
class ComScope {
public:
    ComScope() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope()
    {
        if (initialized_) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool initialized_;  // S_FALSE (already, same model) also counts
};

// Save @p source as a JPEG of at most @p longSide pixels (the settings
// window's preview).
std::string writePreview(Upright& u, const std::wstring& target, UINT longSide)
{
    UINT w = u.width;
    UINT h = u.height;
    if (w > longSide || h > longSide) {
        if (w >= h) {
            h = static_cast<UINT>((static_cast<unsigned long long>(h) * longSide) / w);
            w = longSide;
        } else {
            w = static_cast<UINT>((static_cast<unsigned long long>(w) * longSide) / h);
            h = longSide;
        }
    }
    w = w == 0U ? 1U : w;
    h = h == 0U ? 1U : h;
    ComPtr<IWICBitmapScaler> scaler;
    HRESULT r = u.wic->CreateBitmapScaler(scaler.put());
    if (SUCCEEDED(r)) {
        r = scaler->Initialize(u.rotator.get(), w, h, WICBitmapInterpolationModeHighQualityCubic);
    }
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(r)) {
        r = u.wic->CreateFormatConverter(converter.put());
    }
    if (SUCCEEDED(r)) {
        r = converter->Initialize(scaler.get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                                  WICBitmapPaletteTypeCustom);
    }
    ComPtr<IWICStream> stream;
    if (SUCCEEDED(r)) {
        r = u.wic->CreateStream(stream.put());
    }
    if (SUCCEEDED(r)) {
        r = stream->InitializeFromFilename(target.c_str(), GENERIC_WRITE);
    }
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(r)) {
        r = u.wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put());
    }
    if (SUCCEEDED(r)) {
        r = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (SUCCEEDED(r)) {
        r = encoder->CreateNewFrame(frame.put(), options.put());
    }
    if (SUCCEEDED(r) && options.get() != nullptr) {
        PROPBAG2 option{};
        wchar_t name[] = L"ImageQuality";
        option.pstrName = name;
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = 0.85F;
        (void)options->Write(1, &option, &value);
    }
    if (SUCCEEDED(r)) {
        r = frame->Initialize(options.get());
    }
    if (SUCCEEDED(r)) {
        r = frame->SetSize(w, h);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(r)) {
        r = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(r)) {
        r = frame->WriteSource(converter.get(), nullptr);
    }
    if (SUCCEEDED(r)) {
        r = frame->Commit();
    }
    if (SUCCEEDED(r)) {
        r = encoder->Commit();
    }
    return FAILED(r) ? hr("preview", r) : std::string();
}

std::wstring settingsDirectory()
{
    WinPreferenceFile file;
    return file.directory();
}

std::wstring lowerExtension(const std::wstring& path)
{
    const std::size_t dot = path.find_last_of(L'.');
    const std::size_t slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return L".jpg";
    }
    std::wstring ext = path.substr(dot);
    for (wchar_t& c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return ext.size() > 6U ? L".jpg" : ext;
}

std::wstring fileName(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1U);
}

}  // namespace

std::wstring picturesDirectory()
{
    const std::wstring dir = settingsDirectory();
    return dir.empty() ? std::wstring() : dir + L"\\pictures";
}

std::optional<PictureLibrary> readPictureLibrary()
{
    WinPreferenceFile file(L"pictures", L"json");
    const std::optional<std::string> text = file.read();
    if (!text) {
        return std::nullopt;
    }
    return parsePictureLibrary(*text);
}

bool writePictureLibrary(const PictureLibrary& library)
{
    WinPreferenceFile file(L"pictures", L"json");
    return file.write(serializePictureLibrary(library));
}

std::string readCurrentPicture()
{
    WinPreferenceFile file(L"picture-current");
    const std::optional<std::string> text = file.read();
    if (!text) {
        return std::string();
    }
    return text->substr(0, text->find_first_of("\r\n"));
}

bool writeCurrentPicture(const std::string& id)
{
    WinPreferenceFile file(L"picture-current");
    return file.write((id.empty() ? std::string(kNoPicture) : id) + "\n");
}

std::optional<std::string> oldPicturePath()
{
    WinPreferenceFile file(L"picture");
    std::optional<std::string> text = file.read();
    if (!text) {
        return std::nullopt;
    }
    std::string path = text->substr(0, text->find_first_of("\r\n"));
    if (path.size() >= 3U && path.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        path.erase(0, 3);
    }
    if (path.empty()) {
        return std::nullopt;
    }
    return path;
}

PictureLoad loadPicture(const std::wstring& path, const std::optional<NormRect>& crop)
{
    PictureLoad result;
    ComScope com;
    Upright u;
    result.error = openUpright(path, u);
    if (!result.error.empty()) {
        return result;
    }
    // The chosen part, else the largest centred 480:272 rectangle.
    const NormRect part =
        crop && validCrop(*crop, u.width, u.height) ? *crop : defaultCrop(u.width, u.height);
    auto edge = [](double fraction, UINT size) {
        const long v = static_cast<long>(std::lround(fraction * size));
        return static_cast<INT>(std::clamp(v, 0L, static_cast<long>(size)));
    };
    WICRect rect{edge(part.x, u.width), edge(part.y, u.height), 0, 0};
    rect.Width = std::max(1, std::min(edge(part.w, u.width), static_cast<INT>(u.width) - rect.X));
    rect.Height = std::max(1, std::min(edge(part.h, u.height), static_cast<INT>(u.height) - rect.Y));

    ComPtr<IWICBitmapClipper> clipper;
    HRESULT h = u.wic->CreateBitmapClipper(clipper.put());
    if (SUCCEEDED(h)) {
        h = clipper->Initialize(u.rotator.get(), &rect);
    }
    ComPtr<IWICBitmapScaler> scaler;
    if (SUCCEEDED(h)) {
        h = u.wic->CreateBitmapScaler(scaler.put());
    }
    if (SUCCEEDED(h)) {
        h = scaler->Initialize(clipper.get(), PROTO_PICTURE_W, PROTO_PICTURE_H, WICBitmapInterpolationModeHighQualityCubic);
    }
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(h)) {
        h = u.wic->CreateFormatConverter(converter.put());
    }
    if (SUCCEEDED(h)) {
        h = converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                  WICBitmapPaletteTypeCustom);
    }
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(PROTO_PICTURE_W) * PROTO_PICTURE_H * 4U);
    if (SUCCEEDED(h)) {
        h = converter->CopyPixels(nullptr, PROTO_PICTURE_W * 4U, static_cast<UINT>(bgra.size()), bgra.data());
    }
    if (FAILED(h)) {
        result.error = hr("scale", h);
        return result;
    }
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(rgb565FromBgra(bgra.data(), bgra.size()));
    if (pixels->size() != PROTO_PICTURE_BYTES) {
        result.error = "convert failed";
        return result;
    }
    result.pixels = std::move(pixels);
    return result;
}

std::optional<LibraryPicture> addLibraryPicture(PictureLibrary& library, const std::wstring& source,
                                                std::string& error)
{
    const std::wstring dir = picturesDirectory();
    if (dir.empty()) {
        error = "no settings folder";
        return std::nullopt;
    }
    (void)CreateDirectoryW(settingsDirectory().c_str(), nullptr);
    (void)CreateDirectoryW(dir.c_str(), nullptr);
    (void)CreateDirectoryW((dir + L"\\previews").c_str(), nullptr);

    ComScope com;
    Upright u;
    error = openUpright(source, u);
    if (!error.empty()) {
        return std::nullopt;
    }
    LibraryPicture pic;
    pic.id = newPictureId(library);
    const std::wstring file = toWide(pic.id) + lowerExtension(source);
    pic.file = toUtf8(file.c_str());
    pic.name = toUtf8(fileName(source).c_str());
    pic.width = u.width;
    pic.height = u.height;
    pic.crop = defaultCrop(pic.width, pic.height);
    pic.shuffle = true;
    // The photo unchanged (its crop can be changed later), and a preview.
    if (!CopyFileW(source.c_str(), (dir + L"\\" + file).c_str(), FALSE)) {
        error = "copy failed";
        return std::nullopt;
    }
    error = writePreview(u, dir + L"\\previews\\" + toWide(pic.id) + L".jpg", kPreviewLongSide);
    if (!error.empty()) {
        (void)DeleteFileW((dir + L"\\" + file).c_str());
        return std::nullopt;
    }
    library.pictures.push_back(pic);
    return pic;
}

void removeLibraryFiles(const LibraryPicture& picture)
{
    const std::wstring dir = picturesDirectory();
    if (dir.empty()) {
        return;
    }
    (void)DeleteFileW((dir + L"\\" + toWide(picture.file)).c_str());
    (void)DeleteFileW((dir + L"\\previews\\" + toWide(picture.id) + L".jpg").c_str());
}

bool migrateOldPicture(std::string& message)
{
    if (readPictureLibrary()) {
        return false;  // the library exists
    }
    PictureLibrary library;
    const std::optional<std::string> old = oldPicturePath();
    if (old) {
        std::string error;
        if (addLibraryPicture(library, toWide(*old), error)) {
            message = "Picture library: " + *old + " added.";
        } else {
            message = "WARN: picture library: " + *old + ": " + error;
        }
    }
    return writePictureLibrary(library);
}

// --------------------------------------------------------------- service

WinPicture::WinPicture(LogFn log) : log_(std::move(log)), rng_(static_cast<std::uint32_t>(GetTickCount64())) {}

void WinPicture::reload()
{
    library_ = readPictureLibrary().value_or(PictureLibrary());
    const std::string stored = readCurrentPicture();
    if (stored == kNoPicture) {
        current_.clear();
    } else if (findPicture(library_, stored) != nullptr) {
        current_ = stored;
    } else {
        // Not chosen yet, or removed: the first picture.
        current_ = library_.pictures.empty() ? std::string() : library_.pictures.front().id;
    }
    loadCurrent();
}

void WinPicture::reloadIfChanged()
{
    const ShuffleSettings before = library_.shuffle;
    library_ = readPictureLibrary().value_or(PictureLibrary());
    if (before.enabled != library_.shuffle.enabled || before.minutes != library_.shuffle.minutes) {
        shownAtMs_.reset();  // the next picture after a full interval from now
    }
    const LibraryPicture* now = current_.empty() ? nullptr : findPicture(library_, current_);
    if (!current_.empty() && now == nullptr) {
        current_ = library_.pictures.empty() ? std::string() : library_.pictures.front().id;  // removed
        loadCurrent();
        return;
    }
    if (now != nullptr && loaded_ &&
        (now->file != loaded_->file || now->crop.x != loaded_->crop.x || now->crop.y != loaded_->crop.y ||
         now->crop.w != loaded_->crop.w || now->crop.h != loaded_->crop.h)) {
        loadCurrent();  // its part was changed
    }
}

bool WinPicture::show(const std::string& id, std::uint64_t nowMs)
{
    if (!id.empty() && findPicture(library_, id) == nullptr) {
        return false;
    }
    current_ = id;
    (void)writeCurrentPicture(current_);
    shownAtMs_ = nowMs;
    loadCurrent();
    return true;
}

void WinPicture::onTick(std::uint64_t nowMs)
{
    if (!library_.shuffle.enabled) {
        return;
    }
    if (!shownAtMs_) {
        shownAtMs_ = nowMs;
        return;
    }
    if (nowMs - *shownAtMs_ < static_cast<std::uint64_t>(library_.shuffle.minutes) * 60000U) {
        return;
    }
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const std::optional<std::string> next = nextShufflePicture(library_, current_, uniform(rng_));
    shownAtMs_ = nowMs;
    if (next && *next != current_) {
        (void)show(*next, nowMs);
    }
}

void WinPicture::loadCurrent()
{
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    loaded_.reset();
    const LibraryPicture* pic = current_.empty() ? nullptr : findPicture(library_, current_);
    if (pic != nullptr) {
        const std::wstring path = picturesDirectory() + L"\\" + toWide(pic->file);
        const PictureLoad loaded = loadPicture(path, pic->crop);
        pixels = loaded.pixels;
        loaded_ = *pic;
        if (log_) {
            log_(pixels ? "Picture: " + pic->id + " (" + pic->name + ")"
                        : "WARN: picture " + pic->id + " (" + pic->name + "): " + loaded.error);
        }
    }
    snapshot_.pixels = std::move(pixels);
    ++snapshot_.version;
    if (snapshot_.version == 0U) {
        snapshot_.version = 1U;
    }
}

}  // namespace stm32_audio::host::win
