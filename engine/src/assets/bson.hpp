#pragma once
// MVGL's "BSON" (`data\*.bson`: the game server's tables, shipped in every release - CrxGameTable::
// Load, BsonMarshaller): not MongoDB's. "BSON\n" and 3 bytes, then chunks - a type byte, the size
// (byte 1 x 0xffff + the u16 at 2, the header's 4 bytes counted), the name (+4: an offset into
// the text chunk), the value (+8). The first chunk is the root; type 0x20 is the text chunk (its
// strings from +4, NUL-ended, the names' and the strings' - its size short of the last few, read
// to their NULs regardless); 3 ends a list. 1 an object and 2 an
// array: their count at +8, their chunks from +0xc to their own end; 0x10 an i32, 0x11 an f32,
// 0x14 a string (+8: its offset into the text).

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cr3 {

struct BsonValue {
    enum class Kind : uint8_t { None, Object, Array, Int, Float, String };
    Kind kind = Kind::None;
    int32_t i = 0;
    float f = 0;
    std::string s;
    std::vector<std::pair<std::string, BsonValue>> members; // (an object's, in order)
    std::vector<BsonValue> items;                           // (an array's)

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    // BsonMarshaller::GetI32 / GetF32: a number either way (a float truncated)
    int asInt() const { return kind == Kind::Int ? i : kind == Kind::Float ? int(f) : 0; }
    float asFloat() const { return kind == Kind::Float ? f : kind == Kind::Int ? float(i) : 0.0f; }
    // an object's member (nullptr: none)
    const BsonValue* get(std::string_view key) const;
    int integer(std::string_view key, int fallback = 0) const;
    float number(std::string_view key, float fallback = 0) const;
    std::string string(std::string_view key) const;
    size_t size() const { return kind == Kind::Array ? items.size() : members.size(); }
};

bool parseBson(const std::vector<uint8_t>& data, BsonValue& out, std::string* error = nullptr);

} // namespace cr3
