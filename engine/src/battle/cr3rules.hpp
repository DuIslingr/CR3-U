#pragma once
// CR3's battle rules (docs/FORMATS.md "Battles (CR3)"): the fighters' numbers, the commands, a
// turn's actions and their damage.
//
// The numbers: a character's (char_para: MbCharInfo) and a monster's (mon_para: MbMonsterInfo)
// base + (level - 1) x growth / 100 - HP, MP, attack, defence, speed, luck. A command
// (command_para Command, MbCommandInfo): its power (+0x14), hits (+0x21; below 0 the unit's own),
// its damage formula (+0xc0 -> CommandFomula: 1 physical, 2 magic), its spread (+0xa8, a
// percentage either way: calculateHpFluctuation), its multiplier (+0x18); its motion
// (direction.mbe CommandDirection +8, an AnimCommon type). A monster's commands (MonsterCommand:
// eight (command, action cost, uses)) are picked by the enemy AI (BtlStatusList::ExecuteAIForEnemy):
// its side's think points by select type (command +0xc8), the command's select priority (+0xcc)
// and a preferred target (BtlStatus::IsSelected) - Cr3BattleRules::turn.
//
// A character's equipment (CrxCharacter::initialize): a guest's - char_para +0x0c bit 0 - from its
// row, +0x10 its weapon (item_para Weapon: +0x38 the constant, +0x3c the hits, +0x44 the MP
// consumption %, +0x48 / +0x4c / +0x50.. lowered physical / magic / element resistances), +0x14 its
// armour (Armor: +0x44 physical, +0x48 magic, +0x4c.. element resistances), +0x18 its accessory
// (Accessory: +0x30 HP .. +0x44 luck added), +0x1c its gene (GuestGene: +4 the gene, +8 its level,
// then (type, id) pairs from +0x0c - 0 a command, 1 a skill); anyone else's from the save (not kept
// yet: CR3-U gives them the same row's). Its attack etc.: its own at its level + the gene's at the
// gene's level (gene_para Gene: +0x34 HP, +0x38 MP, +0x3c.. s16 attack, defence, speed, luck, then
// their growth) + the accessory's. The prologue hero: weapon 501 (constant 90, 1 hit, 200%), armour
// 202 (20% physical), accessory 100 (+1 defence), GuestGene 1 (gene 453 at level 50).
//
// Damage (BtlCalculator::calculateBasicDamage), a hit's: clamp(attack / defence, 0.2, 5) x
// (power + 0) x 0.5 (power below 0: VGetWeaponConstant - a character's weapon's, a monster's attack
// command's), x 1.5 with the break gauge's advantage ("Status"), x the element's rate ("Element"),
// x (r + 100) / 100 ("Resistance": r = the attacker's lowered element resistance - (the target's
// physical or magic resistance + its element resistance) + the attacker's lowered physical or magic
// one, at least -100 - each its skills' (kinds 1 / 4 resistance, 2 / 5 lowered, 3 + 4e / 4 + 4e an
// element's) and its equipment's), x (gene level / 20 + 1) ("Level": a character's gene's, a
// monster's own), x the command's multiplier, then (int) and the spread
// (calculateHpFluctuation: x ((random - 0.5) x spread x 2 + 100) / 100); at least 1. "Status" is
// 1.5 with the gauge's advantage, + 0.5 a critical (calculateCriticalHit: luck against luck), + 0.1
// a down (calculateDown: a critical, an element rate above 1, or a target already down - its combo
// chance, kept while one side acts). A hit may miss (calculateHit: speed against speed); status
// effects (skill_para StatusEffect) scale the stats and the damage, stop a unit acting, forbid
// commands and change HP each turn; a downed unit's command waits. The break gauge, the status
// effects' kinds and the misses: docs/FORMATS.md "Battles (CR3)". Not in yet: the rarer
// multipliers (Damage Multi, Team Play, Target Skill). Cover (skill kind 118) in turn(). The command change
// (status kind 120): Cr3BattleRules::commandChange. The status effects' triggers and end timings (BtlStatusEffect::Trigger /
// End): Cr3BattleRules::triggerEffects / endEffects; a skill's fixed damage (kind 91) and blocks (3 / 6
// / an element's, 89 piercing) in turn(); the assists - calculateAssist - after a turn's commands.
//
// A turn (BtlCommandManager::Calculate: a command list per side, in order): the party's chosen
// commands, then each living monster's; a dead target's attack goes to another living one. A
// cooperation (calculateCooperation): a command that allows it (+0xbc bit 7), with a chance -
// a random byte under 154 with the gauge's advantage, else 77, + the unit's skills' kind 80 -
// takes in every later command of its side's that is the same (on the same target where it takes
// one enemy): one action, each unit's hits in it, each damage x battle.mbe's scale for the units
// in it (1, 3, 5).

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "battle/cr3battle.hpp"

namespace cr3 {

struct Cr3Command {
    int id = -1;
    int icon = -1; // (+0x08: its list icon - icon_panel_NN's frame, BtlCommandListUI::Add 0x9a13c4)
    int power = 0, hits = 1, formula = 1, spread = 0;
    bool hitsOwn = false; // (+0x21 below 0: the unit's own)
    int repeat = 1;       // (+0x20: its attacks, each of its hits - Rakshasa Lotus 3 x the weapon's 1)
    int breakPoint = -1;  // (+0x22, s16: its break points a hit - below 0 none; with flag bit 1 the unit's weapon's instead)
    uint32_t flags = 0;   // (+0xbc: bit 0 a gene's, 1 the weapon's break, 2 always hits, 3 its status effects always land, 4 can be critical, 5 always critical, 6 always downs,
                          //  7 can be cooperated in; 16 - +0xbe bit 0 - never on the Auto Battle: noAuto)
    bool noAuto = false;  // (+0xbe bit 0: BtlAutomaticCommandList::EnumerateCommand 0x970edc passes it over)
    float hitRate = 1;    // (+0x1c: x the hit chance)
    std::vector<int> effects; // (+0x40 / +0x48: the status effects it gives - Guard's 2)
    std::vector<int> removes; // (+0x50 / +0x58: the status effects it takes away - MbCommandInfo::HaveRemovesStatusEffect; battle 7's release 1844)
    std::vector<int> requiredEffects;  // (+0x80 / +0x88: required target status effects - Revive's 1 KO)
    std::vector<int> forbiddenEffects; // (+0x90 / +0x98: prohibited target status effects)
    int effectsAnnounce = -1; // (+0xe8: AnnounceBattle's line when it gives one target several)
    int type = 0;         // (+0x04: 0 an attack, 1 a physical skill, 2 magic, 3 support, 4 special, 7 a guard)
    int action = 0;       // (CommandFomula +0x04: 14 calculateActionNumBySpeed)
    int damage = -1;      // (its CommandFomula row's damage: 1 physical, 2 magic, 5..7/15..18 heals, 8..24 special)
    int target = 3;       // (+0x10: BtlStatusList's target kind - 0 itself, 1 an ally, 2 all allies, 3 an enemy,
                          //  4 all enemies, 5 everyone, 6 a random enemy, ...)
    bool all = false;     // (target kind 2, 4, 5, 9, 11 or 13: every one on that side)
    int mpCost = 0, hpCost = 0; // (+0xb8, +0xb0: raw cost values; Cr3BattleRules::mpCost / hpCost compute the real cost)
    int hpKind = 0;             // (+0xac: 0 flat, 1 % current HP, 2 % max HP, 3 x weapon consumption)
    int mpKind = 0;             // (+0xb4: 0 flat, 1 % current MP, 2 % max MP, 3 x weapon consumption)
    bool revives() const {
        for (int e : removes)
            if (e == 1) return true;
        return false;
    }
    bool mpHeal() const { return damage == 16 || damage == 17; }
    bool ally() const { return damage == 5 || damage == 6 || damage == 7 || damage == 15 || damage == 16 || damage == 17 || damage == 18; }
    bool usable() const {
        return (damage < 0 && (!effects.empty() || !removes.empty())) || damage == 1 || damage == 2 || ally() || damage == 8 ||
               damage == 10 || damage == 11 || damage == 12 || damage == 13 || damage == 20 || damage == 21 || damage == 22 ||
               damage == 23 || damage == 24 || direction == 0x16; // (0x16: BtlAnalyzeDirection - Analyze, command 393, no formula)
    }
    float multiplier = 1;
    int priority = 0;   // (+0xa0: BtlCommandManager bucket / BtlCommandList::Sort primary order; -8 / -9 delayed buckets)
    int speedCost = 0;  // (+0xa4: MbCommandInfo::GetCost, added to speed - weight in BtlCommandList::Update)
    int chain = 0;      // (CommandFomula +0x0c: 3 physical chain bonus - calculatePhisicalChainBonus)
    int element = 0;    // (+0x24: its attack element - the Element table's row)
    int extraType = 0;  // (+0xd4: MbCommandInfo::GetExtraType - 1/2 random subcommand, 13 force break, 18 status power)
    std::vector<int> extraConstants; // (+0xd8 / +0xe0: MbCommandInfo::GetExtraConstantByIndex)
    // (+0x30 / +0x38: its first chance out of 256 - CalculateStatusEffect 0x9844a8's for extraType 0x11, a call for
    // help: 1742's 180, the others' 256; 256 where it has none)
    int effectChance = 256;
    // the enemy AI's (BtlStatusList::Select / getCommandTarget / BtlStatus::IsSelected)
    int selectType = 0;     // (+0xc8: 0 attack, 1 physical skill, 2 magic, 3 heal, 4 cure, 5 buff, 6 debuff, 7 special)
    int selectPriority = 0; // (+0xcc: its weight - BtlPriorityCommandIdList::sortBySelectPriority)
    int targetOrder = 0;    // (+0xd0: BtlTargetList::Collect's order - 0 shuffled, 1 / 2 higher / lower HP, 3 / 4 HP rate,
                            //  5 / 6 level, 7 as collected)
    int changeElement = -1; // (+0x28: the element it changes the target to - a target already of it is not preferred)
    int elementC4 = 0;      // (+0xc4: 1 - an element-less one takes the actor's own under skill kind 75)
    int motion = -1;    // (an AnimCommon type: 10 bi01, 15 bs01, ...; -1 none)
    int direction = -1; // (CommandDirection +4: its kind)
};

// a monster's mode's command (MonsterCommand +0x20 + 12k: MbParamCommandSetting - the ids of -1
// skipped; rebuilt, uses full, by every VChangeMode)
struct Cr3MonsterCommand {
    int command = -1;
    int cost = 1;  // (+4: the actions it takes of the turn's - BtlStatus +0x1dc)
    int uses = -1; // (+8: -1 unlimited; BtlMonsterStatus::VDecreaseCount 0x009c6ec4 - one less a use)
};

struct Cr3Fighter {
    int unit = -1; // (Cr3Battle::units' index)
    bool player = false;
    int id = -1;   // (char_para / mon_para's)
    int level = 1;
    int hp = 0, maxHp = 0, mp = 0, maxMp = 0;
    int attack = 1, defence = 1, speed = 1, luck = 0;
    int element = 0;   // (a monster's +0x18: BtlStatus::initializeElement)
    // equipment (a character's; 0 none): the weapon's constant, hits, MP consumption factor and
    // lowered resistances; the armour's resistances (index 0 .. 6 an element's)
    int weaponConstant = 0, weaponHits = 0, weaponBreak = 0; // (Weapon +0x40 the break constant; a monster's: its attack command's +0x22)
    float consumption = 1;
    int lowerPhysical = 0, lowerMagic = 0, physical = 0, magic = 0;
    std::array<int, 7> lowerElement{}, elementResist{};
    int weight = 0; // (CrxCharacter::GetWeight: the weapon's +0x34 and the armour's +0x40, each where above 0)
    std::vector<int> skills; // (skill_para Skill ids: a guest gene's, a monster's mode's)
    // its skills' state (BtlSkill, 0x18 bytes: +0x14 enabled - BtlSkill::Trigger 0x9c60a0 sets it, End 0x9c6504 clears it at
    // the row's end timing +0x28; +8 / +0xc / +0x10 the uses a battle / turn / command - Initialize 0x9c604c copies the row's
    // +0x10 / +0x14 / +0x18, -1 unlimited, ResetCountPerTurn 0x9c6078 / ResetCountPerCommand 0x9c608c restore the last two),
    // by skill id; a skill of no trigger and no condition always counts as enabled (trigger 0 at BtlStatus::Initialize)
    std::set<int> skillsOn;
    std::map<int, std::array<int, 3>> skillUses;
    int attackCommand = -1;                     // (a character's +0x10; a monster's mode's +0x14)
    std::vector<int> geneCommands;              // (a character's gene's: a guest's GuestGene row)
    int geneMaster = -1;                        // (the equipped gene's master ID for the battle card)
    int geneOrder = 0; // (the worn gene's MbGeneInfo +0x54 - BtlStatus::GetOrderType 0x9b7290: battle.mbe OrderType's row, its guard chance)
    int geneLevel = 0;                          // (VGetGeneLevel: the "Level" step's - a character's gene's, a monster's level)
    std::vector<Cr3MonsterCommand> commands;   // (a monster's mode's)
    int actionsPerTurn = 1; // (vGetResetRatio: a monster's MonsterCommand +0x10 - MbMonsterInfo::GetRatio -, a character's 1)
    int actionsLeft = 0;    // (BtlStatus +0x1dc: this turn's - BtlStatusList::ResetRatio 0x009c1c50 at the turn's start)
    int card = -1;          // (a monster's MonsterCommand +0x0c: BtlMonsterStatus::VGetEquipedGeneId 0x009c73e0)
    int forcedBy = -1;      // (the unit whose status effect of kind 52 - a provoke - it bears: GetForceTarget's)
    // its status effects (BtlStatusEffectList: skill_para StatusEffect ids, the turns left - -1
    // until removed) and per-effect accumulation count (col 7 maxAccumulation)
    std::vector<std::pair<int, int>> effects;
    std::map<int, int> effectStacks;
    // (BtlStatusEffect +4 / +0x10: each effect's uses left - StatusEffect +0x10, -1 unlimited - and the
    // ones not enabled - a triggered one, StatusEffect +0x20 not 0, dormant until BtlStatusEffect::Trigger
    // 0x9c7ad8 enables it; BtlStatusEffectVisitor::Visit 0x9b9e3c reads only the enabled), by id
    std::map<int, int> effectUses;
    std::set<int> effectsDormant;
    std::array<int, 6> effectResist{}; // (a monster's resistances to battle.mbe's listed effects)
    bool comboChance = false; // (BtlBasicStatus +0x6d: downed - every later hit on it downs too, until the acting side changes)
    int combo = 0;            // (its combo count: +1 a downing hit; the magic chain bonus's)
    int chain = 0;            // (BtlBasicStatus +0x80: +1 each landed hit; reset on side change; calculatePhisicalChainBonus)
    int mode = 0;             // (BtlMonsterStatus +0x1fc: its mode - VChangeMode's; a character's VGetMode 0)
    // a monster's finishers (MbMonsterInfo::GetLastAttackCommandId / GetFinestArtsCommandId 0x00913b98 /
    // 0x00913ba8: its mode's MonsterCommand row +0x18 / +0x1c; -1 none)
    int lastAttack = -1, finestArts = -1;
    // (a monster's Monster +0x20 MP below 0 - Death Mask 554's -1: BtlCalculator::HaveEnoughMp 0x7e2828 passes a unit
    // whose MP is below 0, nothing spent; its MP here 0 of 0)
    bool unlimitedMp = false;
    int stolenGold = 0; // (BtlStatus +0x1e0 - AddGold 0x9b63b0: what its Steal Gold hits took from the party)
    bool dead() const { return hp <= 0; }
};

struct Cr3Hit {
    int target = -1;
    std::vector<int> hits; // each hit's HP delta (a heal's: below 0)
    std::vector<int> mpHits; // each hit's MP delta (an MP heal's: below 0; MP drain/damage: above 0)
    std::vector<uint8_t> flags; // each hit's: bit 0 critical, bit 1 down, bit 2 a miss, bit 3 blocked (calculateFluctuation 0x7e122c: +0xa7 / +0xa8)
    std::vector<uint8_t> colors; // BtlDamageUI::getDamageColor: 0 normal, 1 weak, 2 resist, 3 HP heal, 4 MP heal
    std::vector<int> gauge; // the break gauge after each hit
    std::vector<int> effectsGiven; // the status effects it gave (StatusEffect ids)
    // (BtlDamageTarget::Calculate 0x983a44: the target's enabled kind 49 - Mental Wall - turned the
    // command back, +0x110 bit 1: target is then the caster, this the unit whose wall it was; -1 none)
    int reflectedFrom = -1;
    // (BtlDamageTarget::Calculate 0x983a44 before trigger 4 - BtlSkillUtility::HasCover 0x9b0cac: a unit's skill kind 118 of
    // this command took the hit - target the coverer, this the unit it covered, +0x18; -1 none: BtlShotCoverDirection)
    int coveredFrom = -1;
    int hpAfter = 0, mpAfter = 0;
    bool killed = false;
    // (calculateStealGold 0x7e20e8 - formula 21, command 1647: BtlDamage +0x72 / +0x1c) -1 not a
    // steal; 0 the party broke (AnnounceBattle 0x29); else the money taken (0x28)
    int stolen = -1;
    // (an HP / MP steal's - +0xbd & 0x60, flags 0x2000 / 0x4000 -: BtlDamageTargetList::Calculate 0x97f81c's last
    // BtlDamageTarget, the source's on itself - SetStealDamage 0x988eb4: hit k the targets' landed hit-k HP / MP damage
    // summed, a recovery; not a target's: no break points, chain, combo or total)
    bool stealBack = false;
};
struct Cr3Action {
    int source = -1, target = -1, command = -1; // (fighters' indices - the first target; command -1: a guard, -2: unable to act)
    std::vector<int> hits;                      // (the first target's, as Cr3Hit's)
    int hpAfter = 0;
    bool killed = false;
    std::vector<Cr3Hit> targets;                // every target's (one but for an all)
    int mpCost = 0;
    int gaugeAfter = 0;                         // the break gauge after it
    std::vector<int> partners;                  // (a cooperation's others - BtlDamageChunk::AddCooperation -, their hits among the targets')
    // (BtlDamageChunk::VUpdate's target-list events: 2 - +0x9e, IsCombo - a landed hit on a target
    // already downed - BtlDamageTarget::notifyCombo, HaveComboChanceById -; 3 - +0xa8, WasBreak -
    // its break points broke the gauge - BtlBreakPoint::Add's +0x31, the jump across 0)
    bool combo = false, broke = false;
    bool finisher = false; // (BtlCommandManager::addLastAttack's: a Last Attack or a Finest Arts, played as its command's own)
    bool cooperated() const { return !partners.empty(); } // (BtlDamageChunk::IsCooperation)
    // (source -1: a command with no actor - an assist, BtlCommandManager::calculateAssist 0x7dbbf4, or a
    // status effect's kind 98 / 101 one, AddNoSourceInterruptCommand / AddNoSourceCommand: Regenerate's 306)
    bool assist = false; // (calculateAssist's: its direction BtlAssistDirection, the announcement's {assistant})
    std::vector<std::vector<int>> effectsAfter; // (every fighter's status effects once it is calculated - Cr3BattleRules::snapshotAfter)
    std::vector<int> elementsAfter;             // (every fighter's element then - ChangeElementToCalculation's)
    // (a field toggle - extraType 0xb, Reverse Gravity 387: BtlFieldEffectList::Add's type 0 - 1 opened, -1 closed; 0 none)
    int fieldToggle = 0;
    // (a call for help - extraType 0x11, action 37: the fallen slot it aimed at, the monster now in it - -1 none -, and
    // whether its roll came up - +0x110 bit 4: AnnounceBattle 92, else 91)
    struct Reinforced {
        int fighter = -1, monster = -1;
        bool success = false;
    } reinforced;
};

class Cr3BattleRules {
public:
    // a party member's gene (the save's: what it wears - GameState::cr3GeneOf), by char_para id;
    // set before setup - a guest's GuestGene row comes first
    struct PartyGene {
        int master = 0, level = 1;
        std::vector<std::array<int, 2>> slots; // (type 0 a command, 1 a skill; id)
        int adds[4] = {};                      // (attack, defence, speed, luck added by items)
    };
    std::map<int, PartyGene> partyGenes;
    // the party's MP from the battle before (InterfaceMain::SaveCharacterMp: what is left carries
    // over - items, the camp and the VS mode's recovery put it back), by char_para id; none - full
    std::map<int, int> partyMpLeft;
    // what each wears (the save's: weapon, armour, accessory table ids - -1 the char_para row's, 0
    // none), by char_para id; a guest its row's always
    std::map<int, std::array<int, 3>> partyEquip;
    // the equipment's attachments (CrxWeapon / CrxArmor / CrxAccessory +0x40: the attached item's
    // info, its constant added to theirs), by the equipment's full id: the item attached
    std::map<int, int> attachments;
    int teamLevel = 1; // (every character's level: CrxCharacter::initialize's DAT_014c6ab0)
    // the characters' finisher tiers (CrxCharacterData +0x14 / +0x18 - Cr3System.SetLastAttackLevel /
    // SetFinestArtsLevel; 0 to begin with - CrxGameData::Init), by the Char row's +0x08 (its save slot)
    std::map<int, int> lastAttackLevel, finestArtsLevel;
    // a fighter's finisher: BtlStatus::GetLastAttackCommandId 0x009b722c (status effect 0x3f none, 0x40
    // command 18; a character's CrxCharacter::GetLastAttackCommandId 0x00931b20 - its Char row's
    // LastAttack rows of a level and gene value no higher, picked by weight -, a monster's row's) and
    // GetFinestArtsCommandId (0x00931d7c: the FinestArts rows allowed, the highest level's, ties at
    // random; a character's none under 0x3f / 0x40); -1 none
    int lastAttackCommand(int fighter);
    // a skill enabled (BtlSkill::IsEnable 0x9c6558 - what BtlSkillVisitor 0x9b8cc4 visits: every reader of a skill's kinds)
    bool skillOn(const Cr3Fighter& f, int skill) const;
    // the device clock's hour for skill conditions 24 / 25 (day 6 .. 17 - Date::set_epoch_time_msec at GameSystem +0x15c's
    // time zone: skills 113 / 114); -1 the local time's, a test's own else
    int localHour = -1;
    // the attack command (BtlStatus::GetAttackCommandId 0x009b57ec / VGetAttackCommandId 0x009b294c): under a
    // transformation's effect kind 63 command 8, 64 - the pig form - command 9, else its own
    int attackCommandOf(int fighter) const;
    int finestArtsCommand(int fighter);
    bool playerStats(const Cr3BattleTables& tables, int character, Cr3Fighter& out) const;
    // the VS mode's tallies (BtlStatus::TakeDamageToDirection: the party's damage to the monsters -
    // the battle's score, DAT_014a7d88, 9999999 at most; ConsumeMpToDirection: the party's MP used -
    // the clear bonus's percentage; GetNumberOfKilledNonPlayer)
    long partyDamage = 0;
    int partyMp = 0, killed = 0;
    bool setup(const Cr3BattleTables& tables, const Cr3Battle& battle, int monsterLevel, uint32_t seed, std::string* error = nullptr);
    // a monster's fighter (BtlMonsterStatus::vInitialize 0x9c74b4): its Monster row's numbers at its own level (+0x14),
    // else levelIfNone, full HP and MP, mode 0's MonsterCommand row (initializeCommandSkill); false: no such monster
    bool monsterFighter(int monsterId, int levelIfNone, Cr3Fighter& out);
    // (VRainforceToCalculation 0x9c6ef0's VAcquireExperience / VAcquireMoney / VAcquireDrop: the fallen a call for help
    // replaced, banked as they were - their EXP and money for the result, with the dead at the end; the viewer takes them)
    std::vector<Cr3Fighter> banked;
    // BtlStatus::IsDefeat 0x9b6b54: dead, or an enabled kind 33 unless kind 128 (the Mimics' escape - status effect 51)
    bool defeated(const Cr3Fighter& f) const;
    bool command(int id, Cr3Command& out) const;
    // a turn: the party's choices (a fighter's index -> command (-1 a guard), target), then the
    // monsters' own; returns the actions in order, applied
    std::vector<Cr3Action> turn(const std::vector<std::array<int, 3>>& choices);
    // BattleStatus.RequestModeChange(unit, mode) (BtlStatusList::RequestModeChange): queued, and
    // applied (ChangeMode, in phaseStatusEffect - before the next turn's commands) by changeMode
    std::vector<std::pair<int, int>> modeRequests; // (fighter, mode)
    // BtlMonsterStatus::VChangeMode: a monster's mode's MonsterCommand row - its attack command,
    // commands and skills (initializeCommandSkill) and element (MbMonsterInfo::GetModeElement:
    // the row's +8)
    void changeMode(int fighter, int mode);
    // a hit's damage; crit / down: whether it was a critical hit, whether it downed the target
    int damage(const Cr3Fighter& source, const Cr3Fighter& target, const Cr3Command& c, bool* crit = nullptr, bool* down = nullptr);
    // the break gauge (BtlBreakGaugeUI: -1000 .. 1000, above 0 the party's advantage, below the
    // monsters'; BtlBreakPoint::Add - the battle's first points jump to +-500, crossing 0 adds
    // another -+500, a break)
    int gauge = 0;
    bool gaugeStarted = false;
    bool advantage(bool player) const { return player ? gauge > 0 : gauge < 0; }
    bool addBreak(int points); // (true: a break - BtlBreakPoint::Add's +0x31)
    // (the party's first strike - BtlStatusList::ClearRatioToEnemy: the monsters' actions this turn
    // none - the next turn() the party's alone)
    bool monstersWait = false;
    bool bootCharacter = false; // (MbCouplingInfo +0x44 bit 3: IsBootCharacter, 10x calculateBasicDamage)
    // (BtlFieldEffectList's type 0 - Reverse Gravity's, extraType 0xb: a toggle, 5 turns from its cast - the rest of
    // that turn and four more -, stepped at each turn's end, before the assist; on, BtlCommandList::Sort's slowest first
    // in lists 0 and 2. Its end a command 58 with no actor - BtlGravityInversionDirection)
    bool reverseSpeed = false;
    int reverseSpeedTurns = 0;
    bool partyUnhurt = false; // (a test aid - --input guard:party: hits on the party do no damage)
    // the party's money (DAT_014c6e74, live: a Steal Gold hit takes from it at once -
    // BtlStatus::TakeDamageToCalculation 0x9b41c4); the viewer's game.cr3Money in and out
    int gold = 0;
    // (Battle_Monster_NNNN's BattleCommand.Add(unit, actions, command) - BtlStatusList::AddCommand
    // 0x009c20e4: a command for a monster this turn - its actions taken off the turn's, its target
    // getCommandTarget's with no select type; the AI still picks for any actions left), by fighter:
    // (actions, command); the next turn() takes them
    std::map<int, std::vector<std::pair<int, int>>> scripted;
    // the enemy AI's think points (BtlOrderPoint, BtlStatusList +0xe0: 8 select types, 0 .. 200 -
    // StepOrderPoint 0x009c0920 each turn's start adds battle.mbe OrderType's row of the coupling's
    // order type, every third turn ThinkPointAddition's row 2; Consume takes 50 from the type used)
    std::array<int, 8> enemyPoints{};
    int turnNo = 0;         // (BtlResult +0: phaseStart's ++)
    int orderType = 0;      // (mon_cop Coupling +0x3c)
    int orderTolerance = 0; // (Coupling +0x40: BtlSelectTypeList::Sort's tie band)
    float elementSkip = 0;  // (Coupling +0x48: IsSelected's chance of not testing the element)
    // BattleStatus.AddEnemyOrderPoint(type, n) (0x009c2658 -> BtlOrderPoint 0x0096eeb8)
    void addEnemyOrderPoint(int type, int points);
    // BtlStatusList::getCommandTarget 0x009bf740: the fighter's target for the command - BtlTargetList::
    // Collect by its target kind (an actor provoked: its provoker alone), ordered by +0xd0, the first
    // from the back IsSelected prefers (preferred: true), else the front; -1 none (or kind 138 against
    // select types 3 / 9). selectType -1: a script's (BattleCommand.Add - IsSelected's default)
    int chooseTarget(int fighter, int command, int selectType, bool* preferred = nullptr);
    // BtlTargetList::Collect 0x009ca648: the fighter's targets for the command by its target kind (a
    // provoked one's: its provoker alone), as collected - no order, no random
    std::vector<int> collectTargets(int fighter, const Cr3Command& c) const;
    // BtlTargetList::GetRandomTarget: one of them at random (a kind 51 unit's - IsAutoAttack -
    // attack's); -1 none
    int randomTarget(int fighter, int command);
    // the get-set's Mass Attack (BtlSysStateSelect::phaseAllAttack 0x9d3e2c): the command menu's
    // members (InitializeMenu: living, none under kind 31) each with its own attack command
    // (GetAttackCommandId), on one target for all - BtlStatusList::GetLowestHpTarget 0x9c388c: the
    // lowest HP among the first member's attack's targets, the first collected on ties -, a kind 51
    // unit's at random; (fighter, command, target) as turn() takes them
    std::vector<std::array<int, 3>> massAttackChoices();
    // the get-set's Auto Battle (BtlAutomaticUtility::ExecuteAutomaticToPlayer 0x974bf4, with false
    // ExecuteAutomaticToEnemy 0x975070 - the colosseum's ghosts'): a side's plan over predicted
    // outcomes (BtlAutomaticManager). Each actor - living, not under kind 31 (IsNoAction) - offers its
    // attack and its gene's commands (a monster its mode's; never an item) the select type allows
    // (CanUseSelectType: kind 138 no heals), unsealed (kind 47), not noAuto, within its HP and MP, one
    // candidate a target; each predicted (BtlAutomaticCommand::Calculate 0x971e94: damage() / heal()
    // on a copy of the rules - the battle's RNG untouched -, every hit). Then the passes, a later
    // one's pick taking the actor's place (DeleteSameActorByCommands): attack (select types 0 .. 2:
    // the most kills on the simulated HP, the most HP rate taken, the least MP, the fastest, the
    // least damage, the lowest HP), and unless IsEasyPrey 0x975bdc - every foe dead or losing half its
    // HP - guard, debuff and buff (none: 1.1.3's reject turns down every one - an order type row is
    // there), cure (type 4, the erased effects' GetAutomaticChance weight), heal (types 3 and 9: the
    // revives first, then by the lowest HP rate's gate - 50 % half the time, 35 % three quarters, 20 %
    // always). Each pick's predicted HP and MP changes reflected for the next. An actor with no pick
    // guards; a kind 51 unit attacks one at random. (fighter, command - -1 a guard -, target)
    std::vector<std::array<int, 3>> autoChoices(bool player);
    // battle.mbe OrderType's chances (+0x24 buffer, +0x28 debuffer, +0x2c guard) of a row; 0 none
    int orderChance(int row, int which) const;
    // BtlStatus::IsSelected 0x009b6cbc: whether the AI prefers the target for the command
    bool isSelected(int fighter, int target, const Cr3Command& c, int selectType);
    // (BtlStatus::IsNonTarget: a kind 32 status effect; BtlStatusEffectList::HasProhibitedTarget: a
    // kind 72 one - held, hidden - but for a command that takes it away, plus required/forbidden effects)
    bool targetable(int fighter, const Cr3Command* by = nullptr) const;
    int heal(const Cr3Fighter& source, const Cr3Command& c); // (calculateHpHeal)
    int chocolatBonus(const Cr3Fighter& target, int baseHeal) const; // (calculateChocolatBonus: skill kind 77)
    int mpCostPercent(const Cr3Fighter& f) const;            // (BtlStatusEffectUtility::GetMpCostPercent: kind 129)
    int mpCost(const Cr3Fighter& f, const Cr3Command& c) const; // (MbCommandInfo::GetMpCost)
    int hpCost(const Cr3Fighter& f, const Cr3Command& c) const; // (MbCommandInfo::GetHpCost)
    float mpCostScale = 1.0f; // (BtlBufferPotion::GetMpCostMultiplier: 1.0 default)
    // a fighter's skills' constants of one effect kind, summed (BtlSkillUtility: the enabled skills - skillOn -, their
    // effects of the gauge's advantage as it stands)
    int skillConstant(const Cr3Fighter& f, int kind) const;
    std::vector<int> skillConstants(const Cr3Fighter& f, int kind) const; // (BtlSkillVisitor: each of the kind's constants)
    int skillKindMax(int skill, int kind) const;
    // a skill's two (kind, constant) effects - with its side's gauge advantage the second set
    // (+0x44 ..; MbSkillInfo, BtlSkillVisitor's advantage)
    std::vector<std::pair<int, int>> skillEffects(int skill, bool advantage) const;
    // the commands a fighter's skills add at the top of its list (kind 127 - BtlStatus::
    // OpenCommandList: the final battle's 476, command 128 with the advantage)
    std::vector<int> skillCommands(int fighter) const;
    void useTables(const Cr3BattleTables& t) { tables_ = &t; } // (the tables alone - no battle set up) // (a skill's own effects of the kind: the highest; 0 none)
    int outcome() const; // 0 going on, 1 won, 2 lost
    // status effects (BtlStatusEffectList): given (a script's, or a command's that landed), removed;
    // whether one of a kind is on; a kind's multiplier (the effects' constants / 100, multiplied)
    // and sum; the stats as they stand (CrxCharacter's x kinds 36 / 38 / 40 / 42)
    // (lasting: no end - a field's; stackOnly: BtlStatusEffectList::Add 0x9b75c8 on an id held - one more accumulation,
    // AddAccumulation 0x9c7d98, up to the max: its turns not refreshed, its uses not re-armed - a skill's kind 60)
    void giveEffect(int fighter, int id, bool lasting = false, bool stackOnly = false);
    void removeEffect(int fighter, int id);
    bool hasEffect(int fighter, int id) const;
    int effectIcon(int id) const; // (StatusEffect col 2, +0x08: the panel's btStIcon_fr00 frame; -1 none)
    void snapshotAfter(Cr3Action& a) const;
    bool hasKind(const Cr3Fighter& f, int kind) const;
    bool hasSkillKind(const Cr3Fighter& f, int kind) const;
    float effectMultiplier(const Cr3Fighter& f, int kind) const;
    float effectStackMultiplier(const Cr3Fighter& f, int kind) const;
    int effectSum(const Cr3Fighter& f, int kind) const;
    int effectMax(const Cr3Fighter& f, int kind) const;
    int attackOf(const Cr3Fighter& f) const, defenceOf(const Cr3Fighter& f) const, speedOf(const Cr3Fighter& f) const,
        luckOf(const Cr3Fighter& f) const;
    // the command menu's buttons (BtlStatus::OpenMenuInterface: kinds 140 no Attack, 47 no Gene,
    // 66 no Item, 65 no Guard): 0 Attack, 1 Gene, 2 Item, 3 Guard
    bool menuEnabled(int fighter, int button) const;
    // a hit landing (calculateHit)
    bool hits(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c);
    // the turn's end: the effects' HP (kinds 45 / 46, a percentage of the max HP) and their turns
    std::vector<std::pair<int, int>> endTurn(); // (fighter, HP change)
    std::vector<Cr3Fighter> fighters;
    // the battle's field status effects (Battle.AddFieldStatusEffect 0x9a8108 -> BtlFieldEffectManager::Add:
    // the boot script's ids; kept across a reinforcement's setup). BtlStatusList::GiveFieldStatusEffect
    // 0x9c21f4 at every turn's start (phaseStart 0x9d35d4) - GiveFieldStatusEffectToCalculation 0x9c2220:
    // the players alone, living; an effect it is immune to passed over (HasStatusEffectInvalid), taken away
    // from one with a skill of kind 0x6a under the gauge as it stands, else given for good (-1 turns)
    std::vector<int> fieldEffects;
    void giveFieldEffects();
    // the assists (BtlCommandManager::calculateAssist 0x7dbbf4, at most one a turn, after the turn's
    // commands): the cheer points (crx_game_data + 0x1e77c, GameState::cr3Cheer - in at the battle's start,
    // what is left out again; kept across a reinforcement's setup) and none in a coupling with MbCouplingInfo
    // +0x44 bit 6 (boss and script battles)
    int cheer = 0;
    bool noAssist = false;
    // getAssistChance 0x7dca58: under 10 points none, under 25 0.1, under 50 0.2, under 80 0.3, under 100 0.4, else 0.5
    static float assistChance(int points);
    // the ten handlers in the BtlCommandManager ctor's order (0x7da0ac, +0xf0), the first that holds:
    // its command (61 .. 71), target (-1 the players' all) and cost (halved, toward 0, while a player has
    // an enabled status effect of kind 79); -1 none
    int pickAssist(int* target = nullptr, int* cost = nullptr) const;
    // calculateAssist itself: the roll (CrxRandom::GenerateReal() <= the chance - none drawn at 0) and the
    // handler's command resolved with no actor, its points spent (a finisher of the turn's last chunk first, when its
    // chance resets); empty none
    std::vector<Cr3Action> assistAfterTurn();
    // BtlStatusList::GetLowHpPlayer 0x9c3e44 / GetLowMpPlayer 0x9c40fc: a living player at 20 % HP / 10 % MP
    // or under, the lowest rate's (ties: the first); -1 none. IsLowHpAllPlayer 0x9c4428 / IsLowMpAllPlayer
    // 0x9c44b4: every player - the fallen too - at 20 % / 10 % or under
    int lowHpPlayer(bool mp) const;
    bool lowAllPlayers(bool mp) const;
    // a command with no actor (source -1: an assist, a status effect's kind 98 / 101 command), on the target
    // given - -1 every one its target kind takes of the players' side, one living player's for a one-target kind: heals
    // (formula 6 -> damage 7 a percentage of the max HP, 7 -> 17 of the max MP, 17 -> 18 both), revives, removes and gives
    // ("always lands" with +0xbc bit 3)
    Cr3Action resolveSourceless(int command, int target);
    // BtlStatusEffect::Trigger 0x9c7ad8 for a unit's effects at an event (the table in docs: 3 its own command
    // worked out, 4 before it is first hit by a command, 7 its first hit landed, 8 that not blocked and no heal, 9 no hits, 10 after
    // its command, 11 every unit after any command, 12 the turn's end): one whose +0x20 is the event's, with
    // uses left and its +0x24 condition holding (0 always, 3 HP at or under +0x28 %, 6 / 7 a physical / magic
    // command - +0xc4 1 / 2 -, 17 its turns at or under +0x28, 18 / 22 HP at or over / at or under +0x28, 20 the
    // hit downed it) is enabled and a use spent; the commands its kinds 97 .. 101 add, returned (kind, command)
    std::vector<std::pair<int, int>> triggerEffects(int fighter, int trigger, int attackKind = 0, bool downed = false);
    // BtlStatusEffect::End 0x9c7bd4: the unit's effects ending at that timing (+0x2c) disabled (unless +0x8c
    // bit 3), and removed with no uses or turns left
    void endEffects(int fighter, int timing);
    bool effectEnabled(const Cr3Fighter& f, int id) const { return !f.effectsDormant.count(id); }
    // BtlStatus::TriggerSkillToCalculation 0x9b3488 at a skill trigger (+0x1c: 0 BtlStatus::Initialize's, 1 / 2 the opening,
    // 3 .. 12 as the status effects', 5 a command aimed at it - playCommand's first step -, 6 the break gauge crossing 0):
    // BtlSkillList::Trigger 0x9bb224 - each skill of that trigger with all three uses left whose condition (+0x20, its constant
    // +0x24) holds and whose chance (+0x2c, +0x40 with the gauge's advantage, of 256) rolls is enabled, each use above 0 one
    // less (BtlSkill::Trigger 0x9c60a0); then every enabled skill's kind 60 (with the gauge's advantage as it stands) gives
    // its status effect - one held gains an accumulation alone - and its kind 61 takes it away. total: the trigger's HP change
    // (+0x1c: a command's on it, below 0 damage - condition 29); other: the other unit (condition 23 - the command's source).
    // The newly enabled skills' kinds 97 .. 101 / 126 commands returned (kind, command)
    std::vector<std::pair<int, int>> triggerSkills(int fighter, int trigger, int attackKind = 0, bool downed = false, int command = -1,
                                                   int total = 0, int other = -1);
    // BtlStatusEffectUtility::GetCommandChange 0x9b1f30: the unit's first enabled status effect - in the list's slot order,
    // StatusEffect +0x04 - with a kind 120 (0x78) constant of 0 or more: the command it plays instead; -1 none
    int commandChange(const Cr3Fighter& f) const;
    // the opening (BtlSysStateSelect::phaseStart 0x9d35d4, the first turn alone): trigger 1 on every living unit's skills,
    // their enabled kind 100 constants {the unit, the command} (GetOpeningSkillCommand 0x9b5c78), end timing 1; the same for
    // trigger 2, made unique by the command. (fighter, command) in order - their commands give nothing themselves:
    // BtlOpeningDirection's visuals alone (the viewer's)
    std::vector<std::pair<int, int>> opening();
    // the turn end's script commands (BattleCommand.SetId / SetIdAndTarget: (command, target - -1 its own)) worked out:
    // end timing 11 on every unit first, then each with no source (resolveSourceless) - an assist direction's (CommandDirection
    // kind 13 / 40) marked assist
    std::vector<Cr3Action> scriptCommands(const std::vector<std::pair<int, int>>& cmds);
    // the turn-end list (trigger 12's kind 100 / 101 commands, gathered at turn()'s end) worked out, then cleared
    std::vector<Cr3Action> turnEndActions();

private:
    const Cr3BattleTables* tables_ = nullptr;
    std::vector<Cr3Action> resolveOrder(std::vector<std::array<int, 4>> order, size_t bucket0End);
    std::pair<int, int> priorityAndCost(int fighter, int command) const;
    void sortBucket(std::vector<std::array<int, 4>>& bucket);
    void queueTurnEnd(int bearer, const std::vector<std::pair<int, int>>& added);
    void markAssist(Cr3Action& a) const;
    // a trigger's condition (BtlStatusEffect::Trigger's / BtlSkill::Trigger's +0x24 / +0x20 and its constant); turns -1: a skill's
    bool conditionHolds(const Cr3Fighter& f, int condition, int constant, int attackKind, bool downed, int turns, int command, int total = 0,
                        int other = -1) const;
    std::array<int, 3>& skillUsesOf(Cr3Fighter& f, int skill, const MbeFile::Row& row); // (the row's +0x10 / +0x14 / +0x18 at first)
    int skillMost(const Cr3Fighter& f, int kind) const; // (the enabled skills' highest constant of the kind - BtlSkillUtility::GetLeastHp 0x9b0368's 53; 0 none)
    std::vector<std::array<int, 3>> turnEndQueue_; // (BtlCommandManager +0x48: source - -1 none -, command, target)
    // BtlCommandManager +0x188 (the last chunk not inserted) and +0xb8 (the combo's actors): cleared by Clear 0x7da8f8 - the
    // turn's start - alone, so they last into the assist and turn-end passes
    struct FinisherChunk {
        bool set = false;
        int attacker = -1, firstTarget = -1, command = -1;
        bool combo = false;
        std::vector<int> hit;
    };
    FinisherChunk finisherChunk_;
    std::vector<int> comboActors_;
    bool turnEndTriggered_ = false;                // (turn() ran trigger 12; endTurn() then doesn't)
    uint32_t rng_ = 1;
    float random(); // [0, 1)
    void rollCritAndDown(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, bool& crit, bool& down, float& rate);
    int hitDamage(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, bool crit, bool down, float rate);
    bool effectLands(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, int id);
    bool effectInvalid(const Cr3Fighter& t, int id) const; // (HasStatusEffectInvalid: kind 68 - a status effect's or a skill's - of the id)
    // a skill effect of the kind enabled (BtlSkillList::HasEnableEffect): an enabled skill's - one of no trigger, or one its
    // trigger enabled (a block's at trigger 4, its chance rolled there) until its end timing -; its constants' highest
    // (GetFixedDamage 0x9b02a0)
    bool skillEnabled(const Cr3Fighter& f, int kind, int* most = nullptr) const;
    std::array<int, 4> breakWith_{0, 25, 25, 50}, breakWithout_{100, 50, 50, 100}; // (battle.mbe Battle +0x50 / +0x60: base, critical, down, kill)
    std::vector<int> resistIds_;
    std::set<std::pair<int, int>> usedSkills_; // (fighter, skill: the added commands fired)
    std::array<float, 4> coopScale_{0, 1, 3, 5}; // (battle.mbe Battle +0x80 / +0x88: MbBattleInfo::GetCooperationScale, by the units in it)
    float coopNow_ = 1;                          // (the action's: calculateBasicDamage's x scale)
    int statusTurnBoost_ = 0;                    // (skill kind 84: GetStatusEffectContinueTurn added on giveEffect)
    int livingTarget(bool player, int preferred, const Cr3Command* by = nullptr);
    std::vector<std::array<int, 8>> orderTypes_;  // (battle.mbe OrderType: each order type's points a turn, by select type)
    std::vector<std::array<int, 3>> orderChances_; // (its +0x24 / +0x28 / +0x2c: the buffer, debuffer and guard chances out of 256)
    std::array<int, 8> thinkEvery3_{0, 0, 0, 0, 0, 0, 0, 50}; // (ThinkPointAddition row 2: every third turn's)
    void loadMode(Cr3Fighter& f, const MbeFile::Row& m); // (a MonsterCommand row: initializeCommandSkill 0x009c6960)
    // BtlStatusList::addCommandByAI 0x009c0df4: one pick for the monster - appended to raw (fighter,
    // command, target, 0) and queued (source, target, command: BtlCommandList::Has's), its actions,
    // uses and think points spent
    void addCommandByAI(int fighter, std::vector<std::array<int, 4>>& raw, std::vector<std::array<int, 3>>& queued);
};

} // namespace cr3
