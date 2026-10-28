#pragma once
#include "assets/image.hpp"
#include "assets/mvgl.hpp"
#include <array>
#include <string>
#include <vector>

namespace cr3 {
// A display-only choice: gene IDs, series in the database, and saves are never changed.
// Menu atlases and full-screen pictures are selected together, from the user's files.
class CardArtwork {
public:
    void configure(const Database& primary, std::vector<std::string> sourceFolders);
    bool setEnabled(bool enabled); // true when cached GPU textures need to be refreshed
    bool enabled() const { return enabled_; }
    int available() const;
    bool decodeTexture(int releaseSeries, Image& out) const;
    bool readPicture(int releaseSeries, std::vector<uint8_t>& out) const;
    std::string originalName(int master, bool japanese) const;
    std::string originalDescription(int master, int rarity, bool japanese) const;
    static int textureSeries(const std::string& name); // card_NNN, else -1
private:
    struct Original {
        Image texture;
        std::vector<uint8_t> picture;
        bool ready() const { return !texture.rgba.empty() && !picture.empty(); }
    };
    const Database* primary_ = nullptr;
    std::vector<std::string> folders_;
    std::array<Original, 3> originals_;
    bool enabled_ = false;
    void prepare();
    bool donor(const std::string& folder);
};
} // namespace cr3
