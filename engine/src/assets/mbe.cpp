#include "assets/mbe.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

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
        t.at = p;
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

bool MbeFile::columnOffset(const Table& t, const char* layout, size_t column, size_t& at, bool& pointer) const {
    const size_t ptr = wide_ ? 8 : 4;
    size_t o = 0, c = 0;
    for (const char* l = layout; *l; ++l) {
        if (*l == '_' || *l == 'p') o = (o + ptr - 1) / ptr * ptr;
        if (*l == '_') continue;
        if (c++ == column) {
            at = o, pointer = *l == 'p';
            return o + (pointer ? ptr : 4) <= t.rowSize;
        }
        o += *l == 'p' ? ptr : 4;
    }
    return false;
}

bool MbeFile::appendRow(const std::string& name, int like, int id, const char* layout, const std::vector<std::pair<size_t, int32_t>>& ints,
                        const std::vector<std::pair<size_t, std::string>>& strings, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error) *error = name + ": " + why;
        return false;
    };
    size_t ti = tables_.size(), from = 0;
    for (size_t k = 0; k < tables_.size(); ++k)
        if (tables_[k].name == name) ti = k;
    if (ti == tables_.size()) return fail("no such table");
    const Table& t = tables_[ti];
    bool found = false;
    for (size_t r : t.rows)
        if (!found && int32_t(u32(d_, r)) == like) from = r, found = true;
    if (!found) return fail("no row " + std::to_string(like));
    // the new row's bytes and its pointer slots (offset in the row, what they point at)
    std::vector<uint8_t> row(d_.begin() + std::ptrdiff_t(from), d_.begin() + std::ptrdiff_t(from + t.rowSize));
    std::vector<std::pair<size_t, std::string>> slots;
    for (const auto& [at, bytes] : chunks_)
        if (at >= from && at < from + t.rowSize) slots.emplace_back(at - from, bytes);
    const auto put = [&](size_t at, int32_t v) { std::memcpy(&row[at], &v, 4); };
    put(0, id);
    for (const auto& [column, v] : ints) {
        size_t at;
        bool pointer;
        if (!columnOffset(t, layout, column, at, pointer) || pointer) return fail("no int column " + std::to_string(column));
        put(at, v);
    }
    for (const auto& [column, text] : strings) {
        size_t at;
        bool pointer;
        if (!columnOffset(t, layout, column, at, pointer) || !pointer) return fail("no pointer column " + std::to_string(column));
        std::string bytes = text;
        bytes.resize((text.size() + 8) / 8 * 8, '\0'); // (NUL-terminated, padded as the game's)
        bool set = false;
        for (auto& s : slots)
            if (s.first == at) s.second = bytes, set = true;
        if (!set) slots.emplace_back(at, bytes);
    }
    // the file again: the tables (rows moved where earlier tables grew), the new row at its
    // table's end, the chunks at their rows' new places
    std::vector<uint8_t> o;
    const auto u32out = [&](uint32_t v) { o.insert(o.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4); };
    const auto align = [&] {
        if (wide_ && o.size() % 8) o.insert(o.end(), 4, 0);
    };
    o.insert(o.end(), {'E', 'X', 'P', 'A'});
    u32out(uint32_t(tables_.size()));
    struct Move {
        size_t from, to, size;
    };
    std::vector<Move> moves;
    size_t added = 0;
    for (size_t k = 0; k < tables_.size(); ++k) {
        const Table& tk = tables_[k];
        const uint32_t len = u32(d_, tk.at);
        o.insert(o.end(), d_.begin() + std::ptrdiff_t(tk.at), d_.begin() + std::ptrdiff_t(tk.at + 4 + len));
        u32out(tk.rowSize);
        u32out(uint32_t(tk.rows.size() + (k == ti)));
        align();
        for (size_t r : tk.rows) {
            moves.push_back({r, o.size(), tk.rowSize});
            o.insert(o.end(), d_.begin() + std::ptrdiff_t(r), d_.begin() + std::ptrdiff_t(r + tk.rowSize));
            align();
        }
        if (k == ti) {
            added = o.size();
            o.insert(o.end(), row.begin(), row.end());
            align();
        }
    }
    std::vector<std::pair<size_t, std::string>> chunks;
    for (const auto& [at, bytes] : chunks_) {
        const auto m = std::upper_bound(moves.begin(), moves.end(), at, [](size_t v, const Move& mv) { return v < mv.from; });
        if (m == moves.begin() || at >= std::prev(m)->from + std::prev(m)->size) return fail("a chunk outside the rows");
        chunks.emplace_back(std::prev(m)->to + (at - std::prev(m)->from), bytes);
    }
    for (const auto& [at, bytes] : slots) chunks.emplace_back(added + at, bytes);
    o.insert(o.end(), {'C', 'H', 'N', 'K'});
    u32out(uint32_t(chunks.size()));
    for (const auto& [at, bytes] : chunks) {
        u32out(uint32_t(at));
        u32out(uint32_t(bytes.size()));
        o.insert(o.end(), bytes.begin(), bytes.end());
    }
    const bool wide = wide_;
    d_ = std::move(o);
    return parseAs(wide, error);
}

} // namespace cr3
