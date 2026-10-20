#include "script/gamestate.hpp"

#include <algorithm>

#include <ctime>
#include <sstream>

#include "script/vm.hpp"

namespace cr3 {

void bindGameState(ScriptVM& vm, GameState& state) {
    vm.setNative("Cr3Flag", "Set", [&state](NativeCall& c) { state.setFlag(c.integer(0), true); });
    vm.setNative("Cr3Flag", "Clear", [&state](NativeCall& c) { state.setFlag(c.integer(0), false); });
    vm.setNative("Cr3Flag", "Check", [&state](NativeCall& c) { c.result(state.flag(c.integer(0))); });
    // (Cr3Work is the global work: Cr3BindWork's Set / Get are Cr3UtilGlobalWorkSet / Get - 32
    // values, others ignored, read as 0)
    vm.setNative("Cr3Work", "Set", [&state](NativeCall& c) {
        const int k = c.integer(0);
        if (k >= 0 && k < 32) state.globalWork[k] = c.integer(1);
    });
    vm.setNative("Cr3Work", "Get", [&state](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k >= 0 && k < 32 ? state.globalWork[k] : 0);
    });
    GameState::Next& next = state.next;
    // FldUtilSetVistaFuncName: the event, and that a vista is to come; its map ("null": none)
    // CR3's utils::WaitSync: DBSystem::IsIdle - no request queued. The port's database is the
    // Vita's, offline (db/cr3db.hpp): a request is done when it is made
    vm.setNative("Cr3System", "WaitSync", [](NativeCall& c) { c.result(true); });
    // utils::GetSyncResult: DBSystem's last request's result (+0x1bc) - an HTTP-like code the
    // scripts compare: 200 done, 1006 a ticket quest's error, ...
    vm.setNative("Cr3System", "GetSyncResult", [&state](NativeCall& c) { c.result(state.cr3Db ? state.cr3Db->lastResult() : int(kDbOk)); });
    // utils::EnterDungeon: DBSystem::Request(EnterDungeon) - the server's dungeon entry (its gems,
    // GEM_COUNT_IN_DUNGEON), not one the Vita plays offline: dropped
    vm.setNative("Cr3System", "EnterDungeon", [](NativeCall&) {});
    // the item sheets (a dungeon's treasure prizes: Fld2OpenItemSheetRequest - DBSystem::
    // OpenItemSheet(sheet, no key, no flag) -, Fld2SetItemSheetName, Fld2GetOpenItemSheetItemNo /
    // Type - the prize last won: its id, 1 a gene -, Fld2CheckOpenItemSheet / OpenItemSheet - the
    // field's sheet screen, Fld2Main step 0xc: none yet, done at once)
    vm.setNative("Cr3GimmickData", "OpenItemSheetRequest", [&state](NativeCall& c) {
        if (state.cr3Db) state.cr3Db->openItemSheet(state, c.string(0), false, -1);
    });
    vm.setNative("Cr3GimmickData", "SetItemSheetName", [&state](NativeCall& c) {
        if (state.cr3Db) state.cr3Db->setOpenedSheet(c.string(0));
    });
    vm.setNative("Cr3GimmickData", "GetOpenItemSheetItemNo", [&state](NativeCall& c) {
        const Cr3ItemSheet::Prize* p = state.cr3Db ? state.cr3Db->openedPrize() : nullptr;
        c.result(p ? p->item : 0);
    });
    vm.setNative("Cr3GimmickData", "GetOpenItemSheetItemType", [&state](NativeCall& c) {
        const Cr3ItemSheet::Prize* p = state.cr3Db ? state.cr3Db->openedPrize() : nullptr;
        c.result(p && p->gene ? 1 : 0);
    });
    vm.setNative("Cr3GimmickData", "OpenItemSheet", [](NativeCall&) {});
    vm.setNative("Cr3GimmickData", "CheckOpenItemSheet", [](NativeCall& c) { c.result(true); });
    // the rest of Cr3System's database calls (libcrx's Sqrat bindings: utils::*), offline
    Cr3OfflineDb* const& db = state.cr3Db;
    // utils::OpenGBox(sheet, entry, key) / OpenGBoxAndSetFlag(.., flag): DBSystem::
    // OpenTreasureBox - that entry won; GetItemSheetNo: the sheet's index (-1 none)
    vm.setNative("Cr3System", "OpenGBox", [&state, &db](NativeCall& c) {
        if (db && !c.string(0).empty()) db->openTreasureBox(state, c.string(0), c.integer(1), c.boolean(2), -1);
    });
    vm.setNative("Cr3System", "OpenGBoxAndSetFlag", [&state, &db](NativeCall& c) {
        if (db && !c.string(0).empty()) db->openTreasureBox(state, c.string(0), c.integer(1), c.boolean(2), c.integer(3, -1));
    });
    vm.setNative("Cr3System", "GetItemSheetNo", [&db](NativeCall& c) { c.result(db ? db->findItemSheet(c.string(0)) : -1); });
    // utils::OpenPrizeBox(name) ("ocean"), GetPrizeBoxItemType / Id / Count, CheckPrizeBoxPresent
    // (it went to the presents box)
    vm.setNative("Cr3System", "OpenPrizeBox", [&state, &db](NativeCall& c) {
        if (db) db->openPrizeBox(state, c.string(0));
    });
    vm.setNative("Cr3System", "GetPrizeBoxItemType", [&db](NativeCall& c) { c.result(db ? db->prizeBox().type : 0); });
    vm.setNative("Cr3System", "GetPrizeBoxItemId", [&db](NativeCall& c) { c.result(db ? db->prizeBox().item : 0); });
    vm.setNative("Cr3System", "GetPrizeBoxItemCount", [&db](NativeCall& c) { c.result(db ? db->prizeBox().count : 0); });
    vm.setNative("Cr3System", "CheckPrizeBoxPresent", [&db](NativeCall& c) { c.result(db && db->prizeBox().toPresents); });
    // utils::EarnPresent(n), Sync2(event): the presents, an event's gifts; utils::Sync: SyncWait,
    // nothing to wait for
    vm.setNative("Cr3System", "EarnPresent", [&state, &db](NativeCall& c) {
        if (db) db->earnPresent(state, c.integer(0));
    });
    vm.setNative("Cr3System", "Sync2", [&state, &db](NativeCall& c) {
        if (db) db->sync2(state, c.string(0));
    });
    vm.setNative("Cr3System", "Sync", [&db](NativeCall&) {
        if (db) db->finish(kDbOk);
    });
    // the day's ticket: utils::CheckTicketReceived (GotTicket) - its Result -, ReceiveTicket
    vm.setNative("Cr3System", "CheckTicketReceived", [&state, &db](NativeCall&) {
        if (db) db->gotTicket(state);
    });
    vm.setNative("Cr3System", "CheckTicketReceivedResult", [&db](NativeCall& c) { c.result(db && db->ticketReceived()); });
    vm.setNative("Cr3System", "ReceiveTicket", [&state, &db](NativeCall&) {
        if (db) db->receiveTicket(state);
    });
    // quests: utils::SelectQuest(n) - DBSelectQuestTask(n): FindQuests, then the quest board's
    // choice (no board yet: none chosen, -100) -, GetSelectedQuest, StartQuest (CrxGameActivities'
    // record: begun), CompleteQuest (ended - one more done), GetQuestState
    vm.setNative("Cr3System", "SelectQuest", [&state, &db](NativeCall&) {
        if (db) db->findQuests(state, std::time(nullptr));
        state.cr3Quests.selected = -100;
    });
    vm.setNative("Cr3System", "GetSelectedQuest", [&state](NativeCall& c) { c.result(state.cr3Quests.selected); });
    vm.setNative("Cr3System", "StartQuest", [&state](NativeCall& c) { state.cr3Quests.begun.insert(c.integer(0)); });
    vm.setNative("Cr3System", "CompleteQuest", [&state](NativeCall& c) {
        state.cr3Quests.ended.insert(c.integer(0));
        ++state.cr3Quests.endedCount;
    });
    vm.setNative("Cr3System", "GetQuestState", [&state](NativeCall& c) { c.result(state.cr3Quests.state); });
    // money: utils::SetGold (GoldSet) / GetGold (GoldGet: n more) / CheckGold (GoldCheck); the
    // Oz coins likewise (CoinGet / CoinCheck)
    vm.setNative("Cr3System", "GoldSet", [&state](NativeCall& c) { state.cr3Money = c.integer(0); });
    vm.setNative("Cr3System", "GoldGet", [&state](NativeCall& c) { state.cr3Money += c.integer(0); });
    vm.setNative("Cr3System", "GoldCheck", [&state](NativeCall& c) { c.result(state.cr3Money); });
    vm.setNative("Cr3System", "CoinGet", [&state](NativeCall& c) { state.cr3Coins += c.integer(0); });
    vm.setNative("Cr3System", "CoinCheck", [&state](NativeCall& c) { c.result(state.cr3Coins); });
    // utils::GetItemWithCategory(category, id, count): 0 an item, 1 .. 6 x 100000 + id, 10 gold
    vm.setNative("Cr3System", "GetItemWithCategory", [&state, &db](NativeCall& c) {
        const int cat = c.integer(0), id = c.integer(1), n = c.integer(2, 1);
        if (cat == 10) state.cr3Money += id;
        else if (cat >= 0 && cat <= 6 && db) db->earnItemPrize(state, cat * 100000 + id, n);
    });
    // genes: utils::GetGene(id, level) - a gene in the first of the 50 places free (its index; -1
    // none) -, CheckGene (how many held, the stock's too), CheckNewGene (never had), CheckGeneFull
    vm.setNative("Cr3System", "GeneGet", [&state, &db](NativeCall& c) {
        if (int(state.cr3Genes.size()) >= (db ? db->constant("GENE_CAPACITY", 50) : 50)) return c.result(-1);
        Cr3Gene g{c.integer(0)};
        if (db) { // (CrxGene::Initialize(level): that level's EXP, its commands and skills)
            if (c.integer(1) > 0) g.exp = db->geneExp(g.master, c.integer(1));
            db->initGene(g);
        }
        state.cr3Genes.push_back(g);
        state.cr3GeneBook.insert(g.master);
        c.result(int(state.cr3Genes.size()) - 1);
    });
    vm.setNative("Cr3System", "GeneCheck", [&state](NativeCall& c) {
        const int id = c.integer(0);
        c.result(int(std::count_if(state.cr3Genes.begin(), state.cr3Genes.end(), [id](const Cr3Gene& g) { return g.master == id; })));
    });
    vm.setNative("Cr3System", "CheckNewGene", [&state](NativeCall& c) { c.result(state.cr3GeneBook.count(c.integer(0)) == 0); });
    vm.setNative("Cr3System", "CheckGeneFull", [&state, &db](NativeCall& c) {
        c.result(int(state.cr3Genes.size()) >= (db ? db->constant("GENE_CAPACITY", 50) : 50) + state.cr3GeneStock);
    });
    vm.setNative("Cr3System", "SetVistaFuncName", [&next](NativeCall& c) { next.vistaFunction = c.string(0), next.vista = true; });
    vm.setNative("Cr3System", "SetVistaMapName", [&next](NativeCall& c) { next.vistaMap = c.string(0); });
    vm.setNative("Cr3System", "SetVistaFuncName2", [&next](NativeCall& c) { next.vistaFunction = c.string(0); }); // (Fld2SetVistaFuncName2)
    vm.setNative("Cr3System", "SetBattleNext", [&next](NativeCall& c) { next.battleNext = c.integer(0, 1); }); // (FldUtilSetBattleNext)
    vm.setNative("Cr3System", "BattleIN", [&next](NativeCall& c) { next.battle = c.integer(0); }); // (FldUtilReserveBattleIN)
    for (const char* name : {"BattleIn", "BattleIn2"}) // (CR3's: FldUtilReserveBattleIN / ..IN2)
        vm.setNative("Cr3System", name, [&next](NativeCall& c) {
            next.battle = c.integer(0), next.battleField = c.integer(1), next.battleLevel = c.integer(2, 1), next.battleMusic = c.integer(3, -1);
            next.battleMusicAfter = c.integer(4, 32); // (BattleIn: 0x20)
        });
    vm.setNative("Cr3System", "SetParty", [&state](NativeCall& c) {
        const int k = c.integer(0);
        if (k >= 0 && k < 5) state.cr3Party[k] = c.integer(1);
    });
    vm.setNative("Cr3System", "GetParty", [&state](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k >= 0 && k < 5 ? state.cr3Party[k] : 0);
    });
    vm.setNative("Cr3System", "SetBattleParty", [&next](NativeCall& c) {
        const int k = c.integer(0);
        if (k == 0 || k == 1) next.battleParty[k] = c.integer(1, -1);
    });
    // the party (Cr3UtilSetActivePartyMember / Get..., SetPartyMember, CopyPartyMember)
    vm.setNative("Cr3System", "SetActivePartyMember", [&state](NativeCall& c) {
        const int k = c.integer(0);
        if (k == 0 || k == 1) state.party[k] = c.integer(1, -1);
    });
    vm.setNative("Cr3System", "GetActivePartyMember", [&state](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k == 0 || k == 1 ? state.party[k] : -1);
    });
    vm.setNative("Cr3System", "SetPartyMember", [&state](NativeCall& c) {
        const int s = c.integer(0), k = c.integer(1);
        if (s >= 0 && s < 3 && (k == 0 || k == 1)) state.partySets[s][k] = c.integer(2, -1);
    });
    vm.setNative("Cr3System", "CopyPartyMember", [&state](NativeCall& c) {
        const int s = c.integer(0);
        if (s >= 0 && s < 3) state.party[0] = state.partySets[s][0], state.party[1] = state.partySets[s][1];
    });
    // Cr3UtilSetCharaEquip / GetCharaEquip: the character, the slot (0 weapon, 1 armour, 2
    // accessory, 6 the second sopia, 3..5 the third to fifth - the first not by a script), the id
    static const int kEquipSlot[7] = {0, 1, 2, 5, 6, 7, 4};
    vm.setNative("Cr3System", "SetEquip", [&state](NativeCall& c) {
        const int ch = c.integer(0), s = c.integer(1);
        if (ch >= 0 && ch < 9 && s >= 0 && s < 7) state.equip[ch][kEquipSlot[s]] = static_cast<short>(c.integer(2));
    });
    vm.setNative("Cr3System", "GetEquip", [&state](NativeCall& c) {
        const int ch = c.integer(0), s = c.integer(1);
        c.result(ch >= 0 && ch < 9 && s >= 0 && s < 7 ? int(state.equip[ch][kEquipSlot[s]]) : 0);
    });
    // Cr3UtilGetSopia (had, at level 1, no EXP) / CheckSopia / GetSopiaLv
    vm.setNative("Cr3System", "SopiaGet", [&state](NativeCall& c) {
        const int s = c.integer(0);
        if (s >= 0 && s < 32) state.sopiaOwned |= 1u << s, state.sopiaLevel[s] = 1, state.sopiaExp[s] = 0;
    });
    vm.setNative("Cr3System", "SopiaCheck", [&state](NativeCall& c) {
        const int s = c.integer(0);
        c.result(s >= 0 && s < 32 && (state.sopiaOwned >> s & 1) != 0);
    });
    vm.setNative("Cr3System", "GetSopiaLv", [&state](NativeCall& c) {
        const int s = c.integer(0);
        c.result(s >= 0 && s < 32 ? int(state.sopiaLevel[s]) : 0);
    });
    // Cr3UtilGetItem(item, n): its count n more, 0..99; Cr3UtilCheckItem: its count
    vm.setNative("Cr3System", "ItemGet", [&state](NativeCall& c) {
        const int id = c.integer(0);
        if (id > 0) state.cr3Items[id] = std::clamp(state.cr3Items[id] + c.integer(1, 1), 0, 99); // (CR3's, by its full id)
        if (id > 0 && id < 300) state.itemCount[id] = static_cast<short>(std::clamp(state.itemCount[id] + c.integer(1, 1), 0, 99));
    });
    // utils::SetItem(item, character): the character wears it, its slot by the id (1xxxxx weapon,
    // 2xxxxx armour, 3xxxxx accessory: test_equip.i's SetEquip - PartyEquip, a character's
    // starting kit); utils::GetAndSetItem: one more of it, then the same
    vm.setNative("Cr3System", "ItemSet", [&state](NativeCall& c) {
        const int id = c.integer(0), ch = c.integer(1);
        if (id >= 200000 && id < 300000 && ch > 0) state.cr3Armour[ch] = id - 200000;
    });
    vm.setNative("Cr3System", "ItemGetAndSet", [&state](NativeCall& c) {
        const int id = c.integer(0), ch = c.integer(1);
        if (id >= 100000) state.cr3Items[id] = std::min(state.cr3Items[id] + 1, 99);
        if (id >= 200000 && id < 300000 && ch > 0) state.cr3Armour[ch] = id - 200000;
    });
    vm.setNative("Cr3System", "ItemCheck", [&state](NativeCall& c) {
        const int id = c.integer(0);
        c.result(id > 0 && state.cr3Items.count(id) ? state.cr3Items.at(id) : 0);
    });
    vm.setNative("Cr3System", "MPFullRecovery", [&state](NativeCall&) {
        for (int& m : state.mp) m = -1;
    });
    vm.setNative("Cr3System", "SetPopPoint", [&next](NativeCall& c) { next.popPoint = c.string(0); }); // (FldUtilSetPopPoint)
    vm.setNative("Cr3System", "SetComeFromBattleToField", [&next](NativeCall&) { next.fieldMode = 1; });
    vm.setNative("Cr3System", "ReserveWorldSelect", [&next](NativeCall&) { next.worldSelect = true; }); // (FldUtilReserveWorldSelect)
    vm.setNative("Cr3System", "ReserveShop", [&next](NativeCall&) { next.shop = true; });
    vm.setNative("Cr3System", "ReserveParty", [&next](NativeCall&) { next.party = true; });
    vm.setNative("Cr3System", "ReserveChapter", [&next](NativeCall&) { next.chapter = true; });
    vm.setNative("InterfaceWindow", "GetChapterID", [&state](NativeCall& c) { c.result(state.selectChapter); }); // (CR3InterfaceEvent_GetSelectChapterID)
    vm.setNative("Cr3System", "GetDungeonNum", [&state](NativeCall& c) { c.result(state.dungeon); });
    vm.setNative("Cr3System", "GetAreaNum", [&state](NativeCall& c) { c.result(state.area); });
    // the character change (FldChangeCharacter): its button allowed, the leader, a change at once
    // (FldMain::ChangeCharacterQuick: the other of the two, unless it leads already); the tab
    vm.setNative("Cr3ChangeCharacter", "SetEnable", [&state](NativeCall& c) { state.changeEnable = c.boolean(0); });
    vm.setNative("Cr3TabMenu", "SetEnable", [&state](NativeCall& c) { state.tabEnable = c.boolean(0); }); // (FldUtilSetTabMenuEnable)
    vm.setNative("Cr3TabMenu", "GetEnable", [&state](NativeCall& c) { c.result(state.tabEnable); });
    vm.setNative("Cr3ChangeCharacter", "GetEnable", [&state](NativeCall& c) { c.result(state.changeEnable); });
    vm.setNative("Cr3ChangeCharacter", "GetActive", [&state](NativeCall& c) { c.result(state.active); });
    vm.setNative("Cr3ChangeCharacter", "ExeQuick", [&state](NativeCall& c) {
        if (c.integer(0) != state.active) state.active = state.active == 0 ? 1 : 0;
    });
    vm.setNative("Cr3System", "SetBGColor", [&state](NativeCall& c) {
        for (int k = 0; k < 3; ++k) state.bgColor[k] = c.number(k);
    });
    vm.setNative("Cr3System", "ReserveLibrary", [&next](NativeCall&) { next.library = true; });
    vm.setNative("Cr3System", "ReserveCamp", [&next](NativeCall&) { next.camp = true; });
    vm.setNative("Cr3System", "ReservePUB", [&next](NativeCall&) { next.pub = true; });
    vm.setNative("Cr3System", "ReserveTitle", [&next](NativeCall&) { next.title = true; });
    vm.setNative("Cr3System", "ChangeDungeon", [&next](NativeCall& c) { next.changeDungeon = c.integer(0), next.changeArea = c.integer(1); });
    vm.setNative("Cr3System", "ChangeArea", [&next, &state](NativeCall& c) { next.changeDungeon = state.dungeon, next.changeArea = c.integer(0); });
    // the Oz (Cr3UtilSetOZ: held at 0 .. 9,999,999) and the encounters' switch
    vm.setNative("Cr3System", "OZSet", [&state](NativeCall& c) { state.oz = std::clamp(c.integer(0), 0, 9999999); });
    vm.setNative("Cr3System", "OZCheck", [&state](NativeCall& c) { c.result(state.oz); });
    vm.setNative("Cr3System", "SetEncountEnable", [&state](NativeCall& c) { state.encountEnable = c.boolean(0); });
    vm.setNative("Cr3System", "ReserveCredit", [&next](NativeCall& c) { next.credit = c.integer(0); }); // (FldUtilReserveCredit)
    vm.setNative("InterfaceWindow", "SetArchiveID", [&state](NativeCall& c) { state.libraryArchive = c.integer(0); });
    vm.setNative("InterfaceWindow", "GetLibraryID", [&state](NativeCall& c) { c.result(state.libraryId); });
    vm.setNative("Cr3System", "SetComeFromVistaToField", [&next](NativeCall&) { next.fieldMode = 2; });
}

} // namespace cr3

namespace cr3 {

std::string GameState::save(const std::map<std::string, std::string>& extra) const {
    std::ostringstream o;
    o << "CR3-U save 1\n";
    o << "flags";
    for (size_t k = 0; k < flags.size(); ++k)
        if (flags[k]) o << ' ' << k;
    o << '\n';
    o << "party " << party[0] << ' ' << party[1] << '\n';
    o << "active " << active << ' ' << int(changeEnable) << ' ' << int(tabEnable) << '\n';
    for (int s = 0; s < 3; ++s) o << "pairs " << s << ' ' << partySets[s][0] << ' ' << partySets[s][1] << '\n';
    for (int id = 1; id < 300; ++id)
        if (itemCount[id]) o << "item " << id << ' ' << itemCount[id] << '\n';
    o << "oz " << oz << '\n';
    for (int c = 0; c < 9; ++c) {
        o << "chara " << c << ' ' << level[c] << ' ' << exp[c] << ' ' << mp[c];
        for (int e = 0; e < 8; ++e) o << ' ' << equip[c][e];
        for (int k = 0; k < 5; ++k) o << ' ' << seed[c][k];
        o << '\n';
    }
    for (int s = 0; s < 32; ++s) o << "sopia " << s << ' ' << int(sopiaLevel[s]) << ' ' << sopiaExp[s] << '\n';
    o << "owned " << sopiaOwned << '\n';
    o << "difficulty " << difficulty << '\n';
    o << "select " << selectDungeon << '\n';
    o << "boxes " << boxes.table;
    for (int k = 0; k < 3; ++k)
        for (int n = 0; n < 10; ++n) o << ' ' << int(boxes.set[k][n]) * 1 + int(boxes.open[k][n]) * 2;
    o << '\n';
    o << "global";
    for (int v : globalWork) o << ' ' << v;
    o << '\n';
    // the offline database's (cr3Db: what the Vita's DBEmu keeps in its save) - the party, items,
    // equipment, money, genes, item sheets, presents, quests and the days' marks
    o << "cr3party";
    for (int c : cr3Party) o << ' ' << c;
    o << '\n';
    for (const auto& [id, n] : cr3Items) o << "cr3item " << id << ' ' << n << '\n';
    for (const auto& [c, a] : cr3Armour) o << "cr3armour " << c << ' ' << a << '\n';
    for (const auto& [id, a] : cr3Attachments) o << "cr3attach " << id << ' ' << a.first << ' ' << a.second << '\n';
    o << "cr3money " << cr3Money << ' ' << cr3Coins << ' ' << cr3CoinsSpent << ' ' << cr3GP << ' ' << cr3GPSpent << ' ' << cr3GeneStock << '\n';
    o << "cr3days " << cr3Daily << ' ' << cr3VipDays << ' ' << int(cr3VipTrial) << ' ' << cr3LastLogin << ' ' << cr3Cheer << '\n';
    auto gene = [&o](const Cr3Gene& g) {
        o << g.master << ' ' << g.exp << ' ' << int(g.locked) << ' ' << int(g.stocked) << ' ' << int(g.fresh);
        for (const auto& sl : g.slots) o << ' ' << sl[0] << ' ' << sl[1];
        for (int a : g.adds) o << ' ' << a;
    };
    for (const Cr3Gene& g : cr3Genes) {
        o << "cr3gene ";
        gene(g);
        o << '\n';
    }
    o << "cr3book";
    for (int id : cr3GeneBook) o << ' ' << id;
    o << '\n';
    for (const auto& [name, p] : cr3Sheets) {
        o << "cr3sheet " << name << ' ' << p.completed << ' ' << int(p.completedNew) << ' ' << int(p.completedSeen) << ' ' << p.opened.size();
        for (size_t k = 0; k < p.opened.size(); ++k) o << ' ' << p.opened[k] << ' ' << int(k < p.mark.size() && p.mark[k]);
        o << '\n';
    }
    for (const Cr3Present& p : cr3Presents) {
        o << "cr3present " << p.item << ' ' << p.count << ' ' << p.from << ' ' << int(p.gene);
        if (p.gene) o << ' ', gene(p.geneData);
        o << '\n';
    }
    o << "cr3quests " << cr3Quests.state << ' ' << cr3Quests.endedCount << ' ' << cr3Quests.selected << '\n';
    for (const auto& [key, ids] : {std::pair<const char*, const std::set<int>*>{"cr3begun", &cr3Quests.begun}, {"cr3ended", &cr3Quests.ended}}) {
        o << key;
        for (int id : *ids) o << ' ' << id;
        o << '\n';
    }
    o << "cr3offered";
    for (int id : cr3Quests.offered) o << ' ' << id;
    o << '\n';
    for (const auto& [k, v] : extra) o << k << ' ' << v << '\n';
    return o.str();
}

bool GameState::load(const std::string& text, std::map<std::string, std::string>* extra, std::string* error) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "CR3-U save 1") {
        if (error) *error = "not a CR3-U save";
        return false;
    }
    flags.clear();
    cr3Items.clear(), cr3Armour.clear(), cr3Attachments.clear(), cr3Genes.clear(), cr3GeneBook.clear();
    cr3Sheets.clear(), cr3Presents.clear(), cr3Quests = Cr3Quests();
    auto readGene = [](std::istringstream& l, Cr3Gene& g) {
        int locked = 0, stocked = 0, fresh = 0;
        if (!(l >> g.master >> g.exp >> locked >> stocked >> fresh)) return false;
        g.locked = locked != 0, g.stocked = stocked != 0, g.fresh = fresh != 0;
        for (auto& sl : g.slots) l >> sl[0] >> sl[1];
        for (int& a : g.adds) l >> a;
        return true;
    };
    std::vector<std::pair<int, int>> oldWork;
    for (short& n : itemCount) n = 0;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string key;
        if (!(l >> key)) continue;
        if (key == "flags") {
            for (int n; l >> n;) setFlag(n, true);
        } else if (key == "work") {
            int k = 0, v = 0;
            if (l >> k >> v && k >= 0 && k < 32) oldWork.push_back({k, v}); // (an older save's: the same values, set after)
        } else if (key == "party") {
            l >> party[0] >> party[1];
        } else if (key == "active") {
            int e = 1, t = 1;
            l >> active >> e >> t;
            active = active == 1 ? 1 : 0, changeEnable = e != 0, tabEnable = t != 0;
        } else if (key == "pairs") {
            int s = -1;
            if (l >> s && s >= 0 && s < 3) l >> partySets[s][0] >> partySets[s][1];
        } else if (key == "item") {
            int id = 0, n = 0;
            if (l >> id >> n && id > 0 && id < 300) itemCount[id] = static_cast<short>(n);
        } else if (key == "oz") {
            l >> oz;
        } else if (key == "chara") {
            int c = -1;
            if (!(l >> c) || c < 0 || c >= 9) continue;
            l >> level[c] >> exp[c] >> mp[c];
            for (short& e : equip[c]) l >> e;
            for (short& k : seed[c]) l >> k;
        } else if (key == "sopia") {
            int s = -1, lv = 0, e = 0;
            if (l >> s >> lv >> e && s >= 0 && s < 32) sopiaLevel[s] = static_cast<signed char>(lv), sopiaExp[s] = e;
        } else if (key == "owned") {
            l >> sopiaOwned;
        } else if (key == "select") {
            l >> selectDungeon;
        } else if (key == "difficulty") {
            l >> difficulty;
        } else if (key == "boxes") {
            l >> boxes.table;
            for (int k = 0; k < 3; ++k)
                for (int n = 0; n < 10; ++n) {
                    int v = 0;
                    l >> v;
                    boxes.set[k][n] = v & 1, boxes.open[k][n] = (v & 2) != 0;
                }
        } else if (key == "global") {
            for (int& v : globalWork) l >> v;
        } else if (key == "cr3party") {
            for (int& c : cr3Party) l >> c;
        } else if (key == "cr3item") {
            int id = 0, n = 0;
            if (l >> id >> n) cr3Items[id] = n;
        } else if (key == "cr3armour") {
            int c = 0, a = 0;
            if (l >> c >> a) cr3Armour[c] = a;
        } else if (key == "cr3attach") {
            int id = 0, m = 0, lv = 0;
            if (l >> id >> m >> lv) cr3Attachments[id] = {m, lv};
        } else if (key == "cr3money") {
            l >> cr3Money >> cr3Coins >> cr3CoinsSpent >> cr3GP >> cr3GPSpent >> cr3GeneStock;
        } else if (key == "cr3days") {
            int trial = 0;
            l >> cr3Daily >> cr3VipDays >> trial >> cr3LastLogin >> cr3Cheer;
            cr3VipTrial = trial != 0;
        } else if (key == "cr3gene") {
            Cr3Gene g;
            if (readGene(l, g)) cr3Genes.push_back(g);
        } else if (key == "cr3book") {
            for (int id; l >> id;) cr3GeneBook.insert(id);
        } else if (key == "cr3sheet") {
            std::string name;
            Cr3SheetProgress p;
            int cn = 0, cs = 0;
            size_t n = 0;
            if (!(l >> name >> p.completed >> cn >> cs >> n) || n > 10000) continue;
            p.completedNew = cn != 0, p.completedSeen = cs != 0;
            p.opened.resize(n), p.mark.resize(n);
            for (size_t k = 0; k < n; ++k) {
                int m = 0;
                l >> p.opened[k] >> m;
                p.mark[k] = m != 0;
            }
            cr3Sheets[name] = p;
        } else if (key == "cr3present") {
            Cr3Present p;
            int g = 0;
            if (!(l >> p.item >> p.count >> p.from >> g)) continue;
            p.gene = g != 0;
            if (p.gene && !readGene(l, p.geneData)) continue;
            cr3Presents.push_back(p);
        } else if (key == "cr3quests") {
            l >> cr3Quests.state >> cr3Quests.endedCount >> cr3Quests.selected;
        } else if (key == "cr3begun" || key == "cr3ended") {
            for (int id; l >> id;) (key == "cr3begun" ? cr3Quests.begun : cr3Quests.ended).insert(id);
        } else if (key == "cr3offered") {
            for (int id; l >> id;) cr3Quests.offered.push_back(id);
        } else if (extra) {
            std::string rest;
            std::getline(l >> std::ws, rest);
            (*extra)[key] = rest;
        }
    }
    for (const auto& [k, v] : oldWork) globalWork[k] = v;
    itemsReady = equipReady = sopiaReady = true; // (a new game's set-up not over it)
    return true;
}

} // namespace cr3
