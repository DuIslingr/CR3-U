#pragma once
// Media.Vision's MDB1 archives (`.mvgl`, docs/FORMATS.md): a table of names and (offset, size,
// stored size); a file stored smaller than its size is doboz-compressed. CR3 splits its data over
// many: the iPhone/iPad's ~170 CRDB*.ios.mvgl, the Android release's main.<version>.android.mvgl
// (in its APK) and asset packs' CRDB{bgm,voice,se,mov}.android.mvgl, the Vita's ~236
// CR3/resources/**/CRDB*.psp2.mvgl. A Database is one release's set, looked up by name.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cr3 {

// doboz (Attila Afra's LZ77 variant): `in` is a whole compressed block, header included; false
// when it is malformed or does not decode to the size its header gives
bool undoboz(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out);

// One .mvgl file - on its own, or stored (uncompressed) inside a zip: `base` its first byte there
class MvglFile {
public:
    struct Entry {
        std::string name; // as stored, folders with '\', the extension after a '.' ("images\a.img")
        uint64_t offset = 0; // in the host file
        uint32_t size = 0, stored = 0;
    };

    MvglFile() = default;
    MvglFile(const MvglFile&) = delete;
    MvglFile& operator=(const MvglFile&) = delete;
    ~MvglFile();

    bool open(const std::string& path, std::string* error = nullptr, uint64_t base = 0);
    void close();
    bool isOpen() const { return file_ != nullptr; }
    const std::string& path() const { return path_; }
    const std::vector<Entry>& entries() const { return entries_; }
    bool read(const Entry& e, std::vector<uint8_t>& out, std::string* error = nullptr) const;

private:
    std::string path_;
    std::FILE* file_ = nullptr;
    std::vector<Entry> entries_;
};

// A release's data: every archive of it, plus its loose files (movies, subtitles, fonts), each
// name once - the release's main CRDB first, then the others by name (a name in two archives is
// almost always the same file). An overlay (a fan translation's archives: the same names,
// translated) goes over them while it is active.
class Database {
public:
    enum class Kind { None, Ios, Android, Vita };
    struct Entry {
        std::string name;     // as stored ("images\a.img"); loose files by their own name ("c01.mp4")
        uint64_t offset = 0;
        uint32_t size = 0, stored = 0;
        int16_t file = -1;    // its archive; -1 a loose file (loose_ index in `offset`)
        bool overlay = false; // the overlay's
    };

    Database() = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // a release: its folder (an .app or the Payload above it; a Vita PCSG00500 / PCSG00457 folder
    // or its CR3/resources; an unpacked APK or a folder with the APK and the asset packs), one of
    // its archives, or an .apk / .apks (read in place: their archives are stored, not deflated)
    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return !files_.empty(); }
    Kind kind() const { return kind_; }
    const char* kindName() const;
    const std::string& path() const { return path_; } // what open() was given
    const std::string& root() const { return root_; } // the release's own folder (resources/, the .app)
    size_t archiveCount() const { return files_.size(); }
    const MvglFile& archive(size_t i) const { return *files_[i]; }

    // a fan translation over the release (the Vita's: its rePatch folder, PCSG00500 or
    // CR3/resources under it, or the archives' folder): its archives and loose files, used in
    // place of the release's own while overlayActive()
    bool addOverlay(const std::string& path, std::string* error = nullptr);
    bool hasOverlay() const { return !overlay_.empty(); }
    void setOverlayActive(bool on) { overlayActive_ = on && hasOverlay(); }
    bool overlayActive() const { return overlayActive_; }
    const std::string& overlayPath() const { return overlayPath_; }

    // every name once (an overlay's replace, never add)
    const std::vector<Entry>& entries() const { return entries_; }
    // by name, case-insensitive, '/' or '\' alike - the overlay's first while it is active
    const Entry* find(const std::string& name) const;
    bool contains(const std::string& name) const { return find(name) != nullptr; }
    bool read(const Entry& e, std::vector<uint8_t>& out, std::string* error = nullptr) const;
    bool read(const std::string& name, std::vector<uint8_t>& out, std::string* error = nullptr) const;
    // where `name` comes from: its archive's file name, or the loose file's path ("" none)
    std::string sourceOf(const std::string& name) const;

    static std::string key(const std::string& name);

    // CR3-U's add-ons (assets/addons.hpp): a file changed as it is read - the release's or the
    // overlay's (data\\item_para.mbe with the add-ons' rows) -, and a name for another's file
    // (j001b101.geom: j001b02.geom), in entries() like the rest. close() drops them.
    using Edit = std::function<bool(std::vector<uint8_t>& bytes, std::string* error)>;
    void setEdit(const std::string& name, Edit edit);
    bool addAlias(const std::string& name, const std::string& of); // (false: `of` isn't here, or `name` is)

private:
    bool addArchive(const std::string& path, uint64_t base, bool overlay, std::string* error);
    bool readStored(const Entry& e, std::vector<uint8_t>& out, std::string* error) const;
    void addLoose(const std::string& path, const std::string& name, bool overlay);

    Kind kind_ = Kind::None;
    std::string path_, root_, overlayPath_;
    std::vector<std::unique_ptr<MvglFile>> files_;
    std::vector<std::string> loose_; // loose files' paths
    std::vector<Entry> entries_, overlay_;
    std::unordered_map<std::string, size_t> byName_, overlayByName_;
    std::unordered_map<std::string, Edit> edits_;
    bool overlayActive_ = false;
};

// The release opened last is a 64-bit build's (iOS, Android): its animations' headers (EA04)
// hold their offsets one word earlier (+0x30.. where the Vita's are at +0x34..), and nothing in
// the files tells them apart. Database::open sets it; Animation::parse reads it
extern bool gWideAnimations;

// The engine's code takes a release where CR2-U's took its one archive
using MvglArchive = Database;

} // namespace cr3
