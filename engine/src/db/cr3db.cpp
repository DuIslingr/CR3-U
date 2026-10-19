#include "db/cr3db.hpp"

#include <algorithm>
#include <cstring>

#include "script/gamestate.hpp"

namespace cr3 {

namespace {

// DBSystem's request names, by id (the Vita eboot's table: 0x815e8a80 + 165 x 8 + 19 x 4), and
// whether CheckEmulation keeps it
const Cr3Request kRequests[kCr3RequestCount] = {
    {"Unknown", false}, {"Auth", true}, {"UserUpdate", false}, {"UserPasswd", false},
    {"UserChange", false}, {"SavePlayer", true}, {"LoadPlayer", true}, {"GetProfile", false},
    {"PutProfile", false}, {"SetTeamName", false}, {"SetBirthday", false}, {"AddItem", true},
    {"RemoveItem", false}, {"UpdateItem", false}, {"EnforceItem", true},
    {"SetEnforceRemain", false}, {"GetItems", false}, {"AddGene", true}, {"RemoveGene", true},
    {"MoveGene", true}, {"GetGenes", false}, {"EnforceGene", true}, {"PickTreasureBox", true},
    {"OpenTreasureBox", true}, {"OpenGBox", true}, {"OpenItemSheet", true}, {"OpenPrizeBox", true},
    {"VerifyCode", false}, {"SellGene", true}, {"MergeGene", true}, {"EnhanceGene", true},
    {"PreviewMergeGene", true}, {"GetFriends", false}, {"GetFriendRequests", false},
    {"FindFriendByTeamLevel", false}, {"FindFriendByTeamName", false},
    {"FindFriendByMainChar", false}, {"RequestFriend", false}, {"RequestFriendAll", false},
    {"AcceptFriend", false}, {"RejectFriend", false}, {"VisitFriend", false}, {"GetVisits", false},
    {"UnFriend", false}, {"DispatchMessage", false}, {"GetMessage", false},
    {"GetFriendGenes", false}, {"AcceptInvitation", false}, {"GetFriendAppliesCount", false},
    {"SendPresent", false}, {"ReceivePresent", true}, {"ReceivePresents", true},
    {"CheckPresent", true}, {"RejectPresent", true}, {"DropPresent", true}, {"EarnPresent", true},
    {"SellBazaar", false}, {"CancelBazaar", false}, {"GetBazaar", false}, {"FindBazaar", false},
    {"BuyBazaar", false}, {"SubmitEntry", false}, {"FindOpponent", false},
    {"FindColosseums", false}, {"BeginBattle", false}, {"EndBattle", false}, {"OnBattle", false},
    {"PickWildCard", true}, {"SyncBattleHistory", false}, {"FindQuests", true},
    {"GotTicket", true}, {"ReceiveTicket", true}, {"MergeGift", true}, {"ItemizeGift", true},
    {"EnterDungeon", false}, {"GetGem", false}, {"ExchangeGem", true}, {"GetResearch", false},
    {"GetResearchResults", false}, {"ReportResearch", false}, {"GetAdvertisement", false},
    {"GetRanking", false}, {"EarnGPPrize", false}, {"EarnRatingPrize", false},
    {"BeginQuest", true}, {"EndQuest", true}, {"EarnQuestRewards", true}, {"UseContinue", true},
    {"UseItem", true}, {"GetDailyItem", true}, {"BuyGoods", true}, {"SellItem", true},
    {"BuyContinue", true}, {"BuyGeneStock", true}, {"BuyVip", true}, {"BuyVipTrial", true},
    {"BoughtVipTrial", true}, {"OpenGPBox", true}, {"OpenLuckyBox", true}, {"OpenGeneBox", true},
    {"PaymentRequest", false}, {"GetThisMonthMaster", true}, {"VSSync", true},
    {"GetVSTournaments", true}, {"GetVSMission", true}, {"BeginVSTournament", true},
    {"EndVSTournament", true}, {"BeginVSBattle", true}, {"EndVSBattle", true}, {"BuyMP", true},
    {"BuyBoostItem", false}, {"SubmitVSEntry", false}, {"FindVSOpponent", false},
    {"OnVSBattle", false}, {"StartBattleSession", false}, {"EndBattleSession", false},
    {"UseBattleItems", false}, {"SetBattleCoupling", false}, {"ResetPendingCharge", false},
    {"GetPendingCharge", false}, {"SetSyncTime", false}, {"SetGPRank", false},
    {"SetRatingRank", false}, {"SetQuest", true}, {"GetRecentSyncFriends", false},
    {"ShowResult", true}, {"ShowGeneResult", true}, {"ShowError", true}, {"Sync", true},
    {"SyncField", true}, {"SyncWait", true}, {"SyncData", false}, {"SyncProperty", true},
    {"SyncOnLogin", false}, {"SyncCoins", false}, {"Sync2", true}, {"SyncMP", false},
    {"Load", false}, {"ChangeDevice", false}, {"Format", false}, {"Clear", false},
    {"ResetVip", false}, {"ResetItemSheet", true}, {"ReachItemSheet", true},
    {"ResetAssistPoint", false}, {"Direct", false}, {"SaveDB", true}, {"LoadDB", true},
    {"Batch", false}, {"SaveSnapDB", true}, {"LoadSnapDB", true}, {"SkipVSTuto", false},
    {"ClearVisits", false}, {"ClearPresents", true}, {"ClearPresent", true},
    {"ClearFriend", false}, {"ClearVS", false}, {"ClearVSScore", true},
    {"ClearVSSeasonScore", true}, {"ClearVSMission", true}, {"ResetVSMission", true},
    {"SetVSMissionLevel", false}, {"OK", false}, {"WarningPartialContent", false},
};

// CrxGameTable::Load's tables (BsonMarshaller::Load(DATABASE, "data/<name>"))
const char* const kTables[] = {"bingos", "checkexps", "checkgolds", "checkitems", "collection_book", "colosseums",
                               "constant", "dailyitems", "emergencies", "flags", "geneboxes", "geneboxschedules",
                               "itemizegifts", "mergegene2", "mergegifts", "news", "questprizes", "quests",
                               "shops", "vipbonus", "vsmissions_cli", "vsprizes", "vsrules", "vsscorerewards",
                               "vsseasons", "vsseasonscorerewards", "vstournaments_cli", "items", "genes",
                               "genelevels", "loginbonus", "vip_login_bonus", "passcodeprizes", "receivetickets",
                               "invitationprizes", "questflag", "vsscore", "vsboostitems"};

} // namespace

const Cr3Request& cr3Request(int id) {
    static const Cr3Request none{"Unknown", false};
    return id >= 0 && id < kCr3RequestCount ? kRequests[id] : none;
}

int cr3RequestId(const char* name) {
    for (int k = 0; k < kCr3RequestCount; ++k)
        if (std::strcmp(kRequests[k].name, name) == 0) return k;
    return -1;
}

bool Cr3OfflineDb::load(const Database& release, std::string* error) {
    tables_.clear(), sheets_.clear();
    for (const char* name : kTables) {
        std::vector<uint8_t> d;
        if (!release.read(std::string("data\\") + name + ".bson", d)) continue; // (questflag, vsscore: .json too)
        BsonValue v;
        std::string err;
        if (!parseBson(d, v, &err)) {
            if (error) *error = std::string(name) + ".bson: " + err;
            return false;
        }
        tables_.emplace(name, std::move(v));
    }
    if (!table("constant") || !table("bingos")) {
        if (error) *error = "the server's tables (data\\constant.bson, bingos.bson) are not in this release";
        return false;
    }
    // CrxGameTable::LoadItemSheetTable: each row's _id, items (category "gene" 1, else 0;
    // item_id), result
    for (const BsonValue& row : table("bingos")->items) {
        Cr3ItemSheet s;
        s.id = row.string("_id");
        if (const BsonValue* items = row.get("items"); items && items->isArray())
            for (const BsonValue& e : items->items) s.entries.push_back({e.integer("item_id"), e.string("category") == "gene"});
        if (const BsonValue* r = row.get("result"); r && r->isObject()) s.result = {r->integer("item_id"), r->string("category") == "gene"};
        sheets_.push_back(std::move(s));
    }
    return true;
}

const BsonValue* Cr3OfflineDb::table(const std::string& name) const {
    const auto it = tables_.find(name);
    return it != tables_.end() ? &it->second : nullptr;
}

const BsonValue* Cr3OfflineDb::constants() const {
    const BsonValue* t = table("constant");
    return t && t->isArray() && !t->items.empty() ? &t->items[0] : nullptr;
}

int Cr3OfflineDb::constant(const std::string& name, int fallback) const {
    const BsonValue* c = constants();
    return c ? c->integer(name, fallback) : fallback;
}

int Cr3OfflineDb::findItemSheet(const std::string& id) const {
    for (size_t k = 0; k < sheets_.size(); ++k)
        if (sheets_[k].id == id) return int(k);
    return -1;
}

const Cr3ItemSheet::Prize* Cr3OfflineDb::openedPrize(size_t k) const {
    const int s = findItemSheet(openedSheet_);
    if (s < 0 || k >= opened_.size()) return nullptr;
    const auto& e = sheets_[size_t(s)].entries;
    return opened_[k] >= 0 && size_t(opened_[k]) < e.size() ? &e[size_t(opened_[k])] : nullptr;
}

Cr3SheetProgress& Cr3OfflineDb::progress(GameState& s, int sheet) const {
    Cr3SheetProgress& p = s.cr3Sheets[sheets_[size_t(sheet)].id];
    const size_t n = sheets_[size_t(sheet)].entries.size();
    if (p.opened.size() != n) p.opened.resize(n), p.mark.resize(n);
    return p;
}

void Cr3OfflineDb::markItemSheets(GameState& s) const {
    for (size_t k = 0; k < sheets_.size(); ++k) {
        Cr3SheetProgress& p = progress(s, int(k));
        for (size_t e = 0; e < p.opened.size(); ++e) p.mark[e] = p.opened[e] > 0;
        p.completedSeen = p.completed > 0;
    }
}

void Cr3OfflineDb::updateItemSheets(GameState& s) const {
    for (size_t k = 0; k < sheets_.size(); ++k) {
        Cr3SheetProgress& p = progress(s, int(k));
        for (size_t e = 0; e < p.opened.size(); ++e) p.mark[e] = !p.mark[e] && p.opened[e] > 0;
        if (p.completed > 0 && !p.completedSeen) p.completedNew = p.completedSeen = true;
    }
}

int Cr3OfflineDb::earnItemPrize(GameState& s, int item, int count, bool toPresents) {
    if (item <= 0 || count <= 0) return kDbBad;
    // (CrxGameData::AddItem: 99 of one at most; the rest - SendPresent - to the presents box)
    int& held = s.cr3Items[item];
    const int room = std::max(99 - held, 0), added = std::min(room, count);
    held += added;
    if (added == count) return kDbOk;
    if (!toPresents) return kDbFull;
    s.cr3Presents.push_back({item, count - added, constant("PRESENT_FROM_SERVICE")});
    return kDbOk;
}

int Cr3OfflineDb::earnGenePrize(GameState& s, int gene) {
    if (gene <= 0) return kDbBad;
    ++s.cr3Genes[gene]; // (dbutils::EarnGenePrize: a new gene of it - CrxGeneData at its first level)
    return kDbOk;
}

// DBEmu::DoOpenItemSheet: the candidates - the one asked for, else (the sheet done once already,
// or all) every entry, else the ones not won yet - shuffled, the first won (an item or a gene),
// counted; with a key one gold key used, with a flag that flag set; and the sheet's own prize the
// first time every entry has been won
void Cr3OfflineDb::doOpenItemSheet(GameState& s, int sheet, int entry, bool all, bool key, int flag) {
    const Cr3ItemSheet& t = sheets_[size_t(sheet)];
    Cr3SheetProgress& p = progress(s, sheet);
    openedSheet_ = t.id;
    std::vector<int> pool;
    if (entry >= 0) pool.push_back(entry);
    else
        for (size_t e = 0; e < t.entries.size(); ++e)
            if (p.completed > 0 || all || p.opened[e] == 0) pool.push_back(int(e));
    if (pool.empty()) return;
    std::shuffle(pool.begin(), pool.end(), rng_);
    const int won = pool.front();
    if (won < 0 || size_t(won) >= t.entries.size()) return;
    const Cr3ItemSheet::Prize& prize = t.entries[size_t(won)];
    if (prize.gene) earnGenePrize(s, prize.item);
    else earnItemPrize(s, prize.item, 1);
    opened_.push_back(won);
    ++p.opened[size_t(won)];
    p.changed = true;
    if (key) { // (CrxGameData::AddItem(GOLDKEY_MASTER_ID, -1))
        int& keys = s.cr3Items[constant("GOLDKEY_MASTER_ID")];
        keys = std::max(keys - 1, 0);
    }
    if (flag > 0) s.setFlag(flag, true);
    if (p.completed == 0 && std::all_of(p.opened.begin(), p.opened.end(), [](int n) { return n > 0; })) {
        if (t.result.gene) earnGenePrize(s, t.result.item);
        else earnItemPrize(s, t.result.item, 1);
        ++p.completed;
    }
}

int Cr3OfflineDb::openItemSheet(GameState& s, const std::string& sheet, bool key, int flag) {
    const int k = findItemSheet(sheet);
    if (k < 0) return finish(kDbBad), kDbBad;
    opened_.clear();
    markItemSheets(s);
    doOpenItemSheet(s, k, -1, false, key, flag);
    updateItemSheets(s);
    return finish(kDbOk), kDbOk;
}

int Cr3OfflineDb::openTreasureBox(GameState& s, const std::string& sheet, int entry, bool key, int flag) {
    const int k = findItemSheet(sheet);
    if (k < 0 || entry < 0 || size_t(entry) >= sheets_[size_t(k)].entries.size()) return finish(kDbBad), kDbBad;
    opened_.clear();
    markItemSheets(s);
    doOpenItemSheet(s, k, entry, false, key, flag);
    updateItemSheets(s);
    return finish(kDbOk), kDbOk;
}

// _PickTreasureBox: the entries not marked (any, with all or the sheet newly done) shuffled, n
// of them taken from the end - once those run out, any entry at random
int Cr3OfflineDb::pickTreasureBox(GameState& s, const std::string& sheet, int n, bool all) {
    const int k = findItemSheet(sheet);
    if (k < 0) return finish(kDbNone), kDbNone;
    const Cr3ItemSheet& t = sheets_[size_t(k)];
    Cr3SheetProgress& p = progress(s, k);
    openedSheet_ = t.id, opened_.clear();
    std::vector<int> pool;
    for (size_t e = 0; e < t.entries.size(); ++e)
        if (p.completedNew || all || !p.mark[e]) pool.push_back(int(e));
    if (pool.empty()) return finish(kDbNone), kDbNone;
    std::shuffle(pool.begin(), pool.end(), rng_);
    for (int i = 0; i < n; ++i) {
        if (pool.empty()) opened_.push_back(int(rng_() % uint32_t(std::max<size_t>(t.entries.size(), 1))));
        else opened_.push_back(pool.back()), pool.pop_back();
    }
    return finish(kDbOk), kDbOk;
}

} // namespace cr3
