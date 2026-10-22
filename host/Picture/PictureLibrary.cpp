#include "PictureLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "MediaCover.h"  // centerCrop
#include "nlohmann/json.hpp"
#include "protocol.h"

namespace stm32_audio::host {

using nlohmann::json;

namespace {

bool validId(const std::string& id)
{
    if (id.size() < 2U || id.size() > 16U || id[0] != 'p') {
        return false;
    }
    return std::all_of(id.begin() + 1, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// A file name in the pictures folder: no path, no "..".
bool validFile(const std::string& file)
{
    return !file.empty() && file.size() < 64U && file.find_first_of("/\\:") == std::string::npos &&
           file.find("..") == std::string::npos;
}

double number(const json& j, const char* key, double fallback)
{
    const auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<double>() : fallback;
}

}  // namespace

NormRect defaultCrop(std::uint32_t width, std::uint32_t height)
{
    NormRect r;
    if (width == 0U || height == 0U) {
        return r;
    }
    const CropRect c = centerCrop(width, height, PROTO_PICTURE_W, PROTO_PICTURE_H);
    r.x = static_cast<double>(c.x) / width;
    r.y = static_cast<double>(c.y) / height;
    r.w = static_cast<double>(c.width) / width;
    r.h = static_cast<double>(c.height) / height;
    return r;
}

bool validCrop(const NormRect& c, std::uint32_t width, std::uint32_t height)
{
    constexpr double kEdge = 1e-6;
    if (width == 0U || height == 0U || !std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.w) ||
        !std::isfinite(c.h) || c.x < -kEdge || c.y < -kEdge || c.w <= 0.0 || c.h <= 0.0 ||
        c.x + c.w > 1.0 + kEdge || c.y + c.h > 1.0 + kEdge) {
        return false;
    }
    const double pixelsW = c.w * width;
    const double pixelsH = c.h * height;
    if (pixelsW < 1.0 || pixelsH < 1.0) {
        return false;
    }
    const double aspect = pixelsW / pixelsH;
    const double wanted = static_cast<double>(PROTO_PICTURE_W) / PROTO_PICTURE_H;
    return std::fabs(aspect / wanted - 1.0) <= 0.02;
}

std::optional<PictureLibrary> parsePictureLibrary(const std::string& text)
{
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    PictureLibrary lib;
    const double nextId = number(j, "nextId", 1.0);
    if (nextId >= 1.0 && nextId < 1e9) {
        lib.nextId = static_cast<unsigned>(nextId);
    }
    const auto shuffle = j.find("shuffle");
    if (shuffle != j.end() && shuffle->is_object()) {
        const auto enabled = shuffle->find("enabled");
        lib.shuffle.enabled = enabled != shuffle->end() && enabled->is_boolean() && enabled->get<bool>();
        const double minutes = number(*shuffle, "minutes", lib.shuffle.minutes);
        if (minutes >= kMinShuffleMinutes && minutes <= kMaxShuffleMinutes) {
            lib.shuffle.minutes = static_cast<unsigned>(minutes);
        }
        const auto random = shuffle->find("random");
        if (random != shuffle->end() && random->is_boolean()) {
            lib.shuffle.random = random->get<bool>();
        }
    }
    const auto pictures = j.find("pictures");
    if (pictures != j.end() && pictures->is_array()) {
        for (const json& p : *pictures) {
            if (!p.is_object()) {
                continue;
            }
            LibraryPicture pic;
            pic.id = p.value("id", std::string());
            pic.file = p.value("file", std::string());
            pic.name = p.value("name", std::string());
            const double w = number(p, "width", 0.0);
            const double h = number(p, "height", 0.0);
            if (!validId(pic.id) || !validFile(pic.file) || findPicture(lib, pic.id) != nullptr || w < 1.0 ||
                h < 1.0 || w > 100000.0 || h > 100000.0) {
                continue;
            }
            pic.width = static_cast<std::uint32_t>(w);
            pic.height = static_cast<std::uint32_t>(h);
            pic.crop = defaultCrop(pic.width, pic.height);
            const auto crop = p.find("crop");
            if (crop != p.end() && crop->is_object()) {
                const NormRect c{number(*crop, "x", -1.0), number(*crop, "y", -1.0), number(*crop, "w", -1.0),
                                 number(*crop, "h", -1.0)};
                if (validCrop(c, pic.width, pic.height)) {
                    pic.crop = c;
                }
            }
            const auto inShuffle = p.find("shuffle");
            pic.shuffle = inShuffle == p.end() || !inShuffle->is_boolean() || inShuffle->get<bool>();
            lib.pictures.push_back(std::move(pic));
        }
    }
    return lib;
}

std::string serializePictureLibrary(const PictureLibrary& lib)
{
    json pictures = json::array();
    for (const LibraryPicture& p : lib.pictures) {
        pictures.push_back(json{{"id", p.id},
                                {"file", p.file},
                                {"name", p.name},
                                {"width", p.width},
                                {"height", p.height},
                                {"crop", json{{"x", p.crop.x}, {"y", p.crop.y}, {"w", p.crop.w}, {"h", p.crop.h}}},
                                {"shuffle", p.shuffle}});
    }
    const json out{{"version", 1},
                   {"nextId", lib.nextId},
                   {"shuffle", json{{"enabled", lib.shuffle.enabled},
                                    {"minutes", lib.shuffle.minutes},
                                    {"random", lib.shuffle.random}}},
                   {"pictures", pictures}};
    return out.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

const LibraryPicture* findPicture(const PictureLibrary& lib, const std::string& id)
{
    for (const LibraryPicture& p : lib.pictures) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string newPictureId(PictureLibrary& lib)
{
    unsigned long next = lib.nextId;
    for (const LibraryPicture& p : lib.pictures) {
        next = std::max(next, std::strtoul(p.id.c_str() + 1, nullptr, 10) + 1UL);
    }
    lib.nextId = static_cast<unsigned>(next + 1UL);
    return "p" + std::to_string(next);
}

std::optional<std::string> nextShufflePicture(const PictureLibrary& lib, const std::string& current,
                                              double random01)
{
    std::vector<const LibraryPicture*> taking;
    for (const LibraryPicture& p : lib.pictures) {
        if (p.shuffle) {
            taking.push_back(&p);
        }
    }
    if (taking.empty()) {
        return std::nullopt;
    }
    if (!lib.shuffle.random) {
        // The one after the current in the library's order (the first if
        // the current takes no part).
        const auto it = std::find_if(taking.begin(), taking.end(),
                                     [&current](const LibraryPicture* p) { return p->id == current; });
        if (it == taking.end() || it + 1 == taking.end()) {
            return taking.front()->id;
        }
        return (*(it + 1))->id;
    }
    std::vector<const LibraryPicture*> others;
    for (const LibraryPicture* p : taking) {
        if (p->id != current) {
            others.push_back(p);
        }
    }
    if (others.empty()) {
        return taking.front()->id;
    }
    const double r = std::clamp(random01, 0.0, 0.999999);
    return others[static_cast<std::size_t>(r * static_cast<double>(others.size()))]->id;
}

}  // namespace stm32_audio::host
