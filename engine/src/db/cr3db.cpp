#include "db/cr3db.hpp"

#include <algorithm>
#include <cstring>

#include "assets/mbe.hpp"

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


// DBEmu::OpenPrizeBox("ocean")'s prizes (libcrx 0xf30fe0: 16 of item, count; the draw's low 4 bits)
const int kOceanPrizes[16][2] = {{18, 1}, {19, 1}, {19, 2}, {71, 1}, {72, 1}, {73, 1}, {74, 1}, {75, 1},
                                 {76, 1}, {77, 1}, {78, 1}, {79, 1}, {80, 1}, {81, 1}, {82, 1}, {91, 1}};

// DBEmu::Sync2: an event's gifts by its name - genes (> 0: a gene_para id, the bool its message)
// and items (full ids), as the Vita's (its strcmp chain: 0x8107a1b0)
struct Sync2Gift {
    const char* name;
    int gene[2];
    bool geneMessage;
    int item[2], count[2];
};
const Sync2Gift kSync2[] = {
    {"tuto_get_gene1", {1, 0}, true, {0, 0}, {0, 0}},
    {"tuto_get_gene2", {4, 0}, true, {0, 0}, {0, 0}},
    {"tuto_get_gene3", {7, 0}, true, {0, 0}, {0, 0}},
    {"tuto_merge_gene1", {4, 7}, false, {0, 0}, {0, 0}},
    {"tuto_merge_gene2", {1, 7}, false, {0, 0}, {0, 0}},
    {"tuto_merge_gene3", {1, 4}, false, {0, 0}, {0, 0}},
    {"tuto_join", {70, 121}, false, {0, 0}, {0, 0}},
    {"quest56_gift", {0, 0}, false, {600022, 0}, {1, 0}},
    {"quest56_gene", {19, 0}, true, {0, 0}, {0, 0}},
    {"s100_t01_00702", {0, 0}, false, {400005, 0}, {2, 0}},
    {"s272_t01_00401_gift_32_50", {0, 0}, false, {600032, 600050}, {1, 1}},
    {"s272_t01_00401_gift_32_57", {0, 0}, false, {600032, 600057}, {1, 1}},
    {"s272_t01_00401_gift_32_08", {0, 0}, false, {600032, 600008}, {1, 1}},
    {"s272_t01_00401_gift_32_23", {0, 0}, false, {600032, 600023}, {1, 1}},
    {"s272_t01_00401_gift_50_57", {0, 0}, false, {600050, 600057}, {1, 1}},
    {"s272_t01_00401_gift_50_08", {0, 0}, false, {600050, 600008}, {1, 1}},
    {"s272_t01_00401_gift_50_23", {0, 0}, false, {600050, 600023}, {1, 1}},
    {"s272_t01_00401_gift_57_08", {0, 0}, false, {600057, 600008}, {1, 1}},
    {"s272_t01_00401_gift_57_23", {0, 0}, false, {600057, 600023}, {1, 1}},
    {"s272_t01_00401_gift_08_23", {0, 0}, false, {600008, 600023}, {1, 1}},
    {"c140_s222_t01_00101", {0, 0}, false, {600011, 0}, {1, 0}},
    {"t02_kbox_03", {0, 0}, false, {59, 0}, {1, 0}},
};

std::tm localTime(std::time_t t) {
    std::tm r{};
#ifdef _WIN32
    localtime_s(&r, &t);
#else
    localtime_r(&t, &r);
#endif
    return r;
}

int monthDays(int year, int month) { // (Date::max_day; month 0 .. 11, year from 1900)
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int y = year + 1900;
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return month == 1 && leap ? 29 : days[std::clamp(month, 0, 11)];
}

// CheckDate(MMDD, MMDD, now): from the first day's start to the second's end, this year (each day
// no later than its month's last; no wrap past the year's end)
bool checkDate(int from, int to, std::time_t now) {
    const std::tm n = localTime(now);
    auto at = [&](int mmdd, bool after) {
        std::tm t{};
        t.tm_year = n.tm_year, t.tm_mon = std::clamp(mmdd / 100 - 1, 0, 11);
        t.tm_mday = std::min(mmdd % 100, monthDays(t.tm_year, t.tm_mon)) + (after ? 1 : 0);
        t.tm_isdst = -1;
        return std::mktime(&t);
    };
    return at(from, false) <= now && now < at(to, true);
}

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
    tables_.clear(), sheets_.clear(), quests_.clear(), prices_.clear();
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
        tables_.clear();
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
    // LoadQuestTable (its Declaration: _id, _no - not in the rows, so the text's first name, "no"
    // -, stime, etime, flags x 6, lv, rank, item_id, item_count)
    if (const BsonValue* q = table("quests"))
        for (const BsonValue& row : q->items) {
            Quest t;
            t.id = row.integer("_id"), t.no = row.integer("no"), t.stime = row.integer("stime"), t.etime = row.integer("etime");
            if (const BsonValue* f = row.get("flags"); f && f->isArray())
                for (size_t k = 0; k < f->items.size() && k < 6; ++k) t.flags[k] = f->items[k].asInt();
            t.level = row.integer("lv"), t.rank = row.integer("rank"), t.item = row.integer("item_id"), t.itemCount = row.integer("item_count");
            quests_.push_back(t);
        }
    // the genes: gene_para's Gene rows and ExpTable (a level, then each curve's EXP; -1 past the
    // curve's end), gene_command_skill's (gene, level, type, id), mergeGene2's ids (a, b, result),
    // the collection book's lines (ids: their place the rarity)
    genes_.clear(), expTable_.clear(), mergePairs_.clear(), rarity_.clear();
    {
        std::vector<uint8_t> g;
        MbeFile gp, cs;
        if (release.read("data\\gene_para.mbe", g) && gp.parse(g)) {
            if (const MbeFile::Table* t = gp.table("Gene"))
                for (size_t k = 0; k < t->rows.size(); ++k) {
                    MbeFile::Row r;
                    if (!gp.row(*t, k, "iiiiiiiiiiiii", r)) continue;
                    GeneInfo i;
                    i.id = r.i(0), i.series = r.i(1), i.grade = r.i(2), i.maxLevel = r.i(3), i.curve = r.i(4), i.rank = r.i(5);
                    i.type = r.i(6), i.worth = r.i(8), i.worthPerLevel = r.i(9), i.price = r.i(10), i.pricePerLevel = r.i(11), i.kind = r.i(12);
                    genes_[i.id] = i;
                }
            if (const MbeFile::Table* t = gp.table("ExpTable"))
                for (size_t k = 0; k < t->rows.size(); ++k) {
                    MbeFile::Row r;
                    if (gp.row(*t, k, "iiiiii", r)) expTable_[r.i(0)] = {r.i(1), r.i(2), r.i(3), r.i(4), r.i(5)};
                }
        }
        if (release.read("data\\gene_command_skill.mbe", g) && cs.parse(g))
            if (const MbeFile::Table* t = cs.table("GeneCommandSkill"))
                for (size_t k = 0; k < t->rows.size(); ++k) {
                    MbeFile::Row r;
                    if (!cs.row(*t, k, "iiii", r)) continue;
                    if (auto it = genes_.find(r.i(0)); it != genes_.end()) it->second.learns.push_back({r.i(1), r.i(2), r.i(3)});
                }
    }
    if (const BsonValue* t = table("mergegene2"))
        for (const BsonValue& r : t->items)
            if (const BsonValue* ids = r.get("ids"); ids && ids->isArray() && ids->items.size() >= 3)
                mergePairs_.push_back({ids->items[0].asInt(), ids->items[1].asInt(), ids->items[2].asInt()});
    if (const BsonValue* t = table("collection_book"))
        for (const BsonValue& r : t->items)
            if (const BsonValue* ids = r.get("ids"); ids && ids->isArray())
                for (size_t k = 0; k < ids->items.size(); ++k)
                    if (ids->items[k].asInt() > 0) rarity_.emplace(ids->items[k].asInt(), int(k));
    // GetItemPrice: item_para's tables (MbItemInfo: +4 the full id, +0x18 the price a shop pays)
    std::vector<uint8_t> d;
    MbeFile items;
    if (release.read("data\\item_para.mbe", d) && items.parse(d))
        for (const MbeFile::Table& t : items.tables())
            for (size_t k = 0; k < t.rows.size(); ++k) {
                MbeFile::Row r;
                if (items.row(t, k, "iiiiiii", r) && r.i(1) > 0) prices_.emplace(r.i(1), r.i(6));
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

int Cr3OfflineDb::itemPrice(int item) const {
    const auto it = prices_.find(item);
    return it != prices_.end() ? it->second : 0;
}

// --- the login and the month ---

int Cr3OfflineDb::login(GameState& s, std::time_t now) {
    dateChanged(s, now);
    return finish(getThisMonthMaster(s, now));
}

int Cr3OfflineDb::dateChanged(GameState& s, std::time_t now) {
    // (CrxGameData::CheckDateChanged: the day turns at DATE_LINE_HOUR)
    const std::time_t shift = std::time_t(constant("DATE_LINE_HOUR")) * 3600;
    const std::tm a = localTime(std::time_t(s.cr3LastLogin) - shift), b = localTime(now - shift);
    if (s.cr3LastLogin != 0 && a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return kDbOk;
    s.cr3Daily = 0;
    s.cr3LastLogin = int64_t(now);
    Cr3Present coin;
    coin.item = constant("LOGIN_BONUS_COIN_MASTER_ID"), coin.count = 1, coin.from = constant("PRESENT_FROM_LOGINBONUS");
    if (coin.item > 0) sendPresent(s, coin);
    s.cr3GP += constant("DATE_CHANGED_GP_BONUS");
    s.cr3Cheer += constant("DATE_CHANGED_CHEERPOINT_BONUS");
    if (const BsonValue* c = constants(); c)
        if (const BsonValue* f = c->get("DATE_CHANGED_CLEAR_FLAGS"); f && f->isArray())
            for (const BsonValue& n : f->items) s.setFlag(n.asInt(), false);
    return getThisMonthMaster(s, now);
}

int Cr3OfflineDb::getThisMonthMaster(GameState& s, std::time_t now) {
    month_ = Month();
    if (const BsonValue* t = table("vipbonus"))
        for (const BsonValue& r : t->items)
            if (checkDate(r.integer("sdate"), r.integer("edate"), now)) {
                month_.expBonus = r.number("exp_bonus", 1), month_.dropBonus = r.number("drop_bonus", 1), month_.goldBonus = r.number("gold_bonus", 1);
                break;
            }
    if (const BsonValue* t = table("geneboxschedules"))
        for (const BsonValue& r : t->items) {
            if (month_.geneBoxes.size() >= 5) break;
            if (checkDate(r.integer("sdate"), r.integer("edate"), now)) month_.geneBoxes.push_back(r.integer("_id"));
        }
    if (const BsonValue* t = table("flags"))
        for (const BsonValue& r : t->items)
            if (checkDate(r.integer("sdate"), r.integer("edate"), now)) s.setFlag(r.integer("flag"), true);
    for (const auto& [name, out] : {std::pair<const char*, std::string*>{"vsscorerewards", &month_.vsScoreSheet},
                                    std::pair<const char*, std::string*>{"vsseasonscorerewards", &month_.vsSeasonSheet}})
        if (const BsonValue* t = table(name))
            for (const BsonValue& r : t->items)
                if (checkDate(r.integer("sdate"), r.integer("edate"), now)) {
                    *out = r.string("bingo");
                    break;
                }
    return kDbOk;
}

// --- prizes and presents ---

int Cr3OfflineDb::earnItemPrize(GameState& s, int item, int count, bool toPresents) {
    if (item <= 0 || count <= 0) return kDbBad;
    // (CrxGameData::AddItem: 99 of one at most - none added past that; SendPresent the lot)
    int& held = s.cr3Items[item];
    if (held + count <= 99) {
        held += count;
        return kDbOk;
    }
    if (!toPresents) return kDbFull;
    Cr3Present p;
    p.item = item, p.count = count, p.from = constant("PRESENT_FROM_SERVICE");
    sendPresent(s, p);
    return kDbOk;
}

int Cr3OfflineDb::earnGenePrize(GameState& s, const Cr3Gene& gene) {
    if (gene.master <= 0) return kDbBad;
    // (CrxGameData::AddGene: GENE_CAPACITY held, then the gene stock's room; else the presents)
    if (int(s.cr3Genes.size()) < constant("GENE_CAPACITY", 50) + s.cr3GeneStock) {
        s.cr3Genes.push_back(gene);
        s.cr3Genes.back().fresh = true;
        initGene(s.cr3Genes.back()); // (CrxGameData::AddGene: its slots, where it has none)
        s.cr3GeneBook.insert(gene.master);
        return kDbOk;
    }
    Cr3Present p;
    p.gene = true, p.geneData = gene, p.from = constant("PRESENT_FROM_SERVICE");
    sendPresent(s, p);
    return kDbOk;
}

void Cr3OfflineDb::sendPresent(GameState& s, const Cr3Present& p) {
    if (s.cr3Presents.size() >= 100) s.cr3Presents.erase(s.cr3Presents.begin()); // (the oldest)
    s.cr3Presents.push_back(p);
}

bool Cr3OfflineDb::removeItem(GameState& s, int item, int n) {
    auto it = s.cr3Items.find(item);
    if (it == s.cr3Items.end() || it->second < n || n <= 0) return false;
    it->second -= n;
    return true;
}

int Cr3OfflineDb::receivePresent(GameState& s, int k) {
    if (k < 0 || size_t(k) >= s.cr3Presents.size()) return finish(kDbBad);
    const Cr3Present p = s.cr3Presents[size_t(k)];
    int r;
    if (p.gene) r = earnGenePrize(s, p.geneData);
    else {
        if (p.item == constant("LOGIN_BONUS_COIN_MASTER_ID")) s.cr3Coins += constant("LOGIN_BONUS_COIN");
        r = earnItemPrize(s, p.item, p.count, false);
    }
    if (r != kDbOk) return finish(r);
    s.cr3Presents.erase(s.cr3Presents.begin() + k);
    return finish(kDbOk);
}

int Cr3OfflineDb::receivePresents(GameState& s) {
    bool all = true;
    for (size_t k = 0; k < s.cr3Presents.size();) {
        const Cr3Present& p = s.cr3Presents[k];
        bool taken = true;
        if (p.gene) taken = earnGenePrize(s, p.geneData) == kDbOk;
        else if (p.item == constant("LOGIN_BONUS_COIN_MASTER_ID")) s.cr3Coins += constant("LOGIN_BONUS_COIN");
        else taken = earnItemPrize(s, p.item, p.count, false) == kDbOk;
        if (taken) s.cr3Presents.erase(s.cr3Presents.begin() + long(k));
        else all = false, ++k;
    }
    return finish(all ? kDbOk : kDbSomeLeft);
}

int Cr3OfflineDb::dropPresent(GameState& s, int k) {
    if (k >= 0 && size_t(k) < s.cr3Presents.size()) s.cr3Presents.erase(s.cr3Presents.begin() + k);
    return finish(kDbOk);
}

int Cr3OfflineDb::earnPresent(GameState& s, int n) {
    Cr3Present p;
    p.from = constant("PRESENT_FROM_SERVICE");
    switch (n) {
    case 1: p.gene = true, p.geneData = Cr3Gene{10, 1239}; break;
    case 2: p.gene = true, p.geneData = Cr3Gene{13, 2869}; break;
    case 3: p.gene = true, p.geneData = Cr3Gene{11, 27674}; break;
    case 4: p.item = 400005, p.count = 1; break;
    default: return finish(kDbBad);
    }
    sendPresent(s, p);
    return finish(kDbOk);
}

int Cr3OfflineDb::clearPresents(GameState& s) {
    s.cr3Presents.clear();
    return finish(kDbOk);
}

// --- the item sheets ---

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
    if (k < 0) return finish(kDbBad);
    opened_.clear();
    markItemSheets(s);
    doOpenItemSheet(s, k, -1, false, key, flag);
    updateItemSheets(s);
    return finish(kDbOk);
}

int Cr3OfflineDb::openTreasureBox(GameState& s, const std::string& sheet, int entry, bool key, int flag) {
    const int k = findItemSheet(sheet);
    if (k < 0 || entry < 0 || size_t(entry) >= sheets_[size_t(k)].entries.size()) return finish(kDbBad);
    opened_.clear();
    markItemSheets(s);
    doOpenItemSheet(s, k, entry, false, key, flag);
    updateItemSheets(s);
    return finish(kDbOk);
}

// _PickTreasureBox: the entries not marked (any, with all or the sheet newly done) shuffled, n
// of them taken from the end - once those run out, any entry at random
int Cr3OfflineDb::pickTreasureBox(GameState& s, const std::string& sheet, int n, bool all) {
    const int k = findItemSheet(sheet);
    if (k < 0) return finish(kDbNone);
    const Cr3ItemSheet& t = sheets_[size_t(k)];
    Cr3SheetProgress& p = progress(s, k);
    openedSheet_ = t.id, opened_.clear();
    std::vector<int> pool;
    for (size_t e = 0; e < t.entries.size(); ++e)
        if (p.completedNew || all || !p.mark[e]) pool.push_back(int(e));
    if (pool.empty()) return finish(kDbNone);
    std::shuffle(pool.begin(), pool.end(), rng_);
    for (int i = 0; i < n; ++i) {
        if (pool.empty()) opened_.push_back(int(random() % uint32_t(std::max<size_t>(t.entries.size(), 1))));
        else opened_.push_back(pool.back()), pool.pop_back();
    }
    return finish(kDbOk);
}

// --- the boxes ---

int Cr3OfflineDb::openGPBox(GameState& s, int n, int gp) {
    if (s.cr3GP < gp) return finish(kDbNoGP);
    std::vector<int> pool; // (the sheets won from: +0x14a)
    for (size_t k = 0; k < sheets_.size(); ++k)
        if (progress(s, int(k)).changed) pool.push_back(int(k));
    if (pool.empty()) return finish(kDbBad);
    opened_.clear();
    markItemSheets(s);
    std::shuffle(pool.begin(), pool.end(), rng_);
    for (int i = 0; i < n; ++i) doOpenItemSheet(s, pool.front(), -1, true, false, 0);
    updateItemSheets(s);
    s.cr3GP -= gp, s.cr3GPSpent += gp;
    openedSheet_ = sheets_[size_t(pool.front())].id;
    return finish(kDbOk);
}

int Cr3OfflineDb::openLuckyBox(GameState& s, const std::string& sheet, int n, int coins) {
    if (s.cr3Coins < coins) return finish(kDbNoCoins);
    opened_.clear();
    markItemSheets(s);
    if (const int k = findItemSheet(sheet); k >= 0)
        for (int i = 0; i < n; ++i) doOpenItemSheet(s, k, -1, false, false, 0);
    updateItemSheets(s);
    s.cr3Coins -= coins, s.cr3CoinsSpent += coins;
    openedSheet_ = sheet;
    return finish(kDbOk);
}

// DBEmu::OpenGeneBox(box, gene): a basket by its prob (out of their sum), then one of its genes
// at random - its masterId, exp and commands
int Cr3OfflineDb::openGeneBox(GameState& s, int box, int n, int coins) {
    if (s.cr3Coins < coins) return finish(kDbNoCoins);
    const BsonValue* t = table("geneboxes");
    const BsonValue* row = nullptr;
    if (t)
        for (const BsonValue& r : t->items)
            if (r.integer("_id") == box) row = &r;
    const BsonValue* baskets = row ? row->get("baskets") : nullptr;
    if (!baskets || !baskets->isArray()) return finish(kDbBad);
    lastGenes_.clear();
    int total = 0;
    for (const BsonValue& b : baskets->items) total += b.integer("prob");
    for (int i = 0; i < n && total > 0; ++i) {
        const int roll = int(random() % uint32_t(total));
        int sum = 0;
        for (const BsonValue& b : baskets->items) {
            sum += b.integer("prob");
            if (roll >= sum) continue;
            const BsonValue* items = b.get("items");
            if (items && items->isArray() && !items->items.empty()) {
                const BsonValue& g = items->items[random() % items->items.size()];
                Cr3Gene gene{g.integer("masterId"), g.integer("exp")};
                if (const BsonValue* c = g.get("commands"); c && c->isArray()) // (its eight (type, id))
                    for (size_t k = 0; k + 1 < c->items.size() && k / 2 < 8; k += 2)
                        gene.slots[k / 2][0] = c->items[k].asInt(), gene.slots[k / 2][1] = c->items[k + 1].asInt();
                earnGenePrize(s, gene);
                lastGenes_.push_back(gene);
            }
            break;
        }
    }
    s.cr3Coins -= coins, s.cr3CoinsSpent += coins;
    return finish(kDbOk);
}

int Cr3OfflineDb::openPrizeBox(GameState& s, const std::string& name) {
    if (name != "ocean") return finish(kDbBad);
    const int* p = kOceanPrizes[random() & 0xf];
    const size_t before = s.cr3Presents.size();
    const int r = earnItemPrize(s, p[0], p[1]);
    prizeBox_ = {1, p[0], p[1], s.cr3Presents.size() > before};
    if (r == kDbOk) s.setFlag(0xfba, true); // (SM_OpenPrizeBox: the box opened)
    return finish(r);
}

// --- the shops and items ---

int Cr3OfflineDb::findShop(const std::string& id) const {
    if (const BsonValue* t = table("shops"))
        for (size_t k = 0; k < t->items.size(); ++k)
            if (t->items[k].string("_id") == id) return int(k);
    return -1;
}

int Cr3OfflineDb::buyGoods(GameState& s, int shop, int goods, int n) {
    const BsonValue* t = table("shops");
    if (!t || shop < 0 || size_t(shop) >= t->items.size()) return finish(kDbBad);
    const BsonValue* list = t->items[size_t(shop)].get("goods");
    if (!list || !list->isArray()) return finish(kDbBad);
    for (const BsonValue& g : list->items) {
        if (g.integer("_id") != goods) continue;
        const int coins = g.integer("coin") * n, gp = g.integer("GP") * n, gold = g.integer("gold") * n;
        if (s.cr3Coins < coins) return finish(kDbNoCoins);
        if (s.cr3GP < gp) return finish(kDbNoGP);
        if (s.cr3Money < gold) return finish(kDbNoGold);
        if (const BsonValue* items = g.get("items"); items && items->isArray())
            for (size_t k = 0; k < items->items.size() && k < 3; ++k) {
                const int id = items->items[k].integer("item_id"), count = items->items[k].integer("count") * n;
                for (int c = 0; id > 0 && c < count; ++c) earnItemPrize(s, id, 1);
            }
        s.cr3Coins -= coins, s.cr3CoinsSpent += coins;
        s.cr3GP -= gp, s.cr3GPSpent += gp;
        s.cr3Money -= gold;
        return finish(kDbOk);
    }
    return finish(kDbBad);
}

int Cr3OfflineDb::sellItem(GameState& s, int item, int n) {
    const int price = itemPrice(item);
    if (!removeItem(s, item, n)) return finish(kDbBad);
    s.cr3Money += price * n;
    return finish(kDbOk);
}

int Cr3OfflineDb::useItem(GameState& s, int item, int n) { return finish(removeItem(s, item, n) ? kDbOk : kDbBad); }

int Cr3OfflineDb::useContinue(GameState& s) {
    return finish(removeItem(s, constant("CONTINUE_GEM_MASTER_ID"), 1) ? kDbOk : kDbBad);
}

int Cr3OfflineDb::exchangeGem(GameState& s) {
    const int gem = constant("GEM_MASTER_ID");
    const auto it = s.cr3Items.find(gem);
    if (it == s.cr3Items.end() || it->second <= 0) return finish(kDbNone);
    if (it->second < 99) return finish(kDbBad);
    it->second = 0;
    earnItemPrize(s, constant("CONTINUE_GEM_MASTER_ID"), 1);
    return finish(kDbOk);
}

int Cr3OfflineDb::buyGeneStock(GameState& s) {
    const int most = constant("GENE_STOCK_CAPACITY_MAX"), price = constant("GENE_STOCK_PRICE");
    if (s.cr3GeneStock >= most) return finish(kDbFull);
    if (s.cr3Coins < price) return finish(kDbNoCoins);
    s.cr3GeneStock = std::min(s.cr3GeneStock + constant("GENE_STOCK_CAPACITY_ADD"), most);
    s.cr3Coins -= price, s.cr3CoinsSpent += price;
    return finish(kDbOk);
}

int Cr3OfflineDb::getDailyItem(GameState& s, int id) {
    if (const BsonValue* t = table("dailyitems"))
        for (const BsonValue& r : t->items) {
            if (r.integer("_id") != id) continue;
            const int flag = r.integer("flag");
            if (s.flag(flag)) return finish(kDbDone);
            const int res = earnItemPrize(s, r.integer("masterId"), r.integer("count"), false);
            s.setFlag(flag, true);
            return finish(res);
        }
    return finish(kDbBad);
}

// --- gifts ---

int Cr3OfflineDb::mergeGift(GameState& s, int id, const std::vector<int>& gifts) {
    if (const BsonValue* t = table("mergegifts"))
        for (const BsonValue& r : t->items) {
            if (r.integer("_id") != id) continue;
            earnGenePrize(s, r.integer("gene_id"));
            for (const int g : gifts) removeItem(s, g, 1);
            return finish(kDbOk);
        }
    return finish(kDbBad);
}

int Cr3OfflineDb::itemizeGift(GameState& s, int gift) {
    const auto held = s.cr3Items.find(gift);
    if (held == s.cr3Items.end() || held->second <= 0) return finish(kDbBad);
    if (const BsonValue* t = table("itemizegifts"))
        for (const BsonValue& r : t->items) {
            if (r.integer("_id") != gift) continue;
            removeItem(s, gift, 1);
            earnItemPrize(s, r.integer("item_id"), r.integer("count"));
            return finish(kDbOk);
        }
    return finish(kDbBad);
}

// --- genes ---

namespace {

// DBEmu's merge kind table (libcrx 0xf31060: [kind][kind] -> the result's kind)
const int kMergeKind[7][7] = {{0, 1, 2, 3, 4, 5, 6}, {1, 1, 3, 5, 2, 6, 2}, {2, 3, 2, 4, 6, 4, 3}, {3, 5, 4, 3, 1, 1, 2},
                              {4, 2, 6, 1, 4, 1, 5}, {5, 6, 4, 1, 1, 5, 0}, {6, 2, 3, 2, 5, 0, 6}};

bool hasSlot(const Cr3Gene& g, int type, int id) {
    for (const auto& sl : g.slots)
        if (sl[0] == type && sl[1] == id) return true;
    return false;
}

} // namespace

const Cr3OfflineDb::GeneInfo* Cr3OfflineDb::geneInfo(int id) const {
    const auto it = genes_.find(id);
    return it != genes_.end() ? &it->second : nullptr;
}

int Cr3OfflineDb::geneExp(int id, int level) const {
    const GeneInfo* i = geneInfo(id);
    if (!i) return 0;
    const auto row = expTable_.find(std::min(level, i->maxLevel));
    return row != expTable_.end() && i->curve >= 0 && i->curve < int(row->second.size()) ? row->second[size_t(i->curve)] : 0;
}

int Cr3OfflineDb::geneLevel(const Cr3Gene& g) const {
    const GeneInfo* i = geneInfo(g.master);
    if (!i) return 1;
    for (int l = i->maxLevel; l >= 0; --l) {
        const auto row = expTable_.find(l);
        if (row == expTable_.end() || i->curve < 0 || i->curve >= int(row->second.size())) continue;
        const int need = row->second[size_t(i->curve)];
        if (need != -1 && need <= g.exp) return l;
    }
    return 1;
}

int Cr3OfflineDb::geneRarity(int id) const {
    const auto it = rarity_.find(id);
    return it != rarity_.end() ? it->second : 0;
}

namespace {

// CrxGene's learning: the gene's (level, type, id) rows of the levels past `from` up to `to`, in
// order - each into its first free own slot, else over 0 .. 5 in turn
void learn(Cr3Gene& g, const Cr3OfflineDb::GeneInfo& i, int from, int to) {
    int turn = 0;
    for (const auto& l : i.learns) {
        if (l[0] <= from || l[0] > to) continue;
        int slot = -1;
        for (int k = 0; k < 6 && slot < 0; ++k)
            if (g.slots[k][1] == -1) slot = k;
        if (slot < 0) slot = turn++ % 6;
        g.slots[slot][0] = l[1], g.slots[slot][1] = l[2];
    }
}

// CrxGameData::InheritSkill (the Vita's): `from`'s slot k into 6, else 7, unless it has it
void inheritSkill(Cr3Gene& g, const Cr3Gene& from, int k) {
    const int type = from.slots[k][0], id = from.slots[k][1];
    if (hasSlot(g, type, id)) return;
    if (g.slots[6][0] == -1) g.slots[6][0] = type, g.slots[6][1] = id;
    else if (g.slots[7][0] == -1) g.slots[7][0] = type, g.slots[7][1] = id;
}

} // namespace

void Cr3OfflineDb::initGene(Cr3Gene& g, bool merged) const {
    const GeneInfo* i = geneInfo(g.master);
    if (!i || g.slots[0][1] != -1) return;
    const Cr3Gene old = g;
    for (auto& sl : g.slots) sl[0] = sl[1] = -1;
    learn(g, *i, 0, geneLevel(old)); // (CrxGene::Initialize(level): from nothing to its level)
    inheritSkill(g, old, merged ? 1 : 6);
    inheritSkill(g, old, merged ? 2 : 7);
}

void Cr3OfflineDb::addGeneExp(Cr3Gene& g, int n) const {
    const GeneInfo* i = geneInfo(g.master);
    if (!i) return;
    const int before = geneLevel(g);
    g.exp += n;
    int after = geneLevel(g);
    if (after >= i->maxLevel) after = i->maxLevel, g.exp = geneExp(g.master, after);
    if (after > before) learn(g, *i, before, after);
}

void Cr3OfflineDb::addGeneLevels(Cr3Gene& g, int levels) const {
    const GeneInfo* i = geneInfo(g.master);
    if (i) addGeneExp(g, geneExp(g.master, std::min(geneLevel(g) + levels, i->maxLevel)) - g.exp);
}

// _PreviewMergeGene / InheritSkill(CrxGene, CrxGene, MbGeneInfo, CrxGeneData)
int Cr3OfflineDb::previewMergeGene(GameState& s, int gene) {
    previews_.clear(), previewGene_ = gene;
    if (gene < 0 || size_t(gene) >= s.cr3Genes.size()) return finish(kDbBad);
    const Cr3Gene& a = s.cr3Genes[size_t(gene)];
    const GeneInfo* ia = geneInfo(a.master);
    if (!ia) return finish(kDbBad);
    for (size_t k = 0; k < s.cr3Genes.size(); ++k) {
        if (int(k) == gene) continue;
        const Cr3Gene& b = s.cr3Genes[k];
        const GeneInfo* ib = geneInfo(b.master);
        if (!ib) continue;
        const int la = geneLevel(a), lb = geneLevel(b);
        const bool bothTop = la == ia->maxLevel && lb == ib->maxLevel;
        // the result: the pair's, else one series' lower grade, else by kind, grade and rank
        int result = -1;
        for (const auto& m : mergePairs_)
            if (((m[0] == a.master && m[1] == b.master) || (m[0] == b.master && m[1] == a.master)) && m[2] != -1) {
                result = m[2];
                break;
            }
        if (result == -1) {
            if (ia->series == ib->series) result = ia->grade < ib->grade ? ia->id : ib->id;
            else {
                const int half = (ia->rank + ib->rank) / 2;
                const int kind = ia->kind >= 0 && ia->kind < 7 && ib->kind >= 0 && ib->kind < 7 ? kMergeKind[ia->kind][ib->kind] : -1;
                const int grade = std::min(ia->grade, ib->grade);
                int belowRank = 0, below = 0, aboveRank = 9999, above = 0;
                for (const auto& [id, c] : genes_) {
                    if (c.type == 1 || c.kind != kind || c.grade != grade) continue;
                    if (c.rank <= half + 1 && belowRank < c.rank) belowRank = c.rank, below = id;
                    if (half < c.rank && c.rank < aboveRank) aboveRank = c.rank, above = id;
                }
                result = aboveRank > 9998 ? below : above;
            }
        }
        if (bothTop && geneRarity(result) < 2) ++result;
        const GeneInfo* ir = geneInfo(result);
        if (!ir) continue;
        MergePreview p;
        p.partner = int(k), p.gold = (la + lb) * 100;
        p.result.master = result;
        p.result.exp = (ia->worthPerLevel * (la - 1) + ia->worth + ib->worth + ib->worthPerLevel * (lb - 1)) / 4;
        p.result.exp = std::min(p.result.exp, geneExp(result, ir->maxLevel));
        // the two inherited: both's commands, then skills, slot by slot, once each and not the
        // result's own (its first 16 learnt), shuffled - into slots 1 and 2 for InitGene
        std::vector<std::array<int, 2>> pool;
        auto offer = [&](int type, int id) {
            if (id < 0) return;
            for (const auto& v : pool)
                if (v[0] == type && v[1] == id) return;
            for (size_t n = 0; n < ir->learns.size() && n < 16; ++n)
                if (ir->learns[n][1] == type && ir->learns[n][2] == id) return;
            pool.push_back({type, id});
        };
        for (int sl = 0; sl < 8; ++sl) {
            if (a.slots[sl][0] == 0) offer(0, a.slots[sl][1]);
            if (b.slots[sl][0] == 0) offer(0, b.slots[sl][1]);
            if (a.slots[sl][0] == 1) offer(1, a.slots[sl][1]);
            if (b.slots[sl][0] == 1) offer(1, b.slots[sl][1]);
        }
        std::shuffle(pool.begin(), pool.end(), rng_);
        for (size_t n = 0; n < pool.size() && n < 2; ++n) p.result.slots[1 + n][0] = pool[n][0], p.result.slots[1 + n][1] = pool[n][1];
        initGene(p.result, true);
        previews_.push_back(p);
    }
    return finish(kDbOk);
}

int Cr3OfflineDb::mergeGene(GameState& s, int preview, bool coins) {
    if (preview < 0 || size_t(preview) >= previews_.size() || previewGene_ < 0 || size_t(previewGene_) >= s.cr3Genes.size())
        return finish(kDbBad);
    const MergePreview p = previews_[size_t(preview)];
    if (coins ? s.cr3Coins < p.coins : s.cr3Money < p.gold) return finish(coins ? kDbNoCoins : kDbNoGold);
    const int a = previewGene_, b = p.partner;
    s.cr3Genes.erase(s.cr3Genes.begin() + std::max(a, b));
    s.cr3Genes.erase(s.cr3Genes.begin() + std::min(a, b));
    s.cr3Genes.push_back(p.result);
    s.cr3Genes.back().fresh = true;
    s.cr3GeneBook.insert(p.result.master);
    if (coins) s.cr3Coins -= p.coins, s.cr3CoinsSpent += p.coins;
    else s.cr3Money -= p.gold;
    previews_.clear(), previewGene_ = -1;
    return finish(kDbOk);
}

int Cr3OfflineDb::enhanceGene(GameState& s, int gene, const std::vector<int>& fed) {
    if (gene < 0 || size_t(gene) >= s.cr3Genes.size()) return finish(kDbBad);
    const GeneInfo* it = geneInfo(s.cr3Genes[size_t(gene)].master);
    if (!it) return finish(kDbBad);
    const int level = geneLevel(s.cr3Genes[size_t(gene)]);
    int worth = 0, same = 0, gold = 0;
    for (const int f : fed) {
        if (f < 0 || size_t(f) >= s.cr3Genes.size() || f == gene) return finish(kDbBad);
        const Cr3Gene& g = s.cr3Genes[size_t(f)];
        const GeneInfo* i = geneInfo(g.master);
        if (!i) continue;
        worth += i->worth + (geneLevel(g) - 1) * i->worthPerLevel;
        same += i->kind == it->kind;
        gold += level * 100;
    }
    if (s.cr3Money < gold) return finish(kDbNoGold);
    addGeneExp(s.cr3Genes[size_t(gene)], same * worth / 100 + worth);
    std::vector<int> gone = fed;
    std::sort(gone.rbegin(), gone.rend());
    gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
    for (const int f : gone) s.cr3Genes.erase(s.cr3Genes.begin() + f);
    s.cr3Money -= gold;
    return finish(kDbOk);
}

int Cr3OfflineDb::enforceGene(GameState& s, int gene, int mode, int item, int n, int exp, const int (*slots)[2]) {
    if (gene < 0 || size_t(gene) >= s.cr3Genes.size()) return finish(kDbBad);
    Cr3Gene& g = s.cr3Genes[size_t(gene)];
    if (mode & 1) { // (a seed: which by its master id - the 27 in the constants' order)
        static const char* const kSeeds[] = {"POWER", "DEFENCE", "SPEED", "LUCK", "ENDLESS", "POWER_SEED2", "DEFENCE_SEED2",
                                             "SPEED_SEED2", "LUCK_SEED2", "ENDLESS_SEED2", "POWER_SEED3", "DEFENCE_SEED3",
                                             "SPEED_SEED3", "LUCK_SEED3", "ENDLESS_SEED3", "JAN", "FEB", "MAR", "APR", "MAY",
                                             "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        if (s.cr3Items[item] <= 0) return finish(kDbBad);
        const BsonValue* amounts = nullptr;
        for (const char* seed : kSeeds) {
            const std::string base = std::strchr(seed, '_') ? std::string(seed) : std::string(seed) + "_SEED";
            if (constant(base + "_MASTER_ID", -2) == item && constants()) amounts = constants()->get(base + "_AMOUNTS");
        }
        if (amounts && amounts->isArray()) {
            const int params = std::min(constant("GENE_PARAM_NUM", 4), 4), most = constant("GENE_ADD_MAX", 40);
            int total = 0;
            for (int k = 0; k < params; ++k) total += g.adds[k];
            for (int k = 0; k < params && size_t(k) < amounts->items.size(); ++k) {
                const int more = amounts->items[size_t(k)].asInt() * n;
                g.adds[k] += std::min(more, most - total);
                total = std::min(total + more, most);
            }
        }
    }
    if (mode & 2) g.exp = exp;
    if ((mode & 4) && slots)
        for (int k = 0; k < 8; ++k) g.slots[k][0] = slots[k][0], g.slots[k][1] = slots[k][1];
    if (mode & 0xe) removeItem(s, item, n);
    return finish(kDbOk);
}

int Cr3OfflineDb::moveGene(GameState& s, int gene, bool toStock) {
    if (gene < 0 || size_t(gene) >= s.cr3Genes.size() || s.cr3Genes[size_t(gene)].stocked == toStock) return finish(kDbBad);
    const int held = int(std::count_if(s.cr3Genes.begin(), s.cr3Genes.end(), [&](const Cr3Gene& g) { return g.stocked == toStock; }));
    if (held >= (toStock ? s.cr3GeneStock : constant("GENE_CAPACITY", 50))) return finish(kDbFull);
    s.cr3Genes[size_t(gene)].stocked = toStock;
    return finish(kDbOk);
}

int Cr3OfflineDb::removeGene(GameState& s, int gene) {
    if (gene < 0 || size_t(gene) >= s.cr3Genes.size()) return finish(kDbBad);
    s.cr3Genes.erase(s.cr3Genes.begin() + gene);
    return finish(kDbOk);
}

int Cr3OfflineDb::sellGenes(GameState& s, std::vector<int> genes) {
    int gold = 0;
    for (const int k : genes) {
        if (k < 0 || size_t(k) >= s.cr3Genes.size()) return finish(kDbBad);
        const GeneInfo* i = geneInfo(s.cr3Genes[size_t(k)].master);
        if (!i) return finish(kDbNone);
        gold += i->price + (geneLevel(s.cr3Genes[size_t(k)]) - 1) * i->pricePerLevel;
    }
    s.cr3Money += gold;
    std::sort(genes.rbegin(), genes.rend());
    genes.erase(std::unique(genes.begin(), genes.end()), genes.end());
    for (const int k : genes) s.cr3Genes.erase(s.cr3Genes.begin() + k);
    return finish(kDbOk);
}

// --- quests ---

int Cr3OfflineDb::findQuests(GameState& s, std::time_t now) {
    auto holds = [&](const Quest& q) { // (dbutils::CheckQuest)
        if ((q.stime != 0 && now < q.stime) || (q.etime != 0 && q.etime < now)) return false;
        for (const int f : q.flags)
            if ((f < 0 && s.flag(-f)) || (f > 0 && !s.flag(f))) return false;
        return true;
    };
    standingQuests_.clear(), foundQuests_.clear();
    std::vector<int> pool[6]; // (ranks 1 .. 4, then 5 and 6 together)
    for (size_t k = 0; k < quests_.size(); ++k) {
        const Quest& q = quests_[k];
        if (!holds(q)) continue;
        if (q.rank == 0) {
            if (standingQuests_.size() < 20) standingQuests_.push_back(int(k));
        } else if (q.rank >= 1 && q.rank <= 4) pool[q.rank].push_back(int(k));
        else if (q.rank == 5 || q.rank == 6) pool[5].push_back(int(k));
    }
    for (std::vector<int>& p : pool) std::shuffle(p.begin(), p.end(), rng_);
    if (!(s.cr3Daily & 4)) {
        int slot[20] = {};
        for (size_t k = 0; k < std::min<size_t>(pool[4].size(), 10); ++k) slot[k] = pool[4][k];
        for (size_t k = 0; k < std::min<size_t>(pool[3].size(), 6); ++k) slot[4 + k] = pool[3][k];
        for (size_t k = 0; k < std::min<size_t>(pool[2].size(), 3); ++k) slot[7 + k] = pool[2][k];
        for (size_t k = 0; k < std::min<size_t>(pool[1].size(), 10); ++k) slot[9 - k] = pool[1][k];
        s.cr3Quests.offered.clear();
        for (const int v : slot)
            if (v > 0) s.cr3Quests.offered.push_back(v); // (index 0 never: its > 0 test)
        s.cr3Daily |= 4;
    }
    for (const int v : s.cr3Quests.offered)
        if (v > 0) foundQuests_.push_back(v);
    for (size_t k = 0; k < std::min<size_t>(pool[5].size(), 10); ++k) foundQuests_.push_back(pool[5][k]);
    return finish(kDbOk);
}

int Cr3OfflineDb::beginQuest(GameState& s, int id) {
    if (id < 0 || id > 999) return finish(kDbBad);
    s.cr3Quests.state = 0;
    s.cr3Quests.begun.insert(id);
    return finish(kDbOk);
}

int Cr3OfflineDb::endQuest(GameState& s, int id) {
    if (id < 0 || id >= 1000) return finish(kDbBad);
    s.cr3Quests.state = 3;
    s.cr3Quests.ended.insert(id);
    ++s.cr3Quests.endedCount;
    auto& o = s.cr3Quests.offered;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (*it >= 0 && size_t(*it) < quests_.size() && quests_[size_t(*it)].id == id) {
            o.erase(it); // (its slot emptied, the rest moved up)
            break;
        }
    return finish(kDbOk);
}

int Cr3OfflineDb::setQuest(GameState& s, int id) {
    s.cr3Quests.offered.clear();
    s.cr3Quests.begun.erase(id), s.cr3Quests.ended.erase(id);
    for (size_t k = 0; k < quests_.size(); ++k)
        if (quests_[k].id == id) s.cr3Quests.offered = {int(k)};
    return finish(kDbOk);
}

// dbutils::EarnQuestPrize: by its category - item, gold, genebox, luckybox, else a gene
int Cr3OfflineDb::earnQuestPrize(GameState& s, const BsonValue& p) {
    const std::string c = p.string("category");
    if (c == "item") return earnItemPrize(s, p.integer("item_id"), p.integer("count"));
    if (c == "gold") return s.cr3Money += p.integer("count"), kDbOk;
    if (c == "genebox") return openGeneBox(s, p.integer("item_id"), 1, 0);
    if (c == "luckybox") return openLuckyBox(s, p.string("item_id"), 1, 0);
    return earnGenePrize(s, p.integer("item_id"));
}

int Cr3OfflineDb::earnQuestRewards(GameState& s, int id) {
    if (s.cr3Quests.state > 0) return finish(kDbQuestBusy);
    if (const BsonValue* t = table("questprizes"))
        for (const BsonValue& r : t->items)
            if (r.integer("_id") == id)
                if (const BsonValue* items = r.get("items"); items && items->isArray())
                    for (const BsonValue& p : items->items) earnQuestPrize(s, p);
    s.cr3Quests.state = 1;
    return finish(kDbOk);
}

// --- tickets and events ---

int Cr3OfflineDb::gotTicket(GameState& s) {
    ticket_ = (s.cr3Daily & 1) != 0;
    return finish(kDbOk);
}

int Cr3OfflineDb::receiveTicket(GameState& s) {
    if (s.cr3Daily & 1) return finish(kDbTicketHad);
    earnItemPrize(s, 118, 1);
    ticket_ = true;
    s.cr3Daily |= 1;
    return finish(kDbOk);
}

int Cr3OfflineDb::sync2(GameState& s, const std::string& name) {
    for (const Sync2Gift& g : kSync2) {
        if (name != g.name) continue;
        for (const int gene : g.gene)
            if (gene > 0) earnGenePrize(s, gene);
        for (int k = 0; k < 2; ++k)
            if (g.item[k] > 0) earnItemPrize(s, g.item[k], g.count[k]);
        return finish(kDbOk);
    }
    return finish(kDbBad);
}

} // namespace cr3
