#include "assets/release.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef CR3_HAVE_ZLIB
#include <zlib.h>
#endif

namespace cr3 {

namespace {

namespace fs = std::filesystem;

constexpr const char* kAndroidLibrary = "lib/arm64-v8a/libcrx.so";

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool endsWith(const std::string& s, const std::string& sfx) {
    return s.size() >= sfx.size() && s.compare(s.size() - sfx.size(), sfx.size(), sfx) == 0;
}

bool readWhole(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    const bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

// the folder an archive path lies in ("." for a bare name)
std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::vector<std::string> filesIn(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

std::string resolveArchive(const std::string& path) { return path; } // (Database::open finds the release in it)

// a game folder something was put in (the release's own come with a README.txt each - those
// alone, or a dot file, are none)
static bool holdsCopy(const std::string& dir) {
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        std::string n = e.path().filename().string();
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (n != "readme.txt" && !n.empty() && n[0] != '.') return true;
    }
    return false;
}

// a folder with a release in it, a few folders down at most: a CRDB.*.mvgl, Android's main.*.mvgl, an OBB or an .apks (an
// older place's name alone - a "data" folder in the home folder, a double-click's working folder - is no copy)
static bool holdsRelease(const std::string& dir) {
    std::error_code ec;
    if (fs::is_regular_file(dir, ec)) return true; // (an archive given by its own name)
    size_t seen = 0;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && !ec && seen < 20000; it.increment(ec), ++seen) {
        if (it.depth() >= 4) it.disable_recursion_pending();
        std::string n = it->path().filename().string();
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        const auto ends = [&](const char* t) { const size_t k = std::strlen(t); return n.size() >= k && n.compare(n.size() - k, k, t) == 0; };
        if (((n.compare(0, 5, "crdb.") == 0 || n.compare(0, 5, "main.") == 0) && ends(".mvgl")) || ends(".obb") || ends(".apks")) return true;
    }
    return false;
}

std::string defaultArchive(const std::vector<std::string>& places) {
    std::error_code ec;
    if (const char* env = std::getenv("CR3_DATA"); env && *env) return env;
    // vita/, then android/, then ios/ in each game folder - the first with a copy in it
    // (Database::open finds the release under it, however deep) -, then the older places
    for (const char* kind : {"vita", "android", "ios"})
        for (std::string base : places) {
            if (base.empty()) continue;
            if (base.back() != '/' && base.back() != '\\') base += '/';
            if (fs::is_directory(base + kind, ec) && holdsCopy(base + kind)) return base + kind;
        }
    for (std::string base : places) {
        if (base.empty()) continue;
        if (base.back() != '/' && base.back() != '\\') base += '/';
        for (const char* sub : {"ipad", "PCSG00500", "PCSG00457", "ChaosRings3_FINAL_ENG.app", "Payload", "data"}) {
            const std::string a = base + sub;
            if (fs::exists(a, ec) && holdsRelease(a)) return a;
        }
    }
    return {};
}

std::string defaultArchive(const std::string& baseDir) { return defaultArchive(std::vector<std::string>{baseDir}); }

namespace {
bool seekAt(std::FILE* f, uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, int64_t(at), SEEK_SET) == 0;
#else
    return fseeko(f, off_t(at), SEEK_SET) == 0;
#endif
}
uint64_t sizeOf(std::FILE* f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    return uint64_t(_ftelli64(f));
#else
    fseeko(f, 0, SEEK_END);
    return uint64_t(ftello(f));
#endif
}
struct ZipItem {
    std::string name;
    uint16_t method = 0;
    uint32_t packed = 0, unpacked = 0;
    uint64_t data = 0; // (in the host file)
};
// the entries of a zip at `base` (`size` long) in its host file - an .apks' APKs are zips stored in it
bool zipItems(std::FILE* f, uint64_t base, uint64_t size, std::vector<ZipItem>& out) {
    out.clear();
    const uint64_t tail = std::min<uint64_t>(size, 0x10000 + 22); // (the end record: its comment at most 65535 bytes)
    std::vector<uint8_t> t(static_cast<size_t>(tail));
    if (t.size() < 22 || !seekAt(f, base + size - tail) || std::fread(t.data(), 1, t.size(), f) != t.size()) return false;
    size_t end = t.size() - 22 + 1;
    while (end-- > 0)
        if (le32(&t[end]) == 0x06054b50) break;
    if (end == size_t(-1)) return false;
    const uint32_t count = le16(&t[end + 10]), dirSize = le32(&t[end + 12]), dirOffset = le32(&t[end + 16]);
    std::vector<uint8_t> d(dirSize);
    if (!seekAt(f, base + dirOffset) || std::fread(d.data(), 1, d.size(), f) != d.size()) return false;
    for (size_t p = 0, k = 0; k < count && p + 46 <= d.size(); ++k) {
        if (le32(&d[p]) != 0x02014b50) break;
        ZipItem e;
        e.method = le16(&d[p + 10]);
        e.packed = le32(&d[p + 20]), e.unpacked = le32(&d[p + 24]);
        const uint16_t nameLen = le16(&d[p + 28]), extraLen = le16(&d[p + 30]), commentLen = le16(&d[p + 32]);
        const uint32_t local = le32(&d[p + 42]);
        e.name.assign(reinterpret_cast<const char*>(&d[p + 46]), std::min<size_t>(nameLen, d.size() - p - 46));
        p += 46 + size_t(nameLen) + extraLen + commentLen;
        uint8_t h[30];
        if (!seekAt(f, base + local) || std::fread(h, 1, sizeof h, f) != sizeof h || le32(h) != 0x04034b50) continue;
        e.data = base + local + 30 + le16(h + 26) + le16(h + 28);
        out.push_back(std::move(e));
    }
    return true;
}
bool readItem(std::FILE* f, const ZipItem& e, std::vector<uint8_t>& out, std::string* error) {
    std::vector<uint8_t> raw(e.packed);
    if (!seekAt(f, e.data) || std::fread(raw.data(), 1, raw.size(), f) != raw.size()) return fail(error, e.name + ": truncated");
    if (e.method == 0) return out = std::move(raw), true;
    if (e.method != 8) return fail(error, e.name + ": compressed in a way not read here");
#ifdef CR3_HAVE_ZLIB
    out.assign(e.unpacked, 0);
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return fail(error, "zlib could not start");
    z.next_in = raw.data(), z.avail_in = uInt(raw.size());
    z.next_out = out.data(), z.avail_out = uInt(out.size());
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (r != Z_STREAM_END || z.total_out != e.unpacked) return fail(error, e.name + ": does not inflate");
    return true;
#else
    return fail(error, e.name + ": deflated (built without zlib)");
#endif
}
} // namespace

bool readZipEntry(const std::string& zip, const std::string& name, std::vector<uint8_t>& out, std::string* error) {
    std::FILE* f = std::fopen(zip.c_str(), "rb");
    if (!f) return fail(error, zip + ": cannot open");
    std::vector<ZipItem> items;
    const bool listed = zipItems(f, 0, sizeOf(f), items);
    bool found = false, ok = false;
    for (const ZipItem& e : items)
        if (e.name == name) {
            found = true, ok = readItem(f, e, out, error);
            break;
        }
    std::fclose(f);
    if (!listed) return fail(error, zip + ": not a zip");
    return found ? ok : fail(error, zip + ": no " + name);
}

namespace {
// `name` from the game's own APK inside an .apks / .xapk (stored there, as they are): the APK with
// the game's library, its entry read in place
bool readFromBundle(const std::string& bundle, const std::string& name, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(bundle.c_str(), "rb");
    if (!f) return false;
    std::vector<ZipItem> apks;
    bool ok = false;
    if (zipItems(f, 0, sizeOf(f), apks))
        for (const ZipItem& a : apks) {
            if (a.method != 0 || a.packed != a.unpacked || a.name.size() < 4 || a.name.compare(a.name.size() - 4, 4, ".apk") != 0) continue;
            std::vector<ZipItem> in;
            if (!zipItems(f, a.data, a.packed, in)) continue;
            const bool game = std::any_of(in.begin(), in.end(), [](const ZipItem& e) { return e.name == kAndroidLibrary; });
            if (!game) continue;
            for (const ZipItem& e : in)
                if (e.name == name) {
                    ok = readItem(f, e, out, nullptr);
                    break;
                }
            if (ok) break;
        }
    std::fclose(f);
    return ok;
}
} // namespace

bool androidFile(const std::string& archivePath, const std::string& name, std::vector<uint8_t>& out, std::string* from) {
    // (the folder given - android/, as a start finds it -, else the given file's)
    std::error_code ec;
    std::string dir = resolveArchive(archivePath);
    if (fs::is_directory(dir, ec)) {
        while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    } else dir = folderOf(dir);
    for (const std::string& p : {dir + "/" + name, dir + "/extracted/" + name})
        if (readWhole(p, out)) {
            if (from) *from = p;
            return true;
        }
    // (an .apks / .xapk - given, or in the folder -: the game's APK inside it)
    const std::string given = resolveArchive(archivePath);
    std::vector<std::string> bundles;
    if (endsWith(given, ".apks") || endsWith(given, ".xapk")) bundles.push_back(given);
    for (const std::string& n : filesIn(dir))
        if ((endsWith(n, ".apks") || endsWith(n, ".xapk")) && dir + "/" + n != given) bundles.push_back(dir + "/" + n);
    for (const std::string& b : bundles)
        if (readFromBundle(b, name, out)) {
            if (from) *from = b + ":" + name;
            return true;
        }
    // (the APKs beside it, and those a bundle holding them compressed was unpacked to: unpacked/)
    for (const std::string& d : {dir, dir + "/unpacked"})
        for (const std::string& n : filesIn(d)) {
            if (!endsWith(n, ".apk")) continue;
            std::vector<uint8_t> lib; // (the game's own APK: the one with its library)
            if (name != kAndroidLibrary && !readZipEntry(d + "/" + n, kAndroidLibrary, lib)) continue;
            if (readZipEntry(d + "/" + n, name, out)) {
                if (from) *from = d + "/" + n + ":" + name;
                return true;
            }
        }
    return false;
}

namespace {
// the table in a 32-bit executable: its four figure names' strings, then four words pointing at
// them (the same distances apart: the pointers' base less the file's, the delta), the 544
// pointers before them the lines
bool findStaffRoll(const std::vector<uint8_t>& b, std::vector<std::string>& lines, std::string* figures) {
    static const char* const kNames[4] = {"end2_BG", "end1_BG", "end3_BG", "end4_BG"};
    auto find = [&](const char* s) -> int64_t {
        const size_t n = std::strlen(s) + 1;
        for (size_t o = 0; o + n <= b.size(); ++o)
            if (std::memcmp(&b[o], s, n) == 0 && (o == 0 || b[o - 1] == 0)) return int64_t(o);
        return -1;
    };
    int64_t at[4];
    for (int k = 0; k < 4; ++k)
        if ((at[k] = find(kNames[k])) < 0) return false;
    auto word = [&](size_t o) { return uint32_t(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | uint32_t(b[o + 3]) << 24); };
    for (size_t o = 544 * 4; o + 16 <= b.size(); o += 4) {
        const int64_t delta = int64_t(word(o)) - at[0];
        bool ok = true;
        for (int k = 1; k < 4 && ok; ++k) ok = int64_t(word(o + 4 * size_t(k))) - at[k] == delta;
        if (!ok) continue;
        lines.clear();
        for (size_t i = 0; i < 544 && ok; ++i) { // (a run of words that only looks so: not every one a string)
            const int64_t s = int64_t(word(o - 544 * 4 + 4 * i)) - delta;
            if (s < 0 || size_t(s) >= b.size()) {
                ok = false;
                break;
            }
            std::string t;
            for (size_t c = size_t(s); c < b.size() && b[c]; ++c) t += char(b[c]);
            lines.push_back(t);
        }
        if (!ok) continue;
        if (figures)
            for (int k = 0; k < 4; ++k) figures[k] = kNames[k];
        return true;
    }
    return false;
}
} // namespace

bool releaseStaffRoll(const std::string& archivePath, const std::string& binary, std::vector<std::string>& lines, std::string figures[4]) {
    const std::string archive = resolveArchive(archivePath);
    std::vector<std::string> candidates;
    if (!binary.empty()) candidates.push_back(binary);
    for (const std::string& c : mapTableCandidates(archive)) candidates.push_back(c);
    std::vector<uint8_t> bytes;
    for (const std::string& c : candidates)
        if (readWhole(c, bytes) && findStaffRoll(bytes, lines, figures)) return true;
    return androidFile(archive, kAndroidLibrary, bytes) && findStaffRoll(bytes, lines, figures);
}

bool releaseMapTable(const std::string& archivePath, const std::string& binary, std::vector<MapInfo>& table, std::string* from) {
    const std::string archive = resolveArchive(archivePath);
    std::vector<std::string> candidates;
    if (!binary.empty()) candidates.push_back(binary);
    for (const std::string& c : mapTableCandidates(archive)) candidates.push_back(c);
    std::vector<uint8_t> bytes;
    for (const std::string& c : candidates)
        if (readWhole(c, bytes) && findMapTable(bytes, table)) {
            if (from) *from = c;
            return true;
        }
    std::string where;
    if (androidFile(archive, kAndroidLibrary, bytes, &where) && findMapTable(bytes, table)) {
        if (from) *from = where;
        return true;
    }
    return false;
}

} // namespace cr3
