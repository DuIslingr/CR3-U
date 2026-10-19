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
#include <ctime>
#include <map>
#include <random>
#include <set>
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
enum : int {
    kDbOk = 200,
    kDbSomeLeft = 0xce,      // (ReceivePresents: some not taken - no room)
    kDbFull = 0x192,         // (no room for it)
    kDbDone = 0x193,         // (GetDailyItem: had today)
    kDbNone = 500,
    kDbBad = 1000,
    kDbTicketHad = 0x3ef,    // (ReceiveTicket: today's had)
    kDbNoGold = 0x3f1, kDbNoCoins = 0x3f2, kDbNoGP = 0x3f3,
    kDbQuestBusy = 0x3f9,    // (EarnQuestRewards: a quest's prize taken already)
};

// a gene held (CrxGeneData, 0x74 bytes: +0 its gene_para id, +4 its EXP, +8 sixteen command
// slots, +0x5c its database id, +0x6c the time, +0x70 locked)
struct Cr3Gene {
    int master = 0, exp = 0;
    std::vector<int> commands; // (the ones it has learnt beyond its own: a gene box's `commands`)
    int level = 0;             // (utils::GetGene(id, level): made at that level - CrxGene::Initialize;
                               // its EXP the gene's curve's, not the port's yet; 0 by its EXP)
    bool locked = false;
    bool fresh = true;         // (CrxGameData::InitNewGenes: new since it was last looked at)
};

// the presents box (dbutils::SendPresent: 100 at most, the oldest dropped; from PRESENT_FROM_*)
struct Cr3Present {
    int item = 0, count = 0, from = 0;
    bool gene = false; // (then `geneData` is it)
    Cr3Gene geneData;
};

// the quests (CrxGameActivities / DBEmu: by quest _id, 1000 at most)
struct Cr3Quests {
    std::set<int> begun, ended;   // (DAT_014343f8 / 014347e0)
    std::vector<int> offered;     // (FindQuests' pick: quest table indices - DAT_01435b6c, 20 at most)
    int state = 0;                // (DAT_01435b68: utils::GetQuestState - 0 begun, 1 prize taken, 3 ended)
    int endedCount = 0;           // (EndQuest: one more)
    int selected = -100;          // (utils::GetSelectedQuest: DBSelectQuestTask's answer; -100 none chosen)
};

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
    // CrxGameTable::Load: the tables (each by its name: "constant", "bingos", "shops", ...), and
    // the item prices (item_para: MbItemInfo +0x18, GetItemPrice)
    bool load(const Database& release, std::string* error = nullptr);
    bool loaded() const { return !tables_.empty(); }
    const BsonValue* table(const std::string& name) const;
    const std::map<std::string, BsonValue>& tables() const { return tables_; }
    // CrxGameTable's constants (data\constant: the one row's - GOLDKEY_MASTER_ID, GOLD_MAX, ...)
    int constant(const std::string& name, int fallback = 0) const;
    const BsonValue* constants() const;
    const std::vector<Cr3ItemSheet>& itemSheets() const { return sheets_; }
    int findItemSheet(const std::string& id) const; // (CrxGameTable::FindItemSheet; -1 none)
    int itemPrice(int item) const;                  // (GetItemPrice: what a shop pays; 0 none)

    // DBSystem::Request: an offline request runs (its DBEmu, below, the caller's to call) - the
    // others are dropped, as the Vita does, and leave the last result as it was
    bool accepts(int request) const { return cr3Request(request).offline; }
    int lastResult() const { return result_; }  // (utils::GetSyncResult)
    int finish(int code) { return result_ = code; } // (FinishRequest)

    // Auth, offline (the Vita's: CrxGameData::Init - a new game's, the port's GameState() -, the
    // login time, then LoadPlayer and GetThisMonthMaster; the friends and the batch dropped)
    int login(GameState& s, std::time_t now);
    // GetThisMonthMaster: the master tables' dated rows that hold today (CheckDate: MMDD to MMDD
    // of this year, the end day whole, no wrap past the year's end) - the VIP bonus, up to five
    // gene boxes on offer, the flags set, the VS rewards' sheets
    int getThisMonthMaster(GameState& s, std::time_t now);
    struct Month {
        float expBonus = 1, dropBonus = 1, goldBonus = 1;
        std::vector<int> geneBoxes; // (geneBoxSchedules' _id)
        std::string vsScoreSheet, vsSeasonSheet; // (the VS score / season rewards' item sheets)
    };
    const Month& month() const { return month_; }
    // CheckDateChanged: a new day's - the day's marks cleared, then GetThisMonthMaster
    int dateChanged(GameState& s, std::time_t now);

    // DBEmu: the item sheets
    // PickTreasureBox(sheet, n, all): n of its entries drawn (all: any, else those not marked;
    // none left - any at random) - opened() lists them; 500 no such sheet / nothing to draw
    int pickTreasureBox(GameState& s, const std::string& sheet, int n, bool all);
    // OpenTreasureBox(sheet, entry, key, flag): that entry won (utils::OpenGBox / OpenGBoxAndSetFlag)
    int openTreasureBox(GameState& s, const std::string& sheet, int entry, bool key, int flag);
    // OpenItemSheet(sheet, key, flag): one entry won, at random - one not won yet, until the
    // sheet is done; key: a gold key spent (GOLDKEY_MASTER_ID); flag: that flag set (> 0)
    int openItemSheet(GameState& s, const std::string& sheet, bool key, int flag);
    // what was last drawn or won (DAT_0143a089, DAT_0143a0ac / 0143a0b0: Fld2GetOpenItemSheetItem*)
    const std::string& openedSheet() const { return openedSheet_; }
    const std::vector<int>& opened() const { return opened_; }
    const Cr3ItemSheet::Prize* openedPrize(size_t k = 0) const;
    void setOpenedSheet(const std::string& sheet) { openedSheet_ = sheet; } // (Fld2SetItemSheetName)

    // DBEmu: the boxes. OpenGPBox(n, GP): a sheet the player has won from, at random, n of its
    // entries (any); OpenLuckyBox(sheet, n, coins): n from that sheet; OpenGeneBox(box, n, coins):
    // n genes, each from a basket by its `prob`, then any of its genes; OpenPrizeBox("ocean"):
    // one of 16 (item, count) - prizeBox() what
    int openGPBox(GameState& s, int n, int gp);
    int openLuckyBox(GameState& s, const std::string& sheet, int n, int coins);
    int openGeneBox(GameState& s, int box, int n, int coins);
    int openPrizeBox(GameState& s, const std::string& name);
    struct PrizeBox {
        int type = 0, item = 0, count = 0; // (DAT_014a6908 ..: type 1 an item)
        bool toPresents = false;           // (it went to the presents box)
    };
    const PrizeBox& prizeBox() const { return prizeBox_; }
    const std::vector<Cr3Gene>& lastGenes() const { return lastGenes_; } // (the box's genes)

    // DBEmu: the shops (data\shops: a shop's goods - _id, gold, GP, coin, up to 3 items, flag),
    // BuyGoods(shop, goods, n): the prices x n paid (gold, GP, coins - each spent counted), the
    // items given; SellItem(item, n): its price x n in gold; UseItem / UseContinue (a continue
    // gem: CONTINUE_GEM_MASTER_ID); ExchangeGem (99 GEM_MASTER_ID for a continue gem);
    // BuyGeneStock (GENE_STOCK_PRICE coins: the stock's room up by GENE_STOCK_CAPACITY_ADD, to
    // _MAX); GetDailyItem(id): data\dailyItems' row, once - its flag
    int findShop(const std::string& id) const; // (-1 none)
    int buyGoods(GameState& s, int shop, int goods, int n);
    int sellItem(GameState& s, int item, int n);
    int useItem(GameState& s, int item, int n);
    int useContinue(GameState& s);
    int exchangeGem(GameState& s);
    int buyGeneStock(GameState& s);
    int getDailyItem(GameState& s, int id);

    // DBEmu: gifts - MergeGift(id): mergeGifts' gene for the gifts given up (`gifts`: their item
    // ids); ItemizeGift(gift): the gift for itemizeGifts' item
    int mergeGift(GameState& s, int id, const std::vector<int>& gifts);
    int itemizeGift(GameState& s, int gift);

    // DBEmu: the presents box - ReceivePresent(k) / ReceivePresents (taken: a login-bonus coin
    // its LOGIN_BONUS_COIN Oz coins), RejectPresent / DropPresent(k) (thrown away), EarnPresent(n)
    // (1 .. 3 a gene - 10, 13, 11 at its EXP -, 4 a gold key), ClearPresents
    int receivePresent(GameState& s, int k);
    int receivePresents(GameState& s);
    int dropPresent(GameState& s, int k);
    int earnPresent(GameState& s, int n);
    int clearPresents(GameState& s);
    void sendPresent(GameState& s, const Cr3Present& p); // (dbutils::SendPresent)

    // DBEmu: quests (data\quests: _id, no, stime / etime, six flags - < 0 one clear, > 0 one set -,
    // lv, rank, item_id / _count). FindQuests(now): rank 0's all listed; ranks 1 .. 4 drawn into
    // ten slots (4's up to 10 from slot 0, 3's up to 6 from 4, 2's up to 3 from 7, 1's up to 10
    // from 9 down - the later over the earlier), once a day (cr3Daily bit 2), then 5 and 6's up to
    // 10; BeginQuest / EndQuest / SetQuest; EarnQuestRewards(id): questPrizes' - a gene, an item,
    // gold, a gene box or a lucky box
    struct Quest {
        int id = 0, no = 0, stime = 0, etime = 0, flags[6] = {}, level = 0, rank = 0, item = 0, itemCount = 0;
    };
    const std::vector<Quest>& quests() const { return quests_; }
    int findQuests(GameState& s, std::time_t now);
    const std::vector<int>& foundQuests() const { return foundQuests_; }       // (the drawn and 5 / 6's: indices)
    const std::vector<int>& standingQuests() const { return standingQuests_; } // (rank 0's: indices)
    int beginQuest(GameState& s, int id);
    int endQuest(GameState& s, int id);
    int setQuest(GameState& s, int id);
    int earnQuestRewards(GameState& s, int id);

    // DBEmu: tickets - GotTicket: today's received (cr3Daily bit 0); ReceiveTicket: a ticket (item
    // 118), once a day
    int gotTicket(GameState& s);
    bool ticketReceived() const { return ticket_; } // (utils::CheckTicketReceivedResult)
    int receiveTicket(GameState& s);

    // DBEmu::Sync2(name): an event's own gifts (the tutorials' genes, s272's two gifts, ...)
    int sync2(GameState& s, const std::string& name);

    // dbutils::EarnItemPrize / EarnGenePrize: one more of it (an item past its most to the
    // presents - SendPresent, from PRESENT_FROM_SERVICE)
    int earnItemPrize(GameState& s, int item, int count, bool toPresents = true);
    int earnGenePrize(GameState& s, const Cr3Gene& gene);
    int earnGenePrize(GameState& s, int master) { return earnGenePrize(s, Cr3Gene{master}); }

    void seed(uint32_t v) { rng_.seed(v); }

private:
    Cr3SheetProgress& progress(GameState& s, int sheet) const;
    void doOpenItemSheet(GameState& s, int sheet, int entry, bool all, bool key, int flag);
    void markItemSheets(GameState& s) const;   // (CrxGameTable::MarkItemSheet(false))
    void updateItemSheets(GameState& s) const; // (CrxGameTable::UpdateItemSheet(false))
    bool removeItem(GameState& s, int item, int n);
    int earnQuestPrize(GameState& s, const BsonValue& prize);
    uint32_t random() { return uint32_t(rng_()); } // (CrxRandom::GenerateUInt32)

    std::map<std::string, BsonValue> tables_;
    std::map<int, int> prices_;
    std::vector<Cr3ItemSheet> sheets_;
    std::vector<Quest> quests_;
    std::string openedSheet_;
    std::vector<int> opened_;
    std::vector<int> foundQuests_, standingQuests_;
    std::vector<Cr3Gene> lastGenes_;
    Month month_;
    PrizeBox prizeBox_;
    bool ticket_ = false;
    int result_ = kDbOk;
    std::mt19937 rng_{0x43523355u};
};

} // namespace cr3
