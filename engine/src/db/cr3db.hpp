#pragma once
// CR3's game database, offline - as the Vita has it. Every release's code is a client of the
// game's server (DBSystem: Request(id) queues a request, SM_<name> sends it as JSON to the server
// and reads the answer back into CrxGameData), and every release also carries DBEmu - the server's
// rules, played on the device - and the server's tables (`data\*.bson`, CrxGameTable::Load). The
// Vita runs with emulation on (GameSystem +0x1af; Android's +0x1b3 is a debug switch,
// "Emulation"): DBSystem::AddRequest queues only the requests CheckEmulation lists (`offline`
// below), each handler calls its DBEmu function and finishes with the code it returns
// (FinishRequest: 200 done, 500 none such, 1000 an error, ...); the others - friends, the bazaar,
// rankings, payments, the colosseum online - are dropped unqueued. The port is that, on every
// release: it never talks to a server.

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "assets/bson.hpp"
#include "assets/mvgl.hpp"

namespace cr3 {

struct GameState;

// DBSystem's requests (the name table after its handlers: DBRequest::Init's ids)
struct Cr3Request {
    const char* name;
    bool offline; // (DBSystem::CheckEmulation: kept - its DBEmu - on the Vita)
};
constexpr int kCr3RequestCount = 0xa4;
const Cr3Request& cr3Request(int id);
int cr3RequestId(const char* name); // -1 none

// FinishRequest's codes (DBSystem +0x1bc: utils::GetSyncResult)
enum : int { kDbOk = 200, kDbFull = 0x192, kDbNone = 500, kDbBad = 1000, kDbTooMany = 0x3f3 };

// an item sheet (CrxItemSheetTable, 0x150 bytes: data\bingos.bson - a dungeon's treasure boxes'
// prizes and the prize for them all), its entries' category 1 a gene, else an item (by its full
// id: 6xxxxx a gift)
struct Cr3ItemSheet {
    std::string id; // ("d02")
    struct Prize {
        int item = 0;
        bool gene = false;
    };
    std::vector<Prize> entries; // (+0x28, 0x10 each - 17 at most)
    Prize result;               // (+0x138)
};

// a sheet's progress - the save's (DBWriter::WriteItemsheets; CrxItemSheetTable +0x30 / +0x34
// an entry's, +0x140 / +0x148 / +0x149 / +0x14a the sheet's)
struct Cr3SheetProgress {
    std::vector<int> opened; // (each entry: how many times won)
    std::vector<bool> mark;  // (newly won since MarkItemSheet - its "new" mark; PickTreasureBox skips them)
    int completed = 0;       // (the whole sheet's prize, how many times)
    bool completedNew = false, completedSeen = false, changed = false;
};

class Cr3OfflineDb {
public:
    // CrxGameTable::Load: the tables (each by its name: "constant", "bingos", "shops", ...)
    bool load(const Database& release, std::string* error = nullptr);
    bool loaded() const { return !tables_.empty(); }
    const BsonValue* table(const std::string& name) const;
    const std::map<std::string, BsonValue>& tables() const { return tables_; }
    // CrxGameTable's constants (data\constant: the one row's - GOLDKEY_MASTER_ID, GOLD_MAX, ...)
    int constant(const std::string& name, int fallback = 0) const;
    const BsonValue* constants() const;
    const std::vector<Cr3ItemSheet>& itemSheets() const { return sheets_; }
    int findItemSheet(const std::string& id) const; // (CrxGameTable::FindItemSheet; -1 none)

    // DBSystem::Request: an offline request runs (its DBEmu, below, the caller's to call) - the
    // others are dropped, as the Vita does, and leave the last result as it was
    bool accepts(int request) const { return cr3Request(request).offline; }
    int lastResult() const { return result_; }  // (utils::GetSyncResult)
    void finish(int code) { result_ = code; }   // (FinishRequest)

    // DBEmu: the item sheets
    // PickTreasureBox(sheet, n, all): n of its entries drawn (all: any, else those not marked;
    // none left - any at random) - opened() lists them; 500 no such sheet / nothing to draw
    int pickTreasureBox(GameState& s, const std::string& sheet, int n, bool all);
    // OpenTreasureBox(sheet, entry, key, flag): that entry won (1000: none such)
    int openTreasureBox(GameState& s, const std::string& sheet, int entry, bool key, int flag);
    // OpenItemSheet(sheet, key, flag): one entry won, at random - one not won yet, until the
    // sheet is done; key: a gold key spent (GOLDKEY_MASTER_ID); flag: that flag set (> 0)
    int openItemSheet(GameState& s, const std::string& sheet, bool key, int flag);
    // what was last drawn or won (DAT_0143a089, DAT_0143a0ac / 0143a0b0: Fld2GetOpenItemSheetItem*)
    const std::string& openedSheet() const { return openedSheet_; }
    const std::vector<int>& opened() const { return opened_; }
    const Cr3ItemSheet::Prize* openedPrize(size_t k = 0) const;
    void setOpenedSheet(const std::string& sheet) { openedSheet_ = sheet; } // (Fld2SetItemSheetName)

    // dbutils::EarnItemPrize / EarnGenePrize: one more of it (an item past its most to the
    // presents - SendPresent, from PRESENT_FROM_SERVICE)
    int earnItemPrize(GameState& s, int item, int count, bool toPresents = true);
    int earnGenePrize(GameState& s, int gene);

    void seed(uint32_t v) { rng_.seed(v); }

private:
    Cr3SheetProgress& progress(GameState& s, int sheet) const;
    void doOpenItemSheet(GameState& s, int sheet, int entry, bool all, bool key, int flag);
    void markItemSheets(GameState& s) const;   // (CrxGameTable::MarkItemSheet(false))
    void updateItemSheets(GameState& s) const; // (CrxGameTable::UpdateItemSheet(false))

    std::map<std::string, BsonValue> tables_;
    std::vector<Cr3ItemSheet> sheets_;
    std::string openedSheet_;
    std::vector<int> opened_;
    int result_ = kDbOk;
    std::mt19937 rng_{0x43523355u};
};

} // namespace cr3
