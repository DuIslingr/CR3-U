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

#include <array>
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

// a gene held (CrxGeneData, 0x74 bytes: +0 its gene_para id, +4 its EXP, +8 eight (type, id)
// slots - 0 a command, 1 a skill; 0 .. 5 its own, learnt by level, 6 and 7 inherited -, +0x48
// what items have added to its attack, guard, speed, luck, +0x5c its database id, +0x70 locked)
struct Cr3Gene {
    int master = 0, exp = 0;
    int slots[8][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
    int adds[4] = {};
    bool locked = false;
    bool stocked = false;      // (in the gene stock - CrxGameData +0x16..., GENE_STOCK_CAPACITY_* - not the 50)
    bool fresh = true;         // (CrxGameData::InitNewGenes: new since it was last looked at)
    int uid = 0;               // (+0x5c: its database id - what a character wears; 0 not given yet - GameState::geneUid)
    Cr3Gene() = default;
    Cr3Gene(int m, int e = 0) : master(m), exp(e) {}
};

// the VS mode's part of the save (CrxGameData +0x28550 ..; the Vita's DBEmu keeps it at 0x81641b38 ..)
struct Cr3Vs {
    int score = 0;          // (b38: the tournament's - EndVSBattle's, VS_SCORE_MAX at most)
    int weekScore = 0;      // (b3c: the week's, from Monday - CheckVSScoreReset; 999999999 at most)
    int weekRewarded = 0;   // (b40: the week's score its rewards were given up to)
    int64_t weekFrom = 0;   // (b48: when the week's score began, epoch seconds; 0 never)
    // the day's mission (GetVSMission - vsmissions_cli: b50 its _id, b54 this tournament's tally,
    // b58 the day's, b5c the goal, b60 its level, b64 its type, b68 the boost item it gives, b6c done)
    int mission = 0, missionTally = 0, missionTotal = 0, missionGoal = 0, missionLevel = 0, missionType = 0, missionBoost = 0;
    bool missionCleared = false;
    int missionRank[2] = {1, 1}, missionBest[2] = {1, 1}; // (b70 / b74 the rank - type 0's, the others' -, b78 / b7c the best)
    int missionsCleared = 0;                               // (b80)
    // the season (be0 ..: vsseasons' row whose dates hold): its _id (-1 none), its points
    int season = -1, seasonPoints = 0, seasonCount = 0;
    bool seasonDrop = false; // (be4: this tournament may drop the season's item - season_prob, or the bonus boost)
    std::map<int, int> boosts; // (c00 ..: vsboostitems _id -> how many held, VS_BOOST_ITEM_COUNT_MAX at most)
    // the tournament (c68 ..): playing, its battles won, its prize given, the last of each mode
    bool playing = false, rewarded = false;
    int battles = 0, last[2] = {-1, -1};
    std::set<int> cleared; // (DBSystem::CheckVSCompletedFlag: the tournaments cleared)
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

    // genes (gene_para.mbe Gene / ExpTable, gene_command_skill.mbe, data\mergeGene2, the collection
    // book): a gene's numbers (MbGeneInfo: +4 its series, +8 grade, +0xc top level, +0x10 EXP curve,
    // +0x14 rank, +0x18 type, +0x20 / +0x24 its EXP worth at level 1 / a level more, +0x28 / +0x2c
    // its price likewise, +0x30 kind - its element, +0x34 HP, +0x38 MP, +0x3c attack | defence and
    // +0x40 speed | luck (s16s), +0x44 .. +0x50 their growth a level (in hundredths), +0x58 its card's
    // stars)
    struct GeneInfo {
        int id = 0, series = 0, grade = 0, maxLevel = 1, curve = 0, rank = 0, type = 0;
        int worth = 0, worthPerLevel = 0, price = 0, pricePerLevel = 0, kind = 0, stars = 0;
        int base[6] = {}, growth[6] = {}; // (HP, MP, attack, defence, speed, luck)
        std::vector<std::array<int, 3>> learns; // (gene_command_skill: level, type, id - in order)
    };
    const GeneInfo* geneInfo(int id) const;
    // MbGeneInfo::GetHp / GetMp, CrxGene::GetAttack ..: its numbers at its level (base + (level - 1)
    // x growth / 100), what items added to the four after (Cr3Gene::adds) - HP, MP, attack,
    // defence, speed, luck
    std::array<int, 6> geneStats(const Cr3Gene& g) const;
    int geneExp(int id, int level) const;   // (MbGeneInfo::GetExp: the EXP a level needs; the top one's past it)
    int geneLevel(const Cr3Gene& g) const;  // (MbGeneInfo::GetLevel: the highest its EXP reaches; 1 at least)
    int geneRarity(int id) const;           // (CrxGameTable::GetGeneRarity: its place in its collection line, 0 ..)
    // CrxGameData::InitGene: a gene with no slots yet learns its own (its level's, by Initialize);
    // what it had in slots 6 and 7 (a merge's: 1 and 2) kept as inherited - the Vita's InheritSkill:
    // into 6, else 7, where it has it not already
    void initGene(Cr3Gene& g, bool merged = false) const;
    // CrxGene::AddTemporaryExp: n EXP more, no further than its top level's; the commands and
    // skills of the levels gained learnt (into a free own slot, else over 0 .. 5 in turn)
    void addGeneExp(Cr3Gene& g, int n) const;
    void addGeneLevels(Cr3Gene& g, int levels) const; // (AddTemporaryExpByLevelUp)

    // DBEmu: the genes. PreviewMergeGene(gene): what merging it with each other gene held makes
    // (the pair in mergeGene2, else one series' lower grade, else the kind table's kind of the
    // grade below both, the rank nearest above half theirs; the next in its line with both at
    // their top level; a quarter of both's worth in EXP; two of their commands and skills it has
    // not of its own) and costs (100 gold a level of the two); MergeGene(k, coins): the k-th
    // preview made, the two gone
    struct MergePreview {
        int partner = -1; // (the other gene: its index in cr3Genes)
        Cr3Gene result;
        int gold = 0, coins = 0;
    };
    int previewMergeGene(GameState& s, int gene);
    const std::vector<MergePreview>& mergePreviews() const { return previews_; }
    int mergeGene(GameState& s, int preview, bool coins);
    // EnhanceGene (InterfaceMain::StartIntensificationSynthesis): the genes fed give their worth at
    // their level, 1% more for each of the target's kind; 100 gold a fed gene x the target's level
    int enhanceGene(GameState& s, int gene, const std::vector<int>& fed);
    // what EnhanceGene would give and take (InterfaceMain::GetIntensificationData): the EXP, the gold
    struct Enhance {
        int exp = 0, gold = 0;
    };
    Enhance enhanceGain(const GameState& s, int gene, const std::vector<int>& fed) const;
    int genePrice(const Cr3Gene& g) const; // (SellGenes: its price at its level)
    // EnforceGene(gene, mode, item, n): mode 1 a seed's amounts x n added (*_SEED_MASTER_ID /
    // _AMOUNTS; GENE_ADD_MAX in all), 2 its EXP set (exp), 4 its slots (slots); 2 / 4 / 8 use n of
    // the item
    int enforceGene(GameState& s, int gene, int mode, int item, int n, int exp = 0, const int (*slots)[2] = nullptr);
    int moveGene(GameState& s, int gene, bool toStock);  // (the 50 <-> the stock)
    int removeGene(GameState& s, int gene);
    int sellGenes(GameState& s, std::vector<int> genes); // (their price at their level in gold)

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

    // DBEmu: EnforceItem(item, material, level, gold) - the material attached at that level, used
    // up, the gold paid; BuyMP (VS_MP_PRICE coins); BuyVIP (VIP_PRICE coins: VIP_DAYS more, to
    // VIP_DAYS_MAX), BuyVIPTrial (VIP_TRIAL_DAYS, once); PollMessage(news, emergency): the news
    // whose dates hold today (their text ids - messages())
    int enforceItem(GameState& s, int item, int material, int level, int gold);
    int buyMP(GameState& s);
    int buyVIP(GameState& s);
    int buyVIPTrial(GameState& s);
    int pollMessage(GameState& s, std::time_t now, bool news, bool emergency);
    const std::vector<int>& messages() const { return messages_; }

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

    // DBEmu: the VS mode. GetVSTournaments: vstournaments_cli's rows whose dates (sdate / edate:
    // five MMDD windows; none - always) hold and whose flags are all set, each with its rule's
    // wildcards (vsrules), whether it is cleared and when its window ends
    struct VsTournament {
        int id = 0, mode = 0, ghost = 0, icon = 0, level = 1, bgm = 0, clearFlag = 0, exp = 0, mapId = 0, count = 0, rule = 0;
        int flags[3] = {}, coupling[3] = {-1, -1, -1};
        int prizeGold = 0, prizeIcon = 0, prizeCategory = 0, prizeId = 0, prizeCount = 0, seasonItem = -1, seasonProb = 0;
        bool perfect = false, cleared = false, seasonDrop = false;
        std::string banner;
        std::vector<int> wildcards; // (vsrules: WILDCARD_NUM flags, the wildcards it may draw)
        std::time_t ends = 0;       // (its window's end; 0 none)
    };
    int getVSTournaments(const GameState& s, std::time_t now);
    const std::vector<VsTournament>& vsTournaments() const { return vsTournaments_; }
    // BeginVSTournament(k, boosts): the k-th listed, if its window holds - the boost items chosen
    // used (one each, or bought with Oz coins at their price where none is held: 0x3f2 short), the
    // score and battles cleared, the week checked, the season's drop drawn (season_prob %, or
    // certain with VS_BOOST_DROP_RATE_BONUS_MASTER_ID's boost)
    int beginVSTournament(GameState& s, int k, const std::vector<int>& boosts, std::time_t now);
    // BeginVSBattle; EndVSBattle(score, mission, lost): a won battle's score and mission tally
    int beginVSBattle() { return finish(kDbOk); }
    int endVSBattle(GameState& s, int score, int mission, bool lost);
    // EndVSTournament: the score to the week's, the tally to the mission's - its goal reached: a
    // boost item (its own), the rank up; the week's score rewards (vsscorerewards' item sheet in its
    // dates, a draw for each of vsscore's totals passed); then EarnVSTournamentReward - the
    // tournament won (its count of battles): OBTAIN_GP_VS_END_TOURNAMENT GP, its prize (category 6:
    // one of vsprizes' items; 3: its gene box), the season's points, cleared, its clear flag
    struct VsRewards {
        int gold = 0, gp = 0, item = 0, scoreSheetDraws = 0;
        std::vector<Cr3Gene> genes;
        bool won = false, mission = false;
    };
    int endVSTournament(GameState& s, std::time_t now);
    const VsRewards& vsRewards() const { return vsRewards_; }
    // GetVSMission: the day's mission (once a day - the daily mark 8): the last one not done lowers
    // its rank (to the best - 5, 1 at least); one drawn among those whose count of missions done is
    // reached (not the last), its goal drawn from its scores at the rank, a boost item 1 .. 8
    int getVSMission(GameState& s);
    // PickWildcard: the tournament chosen's wildcards, shuffled, PICK_WILDCARD_NUM of them
    int pickWildcard();
    const std::vector<int>& wildcards() const { return wildcards_; }
    int vsChosen() const { return vsChosen_; }
    // CheckVSScoreReset: the week's score cleared on the first Monday since it began
    void checkVSScoreReset(GameState& s, std::time_t now);

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
    std::map<int, GeneInfo> genes_;
    std::map<int, std::vector<int>> expTable_; // (level: the EXP of each curve)
    std::vector<std::array<int, 3>> mergePairs_; // (a, b, result)
    std::map<int, int> rarity_;
    std::vector<MergePreview> previews_;
    std::vector<VsTournament> vsTournaments_;
    int vsChosen_ = -1;
    VsRewards vsRewards_;
    std::vector<int> wildcards_;
    std::vector<int> messages_;
    int previewGene_ = -1;
    Month month_;
    PrizeBox prizeBox_;
    bool ticket_ = false;
    int result_ = kDbOk;
    std::mt19937 rng_{0x43523355u};
};

} // namespace cr3
