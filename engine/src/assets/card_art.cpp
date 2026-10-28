#include "assets/card_art.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>

namespace cr3 {
namespace {
// Index entries 80, 81 and 86. Mobile changes the series for the first two;
// the last reuses series 119 for new art, keeping the original as 160 on iOS.
constexpr int releaseSeries[] = {131, 124, 119};
constexpr int originalSeries[] = {42, 49, 119};
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
template<class Read>
bool pair(Read read, int series, Image& texture, std::vector<uint8_t>& picture) {
    char name[40];
    std::vector<uint8_t> bytes, full;
    Image atlas, check;
    std::snprintf(name, sizeof name, "images\\card_%03d.img", series);
    if (!read(name, bytes) || !decodeImg(bytes, atlas)) return false;
    std::snprintf(name, sizeof name, "%03d.jpg", series);
    if (!read(name, full) || !decodePicture(full, check)) return false;
    texture = std::move(atlas), picture = std::move(full);
    return true;
}
} // namespace

void CardArtwork::configure(const Database& primary, std::vector<std::string> folders) {
    primary_ = &primary;
    folders_ = std::move(folders);
    originals_ = {};
    enabled_ = false;
}

bool CardArtwork::setEnabled(bool enabled) {
    if (enabled_ == enabled) return false;
    enabled_ = enabled;
    originals_ = {};
    if (enabled_) prepare(); // turning it on again also discovers newly copied archives
    return true;
}

int CardArtwork::available() const {
    int n = 0;
    for (const auto& original : originals_) n += original.ready();
    return n;
}

void CardArtwork::prepare() {
    if (!primary_) return;
    auto read = [this](const std::string& name, std::vector<uint8_t>& out) { return primary_->read(name, out); };
    for (int k = 0; k < 3; ++k) {
        const int series = k == 2 && primary_->kind() != Database::Kind::Vita ? 160 : originalSeries[k];
        pair(read, series, originals_[k].texture, originals_[k].picture);
    }
    if (available() < 3)
        for (const std::string& folder : folders_)
            if (donor(folder)) break;
    // Vita atlases use the opposite row order from mobile atlases. Keep the
    // selected release's mesh UV convention when borrowing another release's art.
    for (int k = 0; k < 3; ++k) {
        Image release;
        std::vector<uint8_t> bytes;
        char name[40];
        std::snprintf(name, sizeof name, "images\\card_%03d.img", releaseSeries[k]);
        if (!originals_[k].ready() || !read(name, bytes) || !decodeImg(bytes, release)) continue;
        Image& image = originals_[k].texture;
        if (image.bottomUp != release.bottomUp) {
            const size_t row = size_t(image.width) * 4;
            for (int y = 0; y < image.height / 2; ++y)
                std::swap_ranges(image.rgba.begin() + size_t(y) * row, image.rgba.begin() + size_t(y + 1) * row,
                                 image.rgba.begin() + size_t(image.height - 1 - y) * row);
            image.bottomUp = release.bottomUp;
        }
    }
}

bool CardArtwork::donor(const std::string& folder) {
    namespace fs = std::filesystem;
    if (folder.empty()) return false;
    std::error_code ec;
    fs::path root = fs::u8path(folder);
    if (fs::is_regular_file(root, ec)) root = root.parent_path();
    if (!fs::is_directory(root, ec)) return false;
    struct Archives { fs::path texture, picture; int series = 119; };
    std::map<std::string, Archives> groups; // Vita before iOS; each pair from the same folder
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    size_t seen = 0;
    for (; it != end && !ec && seen < 20000; it.increment(ec), ++seen) {
        if (it.depth() >= 8) it.disable_recursion_pending();
        if (!it->is_regular_file(ec)) continue;
        const std::string name = lower(it->path().filename().u8string());
        const bool vita = name == "crdbcard01.psp2.mvgl" || name == "crdbcardhd.psp2.mvgl";
        const bool ios = name == "crdbcard01.ios.mvgl" || name == "crdbcardhd.ios.mvgl";
        if (!vita && !ios) continue;
        auto& group = groups[(vita ? "0" : "1") + it->path().parent_path().u8string()];
        group.series = vita ? 119 : 160;
        (name.find("card01") != std::string::npos ? group.texture : group.picture) = it->path();
    }
    for (const auto& entry : groups) {
        const auto& group = entry.second;
        if (group.texture.empty() || group.picture.empty()) continue;
        MvglFile textures, pictures; // asset-only readers: never change the primary release's animation layout
        if (!textures.open(group.texture.u8string()) || !pictures.open(group.picture.u8string())) continue;
        auto read = [&](const std::string& name, std::vector<uint8_t>& out) {
            const MvglFile& file = name.compare(0, 7, "images\\") == 0 ? textures : pictures;
            for (const auto& e : file.entries())
                if (Database::key(e.name) == Database::key(name)) return file.read(e, out);
            return false;
        };
        for (int k = 0; k < 3; ++k)
            if (!originals_[k].ready()) pair(read, k == 2 ? group.series : originalSeries[k], originals_[k].texture, originals_[k].picture);
        if (available() == 3) return true;
    }
    return false;
}

bool CardArtwork::decodeTexture(int series, Image& out) const {
    if (!enabled_) return false;
    for (int k = 0; k < 3; ++k)
        if (series == releaseSeries[k] && originals_[k].ready()) return out = originals_[k].texture, true;
    return false;
}

bool CardArtwork::readPicture(int series, std::vector<uint8_t>& out) const {
    if (!enabled_) return false;
    for (int k = 0; k < 3; ++k)
        if (series == releaseSeries[k] && originals_[k].ready()) return out = originals_[k].picture, true;
    return false;
}

int CardArtwork::textureSeries(const std::string& name) {
    if (name.size() != 8 || name.compare(0, 5, "card_") != 0) return -1;
    int series = 0;
    for (size_t k = 5; k < 8; ++k) {
        if (name[k] < '0' || name[k] > '9') return -1;
        series = series * 10 + name[k] - '0';
    }
    return series;
}
} // namespace cr3
