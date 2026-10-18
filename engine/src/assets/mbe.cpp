#include "assets/mbe.hpp"

#include <algorithm>
#include <cstring>

#include "assets/mvgl.hpp"

namespace cr3 {

namespace {

uint32_t u32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
    return v;
}

} // namespace

bool MbeFile::parse(const std::vector<uint8_t>& file, std::string* error) {
    d_ = file;
    if (d_.size() < 8 || std::memcmp(d_.data(), "EXPA", 4) != 0) {
        if (error) *error = "not an EXPA table file";
        return false;
    }
    // (the release's layout first - gWideAnimations, the 64-bit builds' -, else the other: a
    // table of rows a multiple of 8 long reads either way, so the layout can't tell itself)
    std::string e;
    if (parseAs(gWideAnimations, &e)) return true;
    return parseAs(!gWideAnimations, error);
}

bool MbeFile::parseAs(bool wide, std::string* error) {
    wide_ = wide;
    tables_.clear(), chunks_.clear();
    const uint32_t n = u32(d_, 4);
    size_t p = 8;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t len = u32(d_, p);
        if (len > 256 || p + 4 + len + 8 > d_.size()) {
            if (error) *error = "table " + std::to_string(k) + " past the file's end";
            return false;
        }
        Table t;
        t.name.assign(reinterpret_cast<const char*>(&d_[p + 4]), len);
        t.name.resize(std::strlen(t.name.c_str()));
        p += 4 + len;
        t.rowSize = u32(d_, p);
        const uint32_t rows = u32(d_, p + 4);
        if (t.rowSize == 0 || rows > d_.size() || p + uint64_t(t.rowSize) * rows > d_.size()) {
            if (error) *error = "table " + std::to_string(k) + " past the file's end";
            return false;
        }
        p += 8;
        if (wide && p % 8) p += 4;
        for (uint32_t r = 0; r < rows; ++r) {
            t.rows.push_back(p);
            p += t.rowSize;
            if (wide && p % 8) p += 4;
        }
        tables_.push_back(std::move(t));
    }
    if (p + 8 > d_.size() || std::memcmp(&d_[p], "CHNK", 4) != 0) {
        if (error) *error = "no CHNK";
        return false;
    }
    const uint32_t count = u32(d_, p + 4);
    p += 8;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t at = u32(d_, p), size = u32(d_, p + 4);
        if (p + 8 + uint64_t(size) > d_.size()) {
            if (error) *error = "chunk past the file's end";
            return false;
        }
        chunks_.emplace_back(at, std::string(reinterpret_cast<const char*>(&d_[p + 8]), size));
        p += 8 + size;
    }
    std::sort(chunks_.begin(), chunks_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return true;
}

const MbeFile::Table* MbeFile::table(const std::string& name) const {
    for (const Table& t : tables_)
        if (t.name == name) return &t;
    return nullptr;
}

bool MbeFile::row(const Table& t, size_t index, const char* layout, Row& out) const {
    out = Row();
    if (index >= t.rows.size()) return false;
    const size_t base = t.rows[index];
    const size_t ptr = wide_ ? 8 : 4;
    size_t at = 0;
    for (const char* c = layout; *c; ++c) {
        if (*c == '_' || *c == 'p') at = (at + ptr - 1) / ptr * ptr;
        if (*c == '_') continue;
        if (at + (*c == 'p' ? ptr : 4) > t.rowSize) return false;
        if (*c == 'p') {
            const auto it = std::lower_bound(chunks_.begin(), chunks_.end(), base + at, [](const auto& a, size_t v) { return a.first < v; });
            out.ints.push_back(0);
            out.bytes.push_back(it != chunks_.end() && it->first == base + at ? it->second : std::string());
            at += ptr;
        } else {
            out.ints.push_back(int32_t(u32(d_, base + at)));
            out.bytes.emplace_back();
            at += 4;
        }
    }
    return true;
}

bool MbeFile::find(const std::string& name, int id, const char* layout, Row& out) const {
    const Table* t = table(name);
    if (!t) return false;
    for (size_t r = 0; r < t->rows.size(); ++r)
        if (int32_t(u32(d_, t->rows[r])) == id) return row(*t, r, layout, out);
    return false;
}

float MbeFile::Row::f(size_t c) const {
    float v = 0;
    const int32_t bits = i(c);
    std::memcpy(&v, &bits, 4);
    return v;
}

std::string MbeFile::Row::s(size_t c) const {
    if (c >= bytes.size()) return {};
    return std::string(bytes[c].c_str());
}

int32_t MbeFile::Row::element(size_t c, size_t k) const {
    if (c >= bytes.size() || (k + 1) * 4 > bytes[c].size()) return -1;
    int32_t v;
    std::memcpy(&v, bytes[c].data() + k * 4, 4);
    return v;
}

} // namespace cr3
