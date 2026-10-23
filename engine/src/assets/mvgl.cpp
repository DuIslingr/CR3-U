#include "assets/mvgl.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <system_error>

namespace cr3 {

bool gWideAnimations = false;

namespace {

namespace fs = std::filesystem;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

bool fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}

// (archives past 2 GB: the Android .apks is 2.3 GB)
bool seek64(std::FILE* f, uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, int64_t(at), SEEK_SET) == 0;
#else
    return fseeko(f, off_t(at), SEEK_SET) == 0;
#endif
}

uint64_t fileSize(std::FILE* f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    return uint64_t(_ftelli64(f));
#else
    fseeko(f, 0, SEEK_END);
    return uint64_t(ftello(f));
#endif
}

bool endsWith(const std::string& s, const char* sfx) {
    const size_t n = std::strlen(sfx);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != sfx[i]) return false;
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A zip's entries (`base`: where the zip starts in its host file; the offsets are the data's there)
struct ZipEntry {
    std::string name;
    uint64_t data = 0;
    uint32_t size = 0, packed = 0;
    uint16_t method = 0;
};

bool zipEntries(std::FILE* f, uint64_t base, uint64_t size, std::vector<ZipEntry>& out) {
    out.clear();
    const uint64_t tail = std::min<uint64_t>(size, 0x10000 + 22);
    std::vector<uint8_t> t(static_cast<size_t>(tail));
    if (t.size() < 22 || !seek64(f, base + size - tail) || std::fread(t.data(), 1, t.size(), f) != t.size()) return false;
    size_t end = t.size() - 22 + 1;
    while (end-- > 0)
        if (le32(&t[end]) == 0x06054b50) break;
    if (end == size_t(-1)) return false;
    const uint32_t count = le16(&t[end + 10]), dirSize = le32(&t[end + 12]), dirOffset = le32(&t[end + 16]);
    std::vector<uint8_t> d(dirSize);
    if (!seek64(f, base + dirOffset) || std::fread(d.data(), 1, d.size(), f) != d.size()) return false;
    for (size_t p = 0, k = 0; k < count && p + 46 <= d.size(); ++k) {
        if (le32(&d[p]) != 0x02014b50) break;
        ZipEntry e;
        e.method = le16(&d[p + 10]);
        e.packed = le32(&d[p + 20]), e.size = le32(&d[p + 24]);
        const uint16_t nameLen = le16(&d[p + 28]), extraLen = le16(&d[p + 30]), commentLen = le16(&d[p + 32]);
        const uint32_t local = le32(&d[p + 42]);
        e.name.assign(reinterpret_cast<const char*>(&d[p + 46]), std::min<size_t>(nameLen, d.size() - p - 46));
        p += 46 + size_t(nameLen) + extraLen + commentLen;
        uint8_t h[30];
        if (!seek64(f, base + local) || std::fread(h, 1, sizeof h, f) != sizeof h || le32(h) != 0x04034b50) continue;
        e.data = base + local + 30 + le16(h + 26) + le16(h + 28);
        out.push_back(std::move(e));
    }
    return true;
}

// the .mvgl files a zip holds stored - and those in the zips it holds stored (an .apks: its
// base.apk; an APK: assets/main.*.android.mvgl), as (offset, name)
void storedArchives(std::FILE* f, uint64_t base, uint64_t size, int depth, std::vector<std::pair<uint64_t, std::string>>& out) {
    std::vector<ZipEntry> es;
    if (!zipEntries(f, base, size, es)) return;
    for (const ZipEntry& e : es) {
        if (e.method != 0 || e.size != e.packed) continue;
        if (endsWith(e.name, ".mvgl")) out.emplace_back(e.data, e.name);
        else if (depth > 0 && (endsWith(e.name, ".apk") || endsWith(e.name, ".zip") || e.name == "data"))
            storedArchives(f, e.data, e.size, depth - 1, out);
    }
}

// the files under `dir` (at most `depth` folders down), sorted
std::vector<std::string> walk(const fs::path& dir, int depth) {
    std::vector<std::string> out;
    std::error_code ec;
    std::function<void(const fs::path&, int)> go = [&](const fs::path& d, int left) {
        for (const fs::directory_entry& e : fs::directory_iterator(d, fs::directory_options::skip_permission_denied, ec)) {
            if (e.is_directory(ec)) {
                if (left > 0) go(e.path(), left - 1);
            } else if (e.is_regular_file(ec)) {
                out.push_back(e.path().string());
            }
            if (out.size() > 100000) return;
        }
    };
    go(dir, depth);
    std::sort(out.begin(), out.end());
    return out;
}

std::string baseName(const std::string& p) { return fs::path(p).filename().string(); }

// a release's main archive among `files` (the one the others are ordered after)
bool isMain(const std::string& path) {
    const std::string n = lower(baseName(path));
    return n == "crdb.ios.mvgl" || n == "crdb.psp2.mvgl" || (n.compare(0, 5, "main.") == 0 && endsWith(n, ".android.mvgl"));
}

} // namespace

bool undoboz(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out) {
    if (inSize < 3) return false;
    const unsigned attr = in[0];
    const unsigned width = ((attr >> 3) & 7) + 1; // the size fields' bytes: 1, 2, 4
    if (width > 4 || inSize < 1 + 2 * width) return false;
    uint32_t size = 0, packed = 0;
    for (unsigned i = 0; i < width; ++i) size |= uint32_t(in[1 + i]) << (8 * i), packed |= uint32_t(in[1 + width + i]) << (8 * i);
    if (packed > inSize) return false;
    size_t p = 1 + 2 * width;
    out.clear();
    if (attr & 0x80) { // stored
        if (p + size > inSize) return false;
        out.assign(in + p, in + p + size);
        return true;
    }
    out.reserve(size);
    // match forms by the word's low 3 bits: mask, offset shift, length mask, length shift, bytes
    static const struct { uint32_t mask; uint8_t offShift, lenMask, lenShift, bytes; } kMatch[8] = {
        {0xff, 2, 0, 0, 1}, {0xffff, 2, 0, 0, 2}, {0xffff, 6, 15, 2, 2}, {0xffffff, 8, 31, 3, 3},
        {0xff, 2, 0, 0, 1}, {0xffff, 2, 0, 0, 2}, {0xffff, 6, 15, 2, 2}, {0xffffffff, 11, 255, 3, 4},
    };
    uint32_t control = 1;
    while (out.size() < size) {
        if (control == 1) { // 31 flags a word; the top set bit marks its end
            if (p + 4 > inSize) return false;
            control = le32(in + p);
            p += 4;
        }
        if ((control & 1) == 0) {
            if (p >= inSize) return false;
            out.push_back(in[p++]);
        } else {
            uint8_t w4[4] = {};
            std::memcpy(w4, in + p, std::min<size_t>(4, inSize - p));
            const uint32_t w = le32(w4);
            const auto& m = kMatch[w & 7];
            const size_t back = (w & m.mask) >> m.offShift; // from the output's end (no - 1)
            const size_t length = ((w >> m.lenShift) & m.lenMask) + 3;
            p += m.bytes;
            if (p > inSize || back > out.size()) return false;
            const size_t src = out.size() - back;
            for (size_t k = 0; k < length; ++k) out.push_back(out[src + k]); // (may overlap itself)
        }
        control >>= 1;
    }
    out.resize(size);
    return true;
}

// ---- MvglFile

MvglFile::~MvglFile() { close(); }

void MvglFile::close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    entries_.clear();
    path_.clear();
}

bool MvglFile::open(const std::string& path, std::string* error, uint64_t base) {
    close();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return fail(error, path + ": cannot open");
    uint8_t h[0x14];
    if (!seek64(f, base) || std::fread(h, 1, sizeof h, f) != sizeof h || std::memcmp(h, "MDB1", 4) != 0) {
        std::fclose(f);
        return fail(error, path + ": not an MDB1 archive (an encrypted Vita dump? install it first)");
    }
    const uint32_t trees = le16(h + 4), names = le16(h + 6), datas = le32(h + 8), dataStart = le32(h + 12);
    std::vector<uint8_t> t(size_t(trees) * 8 + size_t(names) * 64 + size_t(datas) * 12);
    if (std::fread(t.data(), 1, t.size(), f) != t.size()) {
        std::fclose(f);
        return fail(error, path + ": truncated table");
    }
    const uint8_t* tree = t.data();
    const uint8_t* nameTab = tree + size_t(trees) * 8;
    const uint8_t* dataTab = nameTab + size_t(names) * 64;
    for (uint32_t i = 0; i < trees && i < names; ++i) {
        const uint16_t id = le16(tree + 8 * i + 2);
        if (id == 0xffff || id >= datas) continue; // the trie's root
        const char* ext = reinterpret_cast<const char*>(nameTab + 64 * i);
        const char* nm = ext + 4;
        std::string e(ext, strnlen(ext, 4)), n(nm, strnlen(nm, 60));
        while (!e.empty() && e.back() == ' ') e.pop_back(); // ("img ", "nut ")
        Entry en;
        en.name = e.empty() ? n : n + "." + e;
        en.offset = base + dataStart + le32(dataTab + 12 * id);
        en.size = le32(dataTab + 12 * id + 4);
        en.stored = le32(dataTab + 12 * id + 8);
        entries_.push_back(std::move(en));
    }
    file_ = f;
    path_ = path;
    return true;
}

bool MvglFile::read(const Entry& e, std::vector<uint8_t>& out, std::string* error) const {
    if (!file_) return fail(error, "no archive");
    std::vector<uint8_t> raw(e.stored);
    if (!seek64(file_, e.offset) || std::fread(raw.data(), 1, raw.size(), file_) != raw.size())
        return fail(error, e.name + ": cannot read");
    if (e.stored == e.size) {
        out = std::move(raw);
        return true;
    }
    if (!undoboz(raw.data(), raw.size(), out) || out.size() != e.size) return fail(error, e.name + ": bad doboz data");
    return true;
}

// ---- Database

std::string Database::key(const std::string& name) {
    std::string k = name;
    for (char& c : k) c = c == '/' ? '\\' : char(std::tolower(static_cast<unsigned char>(c)));
    return k;
}

const char* Database::kindName() const {
    switch (kind_) {
    case Kind::Ios: return "iOS";
    case Kind::Android: return "Android";
    case Kind::Vita: return "Vita";
    default: return "none";
    }
}

void Database::close() {
    kind_ = Kind::None;
    path_.clear(), root_.clear(), overlayPath_.clear();
    files_.clear();
    loose_.clear();
    entries_.clear(), overlay_.clear();
    byName_.clear(), overlayByName_.clear();
    edits_.clear();
    overlayActive_ = false;
}

bool Database::addArchive(const std::string& path, uint64_t base, bool overlay, std::string* error) {
    auto f = std::make_unique<MvglFile>();
    if (!f->open(path, error, base)) return false;
    const int16_t index = int16_t(files_.size());
    auto& list = overlay ? overlay_ : entries_;
    auto& names = overlay ? overlayByName_ : byName_;
    for (const MvglFile::Entry& e : f->entries()) {
        const std::string k = key(e.name);
        if (names.count(k)) continue; // (the first archive's: the main one, then by name)
        names[k] = list.size();
        list.push_back({e.name, e.offset, e.size, e.stored, index, overlay});
    }
    files_.push_back(std::move(f));
    return true;
}

void Database::addLoose(const std::string& path, const std::string& name, bool overlay) {
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    if (ec || size > 0xffffffffu) return;
    auto& list = overlay ? overlay_ : entries_;
    auto& names = overlay ? overlayByName_ : byName_;
    const std::string k = key(name);
    if (names.count(k)) return;
    names[k] = list.size();
    list.push_back({name, loose_.size(), uint32_t(size), uint32_t(size), -1, overlay});
    loose_.push_back(path);
}

bool Database::open(const std::string& path, std::string* error) {
    close();
    std::error_code ec;
    if (!fs::exists(path, ec)) return fail(error, path + ": not found");
    path_ = path;

    // an .apk / .apks: its stored archives in place (the asset packs inside an .apks are deflated:
    // those need unpacking - a folder with them beside the .apk)
    const bool zip = fs::is_regular_file(path, ec) && (endsWith(path, ".apk") || endsWith(path, ".apks") || endsWith(path, ".zip"));
    std::string dir = zip || fs::is_regular_file(path, ec) ? fs::path(path).parent_path().string() : path;
    if (dir.empty()) dir = ".";

    std::vector<std::pair<std::string, uint64_t>> archives; // (path, base)
    std::vector<std::string> files = walk(dir, zip ? 0 : 8);
    if (zip) files = {path};
    // which release: the main archive found nearest the top decides
    std::string main;
    size_t mainDepth = size_t(-1);
    for (const std::string& f : files) {
        if (!isMain(f)) continue;
        const size_t depth = size_t(std::count(f.begin(), f.end(), '/') + std::count(f.begin(), f.end(), '\\'));
        if (depth < mainDepth) main = f, mainDepth = depth;
    }
    std::vector<std::pair<uint64_t, std::string>> inZip;
    std::string zipPath;
    if (main.empty()) { // (an .apk / .apks given, or beside the asset packs)
        for (const std::string& f : files) {
            if (!(endsWith(f, ".apk") || endsWith(f, ".apks"))) continue;
            std::FILE* zf = std::fopen(f.c_str(), "rb");
            if (!zf) continue;
            std::vector<std::pair<uint64_t, std::string>> found;
            storedArchives(zf, 0, fileSize(zf), 2, found);
            std::fclose(zf);
            for (const auto& [off, n] : found)
                if (isMain(n)) {
                    inZip = found, zipPath = f;
                    break;
                }
            if (!zipPath.empty()) break;
        }
        if (zipPath.empty()) return fail(error, path + ": no Chaos Rings III data (CRDB.ios.mvgl, CRDB.psp2.mvgl, main.*.android.mvgl) found");
    }

    const std::string mainName = lower(baseName(main.empty() ? std::string("main.android.mvgl") : main));
    if (endsWith(mainName, ".ios.mvgl")) kind_ = Kind::Ios;
    else if (endsWith(mainName, ".psp2.mvgl")) kind_ = Kind::Vita;
    else kind_ = Kind::Android;
    gWideAnimations = kind_ != Kind::Vita;

    if (!main.empty()) {
        // iOS / Vita: the main archive's folder is the release's (the Vita's subfolders bgm/, se/,
        // voice/<x>/ in it); Android: the folder given, its asset packs anywhere under it
        root_ = kind_ == Kind::Android ? dir : fs::path(main).parent_path().string();
        const char* suffix = kind_ == Kind::Ios ? ".ios.mvgl" : kind_ == Kind::Vita ? ".psp2.mvgl" : ".android.mvgl";
        archives.emplace_back(main, 0);
        for (const std::string& f : kind_ == Kind::Android ? files : walk(root_, 3))
            if (f != main && endsWith(f, suffix)) archives.emplace_back(f, 0);
    } else {
        root_ = dir;
        for (const auto& [off, n] : inZip)
            if (isMain(n)) archives.emplace(archives.begin(), zipPath, off);
            else archives.emplace_back(zipPath, off);
        for (const std::string& f : walk(dir, 8)) // (asset packs unpacked beside it)
            if (endsWith(f, ".android.mvgl") && !isMain(f)) archives.emplace_back(f, 0);
    }
    for (const auto& [p, base] : archives) {
        std::string why;
        if (!addArchive(p, base, false, &why)) {
            if (files_.empty()) return close(), fail(error, why); // (the main one must open)
        }
    }

    // loose files: movies, subtitles, fonts
    for (const std::string& f : walk(root_, kind_ == Kind::Android ? 3 : 1)) {
        const std::string n = baseName(f), l = lower(n);
        const std::string parent = lower(fs::path(f).parent_path().filename().string());
        const bool movie = endsWith(l, ".mp4") || endsWith(l, ".srt");
        const bool font = (l.compare(0, 5, "f0001") == 0 || l.compare(0, 5, "f0002") == 0) && endsWith(l, ".bin");
        const bool vitaFont = parent == "font" && (endsWith(l, ".ttc") || endsWith(l, ".ttf") || endsWith(l, ".otf"));
        if (movie || font || vitaFont) addLoose(f, vitaFont ? "font\\" + n : n, false);
    }
    return true;
}

bool Database::addOverlay(const std::string& path, std::string* error) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return fail(error, path + ": not found");
    const std::string dir = fs::is_regular_file(path, ec) ? fs::path(path).parent_path().string() : path;
    // its archives' folder: the shallowest one holding a .psp2.mvgl
    std::string at;
    size_t best = size_t(-1);
    for (const std::string& f : walk(dir, 6)) {
        if (!endsWith(f, ".psp2.mvgl")) continue;
        const size_t depth = size_t(std::count(f.begin(), f.end(), '/') + std::count(f.begin(), f.end(), '\\'));
        if (depth < best) best = depth, at = fs::path(f).parent_path().string();
    }
    if (at.empty()) return fail(error, path + ": no translation archives (*.psp2.mvgl) in it");
    const size_t before = overlay_.size();
    for (const std::string& f : walk(at, 3)) {
        if (endsWith(f, ".psp2.mvgl")) {
            std::string why;
            if (!addArchive(f, 0, true, &why) && error) *error = why;
        } else if (endsWith(f, ".srt") || endsWith(f, ".mp4")) {
            addLoose(f, baseName(f), true);
        }
    }
    if (overlay_.size() == before) return fail(error, path + ": its archives hold nothing");
    overlayPath_ = path;
    return true;
}

const Database::Entry* Database::find(const std::string& name) const {
    const std::string k = key(name);
    if (overlayActive_) {
        const auto o = overlayByName_.find(k);
        if (o != overlayByName_.end()) return &overlay_[o->second];
    }
    const auto it = byName_.find(k);
    return it == byName_.end() ? nullptr : &entries_[it->second];
}

bool Database::read(const Entry& e, std::vector<uint8_t>& out, std::string* error) const {
    const auto it = edits_.empty() ? edits_.end() : edits_.find(key(e.name));
    return readStored(e, out, error) && (it == edits_.end() || it->second(out, error));
}

bool Database::readStored(const Entry& e, std::vector<uint8_t>& out, std::string* error) const {
    if (e.file < 0) {
        if (e.offset >= loose_.size()) return fail(error, e.name + ": no such file");
        std::FILE* f = std::fopen(loose_[size_t(e.offset)].c_str(), "rb");
        if (!f) return fail(error, loose_[size_t(e.offset)] + ": cannot open");
        out.resize(e.size);
        const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        std::fclose(f);
        return ok || fail(error, e.name + ": cannot read");
    }
    if (size_t(e.file) >= files_.size()) return fail(error, e.name + ": no archive");
    return files_[size_t(e.file)]->read({e.name, e.offset, e.size, e.stored}, out, error);
}

bool Database::read(const std::string& name, std::vector<uint8_t>& out, std::string* error) const {
    const Entry* e = find(name);
    if (!e) return fail(error, name + ": not in the release");
    return read(*e, out, error);
}

std::string Database::sourceOf(const std::string& name) const {
    const Entry* e = find(name);
    if (!e) return {};
    if (e->file < 0) return e->offset < loose_.size() ? loose_[size_t(e->offset)] : std::string();
    return baseName(files_[size_t(e->file)]->path());
}

void Database::setEdit(const std::string& name, Edit edit) {
    if (edit) edits_[key(name)] = std::move(edit);
    else edits_.erase(key(name));
}

bool Database::addAlias(const std::string& name, const std::string& of) {
    const std::string k = key(name), o = key(of);
    if (byName_.count(k) || overlayByName_.count(k)) return false;
    const auto it = byName_.find(o);
    if (it == byName_.end()) return false;
    Entry e = entries_[it->second];
    e.name = name;
    byName_.emplace(k, entries_.size());
    entries_.push_back(e);
    if (const auto ov = overlayByName_.find(o); ov != overlayByName_.end()) {
        Entry v = overlay_[ov->second];
        v.name = name;
        overlayByName_.emplace(k, overlay_.size());
        overlay_.push_back(v);
    }
    return true;
}

} // namespace cr3
