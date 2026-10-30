#pragma once
// The story's state the scripts read and write - the save data's flags and work values - and what
// they have asked for next (cr3_script_work: FldUtilSet* / Reserve*), which the engine acts on
// when the script running has finished (FldMain::BranchAfterEvent, VistaMain::BranchAfterVista).

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "db/cr3db.hpp"
#include "field/treasure.hpp"

namespace cr3 {

class ScriptVM;

struct GameState {
    std::vector<bool> flags; // Cr3Flag: Set / Clear / Check
    // the party (cr3_common_data): the two fighting now (+3, +4: characters from 0 - Darwin 0,
    // Orlando 1, Marie 2 ...) and three pairs kept (+5..+10) - a new game's
    // (Cr3InitWorkFromBattleData): Darwin and Marie, the pairs 0 2, 1 3, 4 5
    int party[2] = {0, 2};
    // CR3's party (utils::SetParty / GetParty: five slots of char_para ids, 0 none; the field's
    // player Fld2CharaNameTbl[slot 0 - 1], its battle model the character's). A new game's: 6 -
    // the prologue's hooded hero (c101 on the field, j006 in battle; the reference's), the
    // game's own default not found yet
    int cr3Party[5] = {6, 0, 0, 0, 0};
    // CR3's items (Cr3System.ItemGet / ItemCheck): a count by the full id (CrxGameData::AddItem_Sub)
    // - an Item its own id, else category x 100000 + the table's id (1 Weapon, 2 Armor, 3
    // Accessory; 4, 5 others, 6 a gift)
    std::map<int, int> cr3Items;
    // a character's armour where a script put one on (Cr3System.ItemSet / ItemGetAndSet); else
    // the new game's (Cr3BattleTables::defaultArmour)
    std::map<int, int> cr3Armour;
    // and the rest a character wears (CrxCharacter: the weapon's and accessory's table ids, the
    // gene's uid - Cr3Gene::uid): utils::SetItem, Cr3System.GeneSet, the camp's
    std::map<int, int> cr3Weapon, cr3Accessory, cr3GeneOf;
    // a character's MP left from its last battle (cr3_player_data +0 - SaveCharacterMp); none full
    // (Cr3System.MPFullRecovery, a new game)
    std::map<int, int> cr3Mp;
    // My Page's records (MyPageNewTopMenu::SetListLogData): the play time, CrxGameActivities'
    // kills (KillMonster: 999999 at most) and most damage dealt (Damage), and the counts the
    // server kept in the player's data (CrxGameData +0x280c0 battle mode matches, +0x280c4 genes
    // obtained, +0x280c8 gene combinations, +0x280cc gift fusions) - counted here where they happen
    // the team level (utils::Invoke(0) / Invoke2(2, n) - DAT_014c6ab0: every character's level,
    // CrxCharacter::initialize; raised by the lab clerk's c115_TeamLvCheck to the collection count
    // + 1) and the profile value Invoke(4) / Invoke2(5, n) keep
    int cr3TeamLevel = 1, cr3ProfileValue = 0;
    // Port feature: each restart keeps earned progression and starts the story again.
    int cr3NewGamePlus = 0;
    bool canNewGamePlus() const { return flag(6016); } // MainQuestClear(16), after the final battle
    bool makeNewGamePlus(GameState& out, std::string* error = nullptr) const;

    struct Cr3Records {
        double playSeconds = 0;
        int kills = 0, maxDamage = 0, vsMatches = 0, genesObtained = 0, merges = 0, fusions = 0;
    } cr3Records;
    int cr3GeneUid = 0; // (the last gene uid given)
    int geneUid(Cr3Gene& g) { return g.uid > 0 ? g.uid : (g.uid = ++cr3GeneUid); }
    // a character's gene (its index in cr3Genes; -1 none) and a gene's wearer (0 none)
    int wornGene(int ch) {
        const auto it = cr3GeneOf.find(ch);
        if (it == cr3GeneOf.end() || it->second <= 0) return -1;
        for (size_t k = 0; k < cr3Genes.size(); ++k)
            if (cr3Genes[k].uid == it->second) return int(k);
        return -1;
    }
    int geneWearer(int gene) const {
        if (gene < 0 || size_t(gene) >= cr3Genes.size() || cr3Genes[size_t(gene)].uid <= 0) return 0;
        for (const auto& [ch, uid] : cr3GeneOf)
            if (uid == cr3Genes[size_t(gene)].uid) return ch;
        return 0;
    }
    // the offline game database (db/cr3db.hpp: the server's tables and rules, as the Vita plays
    // them - the release's, set by whoever opened it; null: none) and what it keeps in the save:
    // the item sheets' progress by sheet, the genes held, the presents box, the quests
    Cr3OfflineDb* cr3Db = nullptr;
    // the game's data written where the game syncs it (DBSystem::Sync: Cr3System.Sync / Sync2 /
    // SavePoint - utils::Sync, SavePoint::SM_Sync); the player's (null: nothing)
    std::function<void()> onSync;
    std::map<std::string, Cr3SheetProgress> cr3Sheets;
    std::vector<Cr3Gene> cr3Genes;
    std::set<int> cr3GeneBook; // (the genes ever had: utils::CheckNewGene - CrxGameData +0x28198's bits)
    std::set<int> cr3GeneSeen; // (those looked at in the Gene Index - DAT_014d0448's bits: the rest marked new)
    std::vector<Cr3Present> cr3Presents;
    Cr3Quests cr3Quests;
    Cr3Vs cr3Vs;
    int cr3Money = 0; // (Cr3System.GoldSet / GoldGet; battles' winnings - CrxGameData +0x1ebc4)
    // the Oz coins and GP, each with what has been spent of it (CrxGameData +0x1e76c / +0x1e770,
    // +0x1e774 / +0x1e778), the gene stock's room (+0x1e768) - a new game's all 0
    int cr3Coins = 0, cr3CoinsSpent = 0, cr3GP = 0, cr3GPSpent = 0, cr3GeneStock = 0;
    // the day's marks (DAT_014d094c: bit 0 the day's ticket received, 2 the day's quests drawn,
    // 3 the VS daily mission - cleared when the date changes)
    int cr3Daily = 0;
    int cr3VipDays = 0;          // (DAT_01422918: VIP days left - one a day used, its login bonus)
    bool cr3VipTrial = false;    // (DAT_01422920: the trial had)
    // equipment's attachments (CrxItemData +8 / +0xc: EnforceItem - the material's id, its
    // level), by the equipment's full id (the port keeps counts, not each piece)
    std::map<int, std::pair<int, int>> cr3Attachments;
    int64_t cr3LastLogin = 0; // (CrxGameData +0x28698: the last day's login, epoch seconds; 0 none yet)
    int cr3Cheer = 0;         // (the cheer points: DATE_CHANGED_CHEERPOINT_BONUS a day, CHEER_POINT_MAX)
    // the field's leader of the two (cr3_field_data +8: FldUtilGetActiveCharacter, 0 / 1), the
    // character change allowed (+0x32: Cr3ChangeCharacter.SetEnable - its button) and the tab
    // (+0x33: Cr3TabMenu.SetEnable) - a new game's both (Cr3InitFieldData)
    int active = 0;
    bool changeEnable = true, tabEnable = true;
    int partySets[3][2] = {{0, 2}, {1, 3}, {4, 5}};
    // the sopia's levels and EXP (cr3_common_data +0x26c, a byte each, by the sopia's number; +0x28c,
    // 32 integers) - what Cr3Battle.SetParamI2 / GetParamI2 (0 / 1) read and set. A new game's
    // (Cr3InitWorkFromBattleData): each sopia's starting level (initSopia)
    // the items (cr3_common_data +0xc: a count by the item's number, 0..299) and the Oz (+0x264): a
    // new game's from the party's record (initItems)
    short itemCount[300] = {};
    int oz = 0;
    bool itemsReady = false;
    template <class Data> void initItems(const Data& data) {
        if (itemsReady) return;
        itemsReady = true;
        const auto& p = data.party();
        for (int k = 0; k < 5; ++k)
            if (p.items[k] > 0 && p.items[k] < 300) itemCount[p.items[k]] = static_cast<short>(p.counts[k]);
        oz = p.oz;
    }
    // the characters' equipment (cr3_player_data +0x3c, 0x4c bytes a character from 0): weapon,
    // armour, accessory, then the five sopia - a new game's the battle data's (initEquip);
    // Cr3System.SetEquip / GetEquip
    // and their levels and EXP (cr3_player_data +0, +4): a new game's the records' levels, with
    // the EXP of that level (the battle data's kLevelExp, `levelExp`)
    short equip[9][8] = {};
    int level[9] = {}, exp[9] = {};
    // their MP between battles (cr3_player_data +0x10; -1 the most: MPFullRecovery, a new game)
    int mp[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    // what items have added to their parameters for good (cr3_player_data +0x1e..: the seeds,
    // Cr3UtilParameterUpByItem)
    short seed[9][5] = {};
    // cr3_global_work (Cr3UtilGlobalWorkGet / Set: 100 values - the scripts' GetVar / SetVar, 24 the
    // quest set off on, ..)
    static constexpr int kGlobalWork = 100;
    int globalWork[kGlobalWork] = {};
    // the difficulty (cr3_common_data[0]: Cr3UtilSetDifficulty, the world select's level - 1) and the
    // dungeon's treasure boxes (cr3_treasureBox)
    int difficulty = 0;
    // the dungeon the world select chose last (FldWorldSelect::m_nSelectDungeonNum: GetSelectDungeonNum)
    int selectDungeon = 0;
    int selectLevel = 0; // (CR3's world select: InterfaceMain +0xa4 - the level's index; GetSelectDungeonLevel)
    int selectChapter = -1; // (the chapter select's pick: InterfaceWindow.GetChapterID - not saved)
    // the library's shelf (InterfaceWindow.SetArchiveID: InterfaceMain +0xd8, its category) and the
    // entry being read (GetLibraryID: ArchiveWindowMenu +0x2c) - not saved
    int libraryArchive = 0, libraryId = 0;
    // the frame's clear colour (Cr3System.SetBGColor - Cr3UtilSetBGColor, GameMain::Draw's) - not saved
    float bgColor[3] = {0, 0, 0};
    // the area last entered (FldUtil's: GetDungeonNum - 0 the town - and GetAreaNum, from 0)
    int dungeon = 0, area = 0;
    // where the field is next entered (Cr3System.SetPopPoint2 - Fld2SetPopPoint2: the pop point
    // kept, no change reserved; Fld2Main::SM_Init / SM_ChangeArea take it - the prologue's end,
    // t0201_p3) - not saved
    std::string popPoint2;
    // the field's gimmicks shown or hidden (Cr3GimmickData.Disp - Fld2SetGimmickVisible: the scripts'
    // ModelDisp_Field; an area's people, its objects) by name ("c111_00"); none listed: shown
    std::map<std::string, bool> gimmickShown;
    // the field's boxes opened (Cr3GimmickData.BoxOpen - Fld2OpenTreasureBox: 1) and then let go
    // (DeleteBox - Fld2DeleteTrasureBox: 2, faded out), by name ("kbox_00") - not saved
    std::map<std::string, int> fieldBoxes;
    // the names entered ("name", "team", "pet", "food" -> text: utils::SetDefaultName /
    // SetDefaultTeamName - "プレイヤー", "ケイオスチーム" in every release and language -, the pet's
    // and food's empty till entered; Cr3System.Set...FromInputDialogText) - CrxGameData's, saved
    std::map<std::string, std::string> cr3Names{{"name", "\u30d7\u30ec\u30a4\u30e4\u30fc"},
                                                {"team", "\u30b1\u30a4\u30aa\u30b9\u30c1\u30fc\u30e0"},
                                                {"pet", ""},
                                                {"food", ""}};
    // the dungeon's treasure and gold boxes (Fld2TBoxList: drawn as the field is set up in a dungeon -
    // TBoxTable's kinds for its level, TBoxItemTable's contents; the gold boxes by GBOX_PROBS' chances,
    // their prize the dungeon's item sheet's as one is opened), the one being opened (SetOpenBox:
    // GetOpenBoxName / GetItemType / GetItemID), a box's battle to start (TBoxBattleIn's coupling) and
    // a gold box's sheet to show (OpenGBox(true) - Fld2Main::SM_OpenGBox) - not saved
    struct Cr3Box {
        int area = 0;     // (from 0)
        std::string name; // ("tbox_00")
        int kind = 0, type = 0, category = -1, id = -1; // (type 0 a treasure box, 1 a gold box)
        bool opened = false;
    };
    std::vector<Cr3Box> cr3Boxes;
    int cr3OpenBox = -1, cr3BoxBattle = -1;
    bool cr3GBoxShow = false;
    std::set<std::string> switchesOn; // (Cr3GimmickData.SwitchOn: the bomb switches - Fld2PhysicalSwitch - turned on, the step switches - Fld2StepSwitch - down)
    // Cr3GimmickData.SetMove(name, end, frames) (Fld2SetMovingObjectMove): a moving object's motion
    // to start - its area's <area>p animation from 0 to that frame over those frames; the field takes them
    std::map<std::string, std::array<float, 2>> moveRequests;
    // the field's effects (Cr3FieldMapEffectResident.EffectResidentPlay / PlayOnce - Fld2TaskGimmick::
    // CreateEffect: a model - the scripts' SetFlagMark fe007poin02 - at a gimmick's place, "player"
    // the player's, + the offset, scaled; looped, or once; EffectResidentOut - DeleteEffect: the
    // target's gone) - not saved, an area's own; age: its clock, the viewer's. The environment's too
    // (EnvironmentEffectPlay - Fld2TaskGimmick::CreateEnvironmentEffect: the model where it is,
    // target ""; EnvironmentPlayerPlay - Fld2SetEnvironmentEffectPlayer: an attachment of the
    // player's model on a joint - RootLocator -, target "player" + the offset, not turned with it).
    // serial: its own (the viewer's particles - a .sprk's - by it)
    struct FieldEffect {
        std::string model, target;
        std::array<float, 3> scale{1, 1, 1}, offset{};
        bool loop = true;
        float age = 0;
        uint32_t serial = 0;
    };
    std::vector<FieldEffect> fieldEffects;
    uint32_t fieldEffectSerial = 0;
    // a movie asked for (Cr3System.PlayMovie(name) - MovieControlTask::Play: "c08"); the viewer
    // plays it and clears moviePlaying at its end (IsPlayingMovie: the scripts' WaitMovie)
    std::string movieRequest;
    bool moviePlaying = false;
    // the field's goods buttons (Cr3TabMenu.AddGoodsButton / DelGoodsButton - Fld2GoodsButton::
    // SetButtonEnableOne: 0 the bomb, 1 the toy camera, 2 the detector, 3 the darts; the areas'
    // MenuTabAddALL puts them back by flags 4101 ..) - not saved
    std::set<int> goodsButtons;
    // the rocks broken (Fld2Rock: by a bomb's blast) this visit to a dungeon - "<area>:<gimmick>";
    // a new dungeon's areas have them back - not saved
    std::set<std::string> brokenRocks;
    bool gimmickVisible(const std::string& name) const {
        const auto it = gimmickShown.find(name);
        return it == gimmickShown.end() || it->second;
    }
    // the scripts' encounters switch (Cr3System.SetEncountEnable: cr3_field_data +0x30 - the tab's own is +0x31)
    bool encountEnable = true;
    TreasureBoxes boxes;
    bool equipReady = false;
    template <class Data> void initEquip(const Data& data, const int* levelExp) {
        if (equipReady) return;
        equipReady = true;
        for (int c = 0; c < 9; ++c)
            if (const auto* ch = data.character(c + 1)) {
                for (int e = 0; e < 8; ++e) equip[c][e] = static_cast<short>(ch->equip[e]);
                level[c] = ch->level, exp[c] = ch->level >= 1 && ch->level <= 200 ? levelExp[ch->level - 1] : 0;
            }
    }
    signed char sopiaLevel[32] = {};
    int sopiaExp[32] = {};
    // which are had (cr3_common_data +0x268, a bit by number: Cr3System.SopiaGet / SopiaCheck) - a
    // new game's Darwin's (1); a new game's EXP that of the starting level (its growth's table,
    // `expAt`: battle/battledata.hpp's sopiaExp)
    uint32_t sopiaOwned = 0;
    bool sopiaReady = false;
    template <class Data> void initSopia(const Data& data, int (*expAt)(bool, int) = nullptr) {
        if (sopiaReady) return;
        sopiaReady = true;
        for (int k = 0; k < 32; ++k) {
            const auto* s = k < data.sopias() ? data.sopia(k) : nullptr;
            sopiaLevel[k] = static_cast<signed char>(s ? s->startLevel : 1);
            sopiaExp[k] = s && expAt ? expAt(s->growth != 0, s->startLevel) : 0;
        }
        sopiaOwned = 1u << 1;
    }
    // CR3-U's own save (not the game's format - cr3_savedata_game): "CR3-U save 1", then a line a
    // value ("flags 3 17 ...", "item 1 5", "equip 0 81 201 ..."); `extra` the caller's own lines
    // (the map, the place) - load gives back the lines it doesn't know there
    std::string save(const std::map<std::string, std::string>& extra = {}) const;
    bool load(const std::string& text, std::map<std::string, std::string>* extra = nullptr, std::string* error = nullptr);
    bool parse(const std::string& text, std::map<std::string, std::string>* extra, std::string* error); // (load's: over this copy)
    bool flag(int n) const { return n >= 0 && size_t(n) < flags.size() && flags[size_t(n)]; }
    void setFlag(int n, bool on) {
        if (n < 0) return;
        if (size_t(n) >= flags.size()) flags.resize(size_t(n) + 1);
        flags[size_t(n)] = on;
    }

    // What the scripts reserved (FldUtil's work): a vista event (SetVistaFuncName /
    // SetVistaMapName), a battle (BattleIN, the pair SetBattleParty set), an area to go to
    // (SetPopPoint: the pop point, its map its first five letters), the title; and how the field
    // is entered next (FldMain::Init's mode: SetComeFromBattleToField 1 - evt100, ...FromVista...
    // 2 - evt200; else evt000)
    struct Next {
        bool vista = false;
        std::string vistaFunction, vistaMap = "null";
        int battle = -1;
        int battleParty[2] = {-1, -1};
        // (CR3's: FldUtilSetBattleParty - DAT_0141e298's three slots; with the first not -1 the
        // battle's party is these, -1 none - BtlBootParam +0 .. +8 -, the party's own otherwise)
        int cr3BattleParty[3] = {-1, -1, -1};
        // (CR3's BattleIn(coupling, field, level, music) / BattleIn2(.., the music after):
        // FldUtilReserveBattleIN - BtlBootParam +0x14, +0x10 the battle field (battle_field.mbe),
        // +0x18 the monsters' level, +0x1c the music; the scripts' wrappers name them cup, map,
        // lv, bgm (passing map second))
        int battleField = -1, battleLevel = 1, battleMusic = -1, battleMusicAfter = 32;
        // (an encounter's: BtlBootParam +0x1c, +0x20 - the party's first-strike chance, the enemy's
        // surprise attack's, %; a script's battle none)
        int battleFirstStrike[2] = {0, 0};
        // (after it: SetBattleNext - FldUtil's DAT_0141e2e0: 1 the field, 14 the vista
        // SetVistaFuncName2 named - its function kept without a vista reserved now, Cr3ScriptWork 0x11)
        int battleNext = 1;
        std::string popPoint;
        bool title = false;
        int fieldMode = -1; // -1: evt000
        bool worldSelect = false; // (ReserveWorldSelect: cr3_script_work[4] - FldMain step 10)
        bool shop = false;        // (ReserveShop - FldMain step 0xb)
        bool party = false;       // (ReserveParty - FldMain step 0xc)
        bool chapter = false;     // (ReserveChapter - FldMain step 0xe)
        bool library = false;     // (ReserveLibrary - FldMain step 0xf)
        bool camp = false;        // (ReserveCamp - FldMain step 5)
        bool pub = false;         // (ReservePUB - FldMain step 0xd)
        int changeDungeon = -1, changeArea = -1; // (ChangeDungeon(d, a) / ChangeArea(a): FldUtilReserveChange*)
        int credit = -1;          // (ReserveCredit(n): cr3_script_work[0xc] / [0x14] - VistaMain::BranchAfterVista 3, the staff roll)
        // (LunchEvent.SetFuncName / SetMapName - utils::SetLunchFuncName: the vista's function and
        // map, reserved as a lunch, Cr3ScriptWork 0xf - VistaMain::BranchAfterVista 0xf, LunchMain)
        bool lunch = false;
        bool townMove = false; // (ReserveTownMove - Fld2ReserveTownMove: Cr3ScriptWork 0xe, Fld2Main::SM_TownMove)
    } next;
};

// the natives on the story's state (Cr3Flag, Cr3Work) and the reservations (Cr3System's
// SetVistaFuncName / SetVistaMapName, BattleIN / SetBattleParty, SetPopPoint, SetComeFrom*), for
// any script
void bindGameState(ScriptVM& vm, GameState& state);

} // namespace cr3
