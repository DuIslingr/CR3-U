#include "assets/bson.hpp"

#include <algorithm>
#include <cstring>

namespace cr3 {

const BsonValue* BsonValue::get(std::string_view key) const {
    for (const auto& [name, value] : members)
        if (name == key) return &value;
    return nullptr;
}

int BsonValue::integer(std::string_view key, int fallback) const {
    const BsonValue* v = get(key);
    return v && (v->kind == Kind::Int || v->kind == Kind::Float) ? v->asInt() : fallback;
}

float BsonValue::number(std::string_view key, float fallback) const {
    const BsonValue* v = get(key);
    return v && (v->kind == Kind::Int || v->kind == Kind::Float) ? v->asFloat() : fallback;
}

std::string BsonValue::string(std::string_view key) const {
    const BsonValue* v = get(key);
    return v && v->kind == Kind::String ? v->s : std::string();
}

namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    size_t text = 0, textSize = 0; // (the text chunk's strings: from, how many bytes)
    std::string error;

    uint32_t u32(size_t o) const {
        uint32_t v;
        std::memcpy(&v, d.data() + o, 4);
        return v;
    }
    // BsonMarshaller::GetChunkSize
    size_t size(size_t o) const { return o + 4 <= d.size() ? size_t(d[o + 1]) * 0xffff + (d[o + 2] | d[o + 3] << 8) : 0; }
    std::string str(uint32_t off) const {
        if (off >= textSize) return {};
        const char* p = reinterpret_cast<const char*>(d.data() + text + off);
        return std::string(p, strnlen(p, textSize - off));
    }
    bool value(size_t o, BsonValue& out, int depth) {
        const size_t n = size(o);
        if (n < 8 || o + n > d.size() || depth > 64) return error = "a bad chunk at " + std::to_string(o), false;
        switch (d[o]) {
        case 1:
        case 2: {
            if (n < 12) return error = "a short list at " + std::to_string(o), false;
            out.kind = d[o] == 1 ? BsonValue::Kind::Object : BsonValue::Kind::Array;
            const size_t end = o + n;
            for (size_t q = o + 12; q + 4 <= end && d[q] != 3 && size(q) > 0; q += size(q)) {
                BsonValue v;
                if (!value(q, v, depth + 1)) return false;
                if (out.kind == BsonValue::Kind::Array) out.items.push_back(std::move(v));
                else out.members.emplace_back(str(u32(q + 4)), std::move(v));
            }
            return true;
        }
        case 0x10: out.kind = BsonValue::Kind::Int, out.i = int32_t(u32(o + 8)); return n >= 12;
        case 0x11: {
            out.kind = BsonValue::Kind::Float;
            const uint32_t bits = u32(o + 8);
            std::memcpy(&out.f, &bits, 4);
            return n >= 12;
        }
        case 0x14: out.kind = BsonValue::Kind::String, out.s = str(u32(o + 8)); return n >= 12;
        default: return true; // (none other in the tables: left None)
        }
    }
};

} // namespace

bool parseBson(const std::vector<uint8_t>& d, BsonValue& out, std::string* error) {
    out = BsonValue();
    if (d.size() < 12 || std::memcmp(d.data(), "BSON\n", 5) != 0) {
        if (error) *error = "not MVGL BSON";
        return false;
    }
    Reader r{d};
    // BsonMarshaller::Load: the text chunk - the chunks after the header, up to an end (3)
    for (size_t o = 8; o + 4 <= d.size() && d[o] != 3 && r.size(o) > 0; o += r.size(o))
        if (d[o] == 0x20) {
            r.text = o + 4, r.textSize = std::min(r.size(o) - 4, d.size() - (o + 4));
            break;
        }
    if (!r.value(8, out, 0)) {
        if (error) *error = r.error;
        return false;
    }
    return true;
}

} // namespace cr3
