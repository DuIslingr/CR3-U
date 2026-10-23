#pragma once
// CR3's parameter tables (data\*.mbe: MbParameterManager::structParam; tools/mbe.py). 'EXPA', u32
// tables, then each: u32 name length, the name, u32 row size, u32 rows, the rows; 'CHNK', u32
// count, then each: u32 file offset, u32 size, the bytes - the pointer slot at that offset points
// at them (a string, or an array: mon_cop's monster ids). The 64-bit builds (iOS, Android) start
// each row 8-aligned and keep pointer slots 8 bytes, 8-aligned; the Vita's are 4 and packed. The
// columns' types are not in the file: a row is read by a layout - 'i' int, 'f' float, 'p' a
// pointer slot (aligned), '_' the pointer alignment alone (where the game's struct pads before
// a pointer-holding member).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cr3 {

class MbeFile {
public:
    bool parse(const std::vector<uint8_t>& file, std::string* error = nullptr);
    bool wide() const { return wide_; }

    struct Table {
        std::string name;
        uint32_t rowSize = 0;
        std::vector<size_t> rows; // (their file offsets)
        size_t at = 0;            // (its name's length's offset)
    };
    const Table* table(const std::string& name) const;
    const std::vector<Table>& tables() const { return tables_; }

    // a row's columns, by the layout
    struct Row {
        std::vector<int32_t> ints;      // ('i' and 'f' columns: the bits; 'p': 0)
        std::vector<std::string> bytes; // ('p' columns: what the slot points at; empty: none)
        int i(size_t c) const { return c < ints.size() ? ints[c] : 0; }
        float f(size_t c) const;
        std::string s(size_t c) const;          // (to its NUL)
        int32_t element(size_t c, size_t k) const; // (a pointed-at array's int k; -1 past it)
    };
    bool row(const Table& t, size_t index, const char* layout, Row& out) const;
    // the first row whose column 0 (an int) is `id`
    bool find(const std::string& table, int id, const char* layout, Row& out) const;

    // CR3-U's add-ons: a new row at `table`'s end - a copy of the row whose id is `like` (its
    // strings too), with columns of the layout set: ints, and pointer columns to strings of their
    // own; column 0 becomes `id`. bytes() is the file with it.
    bool appendRow(const std::string& table, int like, int id, const char* layout, const std::vector<std::pair<size_t, int32_t>>& ints,
                   const std::vector<std::pair<size_t, std::string>>& strings, std::string* error = nullptr);
    const std::vector<uint8_t>& bytes() const { return d_; }

private:
    bool parseAs(bool wide, std::string* error);
    bool columnOffset(const Table& t, const char* layout, size_t column, size_t& at, bool& pointer) const;
    std::vector<uint8_t> d_;
    bool wide_ = true;
    std::vector<Table> tables_;
    std::vector<std::pair<size_t, std::string>> chunks_; // (slot offset, bytes), sorted
};

} // namespace cr3
