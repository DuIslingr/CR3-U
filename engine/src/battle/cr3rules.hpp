#pragma once
// CR3's battle rules (docs/FORMATS.md "Battles (CR3)"): the fighters' numbers, the commands, a
// turn's actions and their damage.
//
// The numbers: a character's (char_para: MbCharInfo) and a monster's (mon_para: MbMonsterInfo)
// base + (level - 1) x growth / 100 - HP, MP, attack, defence, speed, luck. A command
// (command_para Command, MbCommandInfo): its power (+0x14), hits (+0x21; below 0 the unit's own),
// its damage formula (+0xc0 -> CommandFomula: 1 physical, 2 magic), its spread (+0xa8, a
// percentage either way: calculateHpFluctuation), its multiplier (+0x18); its motion
// (direction.mbe CommandDirection +8, an AnimCommon type). A monster picks among its mode's
// commands (MonsterCommand: eight (command, ratio, count)) by ratio.
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
// effects' kinds and the misses: docs/FORMATS.md "Battles (CR3)". Not in yet: skills' conditional
// effects and the rarer multipliers (Damage Multi, Team Play, Target Skill), evasion, the status
// effects' triggers, end timings and stacks.
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
    int power = 0, hits = 1, formula = 1, spread = 0;
    bool hitsOwn = false; // (+0x21 below 0: the unit's own)
    int repeat = 1;       // (+0x20: its attacks, each of its hits - Rakshasa Lotus 3 x the weapon's 1)
    int breakPoint = -1;  // (+0x22, s16: its break points a hit - below 0 none; with flag bit 1 the unit's weapon's instead)
    uint32_t flags = 0;   // (+0xbc: bit 0 a gene's, 1 the weapon's break, 2 always hits, 3 its status effects always land, 4 can be critical, 5 always critical, 6 always downs,
                          //  7 can be cooperated in)
    float hitRate = 1;    // (+0x1c: x the hit chance)
    std::vector<int> effects; // (+0x40 / +0x48: the status effects it gives - Guard's 2)
    std::vector<int> removes; // (+0x50 / +0x58: the status effects it takes away - MbCommandInfo::HaveRemovesStatusEffect; battle 7's release 1844)
    int effectsAnnounce = -1; // (+0xe8: AnnounceBattle's line when it gives one target several)
    int type = 0;         // (+0x04: 0 an attack, 1 a physical skill, 2 magic, 3 support, 4 special, 7 a guard)
    int damage = -1;      // (its CommandFomula row's damage: 1 physical, 2 magic, 5 an HP heal, ...)
    int target = 3;       // (+0x10: BtlStatusList's target kind - 0 itself, 1 an ally, 2 all allies, 3 an enemy,
                          //  4 all enemies, 5 everyone, 6 a random enemy, ...)
    bool all = false;     // (target kind 2, 4, 5, 9 or 11: every one on that side)
    int mpCost = 0, hpCost = 0; // (+0xb8, +0xb0: the costs as listed - kinds 0 and 3; Cr3BattleRules::mpCost the real one)
    int mpKind = 0;             // (+0xb4: 0 the value, 1 / 2 a percentage of the max MP, 3 x the weapon's consumption)
    bool ally() const { return damage == 5 || damage == 6 || damage == 7; } // (a heal: its own side; -1 no damage, its effects alone)
    bool usable() const { return damage == 1 || damage == 2 || ally(); }    // (what CR3-U can play)
    float multiplier = 1;
    int element = 0;    // (+0x24: its attack element - the Element table's row)
    int motion = -1;    // (an AnimCommon type: 10 bi01, 15 bs01, ...; -1 none)
    int direction = -1; // (CommandDirection +4: its kind)
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
    int attackCommand = -1;                     // (a character's +0x10; a monster's mode's +0x14)
    std::vector<int> geneCommands;              // (a character's gene's: a guest's GuestGene row)
    int geneLevel = 0;                          // (VGetGeneLevel: the "Level" step's - a character's gene's, a monster's level)
    std::vector<std::pair<int, int>> commands; // (a monster's: command, ratio)
    // its status effects (BtlStatusEffectList: skill_para StatusEffect ids, the turns left - -1
    // until removed)
    std::vector<std::pair<int, int>> effects;
    std::array<int, 6> effectResist{}; // (a monster's resistances to battle.mbe's listed effects)
    bool comboChance = false; // (BtlBasicStatus +0x6d: downed - every later hit on it downs too, until the acting side changes)
    int combo = 0;            // (its combo count: +1 a downing hit; the magic chain bonus's)
    bool dead() const { return hp <= 0; }
};

struct Cr3Hit {
    int target = -1;
    std::vector<int> hits; // each hit's damage (a heal's: below 0)
    std::vector<uint8_t> flags; // each hit's: bit 0 critical, bit 1 down, bit 2 a miss
    std::vector<int> gauge; // the break gauge after each hit
    std::vector<int> effectsGiven; // the status effects it gave (StatusEffect ids)
    int hpAfter = 0;
    bool killed = false;
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
    bool cooperated() const { return !partners.empty(); } // (BtlDamageChunk::IsCooperation)
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
    int teamLevel = 1; // (every character's level: CrxCharacter::initialize's DAT_014c6ab0)
    bool playerStats(const Cr3BattleTables& tables, int character, Cr3Fighter& out) const;
    // the VS mode's tallies (BtlStatus::TakeDamageToDirection: the party's damage to the monsters -
    // the battle's score, DAT_014a7d88, 9999999 at most; ConsumeMpToDirection: the party's MP used -
    // the clear bonus's percentage; GetNumberOfKilledNonPlayer)
    long partyDamage = 0;
    int partyMp = 0, killed = 0;
    bool setup(const Cr3BattleTables& tables, const Cr3Battle& battle, int monsterLevel, uint32_t seed, std::string* error = nullptr);
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
    void addBreak(int points);
    // (the party's first strike - BtlStatusList::ClearRatioToEnemy: the monsters' actions this turn
    // none - the next turn() the party's alone)
    bool monstersWait = false;
    bool partyUnhurt = false; // (a test aid - --input guard:party: hits on the party do no damage)
    // (Battle_Monster_NNNN's BattleCommand.Add - BtlStatusList::AddCommand: a command for a monster
    // this turn, in place of its own pick), by fighter; the next turn() takes them
    std::map<int, std::vector<int>> scripted;
    // (BtlStatus::IsNonTarget: a kind 32 status effect; BtlStatusEffectList::HasProhibitedTarget: a
    // kind 72 one - held, hidden - but for a command that takes it away)
    bool targetable(int fighter, const Cr3Command* by = nullptr) const;
    int heal(const Cr3Fighter& source, const Cr3Command& c); // (calculateHpHeal)
    int mpCost(const Cr3Fighter& f, const Cr3Command& c) const; // (MbCommandInfo::GetMpCost)
    // a fighter's skills' constants of one effect kind, summed (BtlSkillUtility: the skills
    // without a condition, their effects without the gauge's advantage)
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
    void giveEffect(int fighter, int id, bool lasting = false); // (lasting: no end - a field's)
    void removeEffect(int fighter, int id);
    bool hasEffect(int fighter, int id) const;
    bool hasKind(const Cr3Fighter& f, int kind) const;
    float effectMultiplier(const Cr3Fighter& f, int kind) const;
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

private:
    const Cr3BattleTables* tables_ = nullptr;
    uint32_t rng_ = 1;
    float random(); // [0, 1)
    bool effectLands(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, int id);
    std::array<int, 4> breakWith_{0, 25, 25, 50}, breakWithout_{100, 50, 50, 100}; // (battle.mbe Battle +0x50 / +0x60: base, critical, down, kill)
    std::vector<int> resistIds_;
    std::set<std::pair<int, int>> usedSkills_; // (fighter, skill: the added commands fired)
    std::array<float, 4> coopScale_{0, 1, 3, 5}; // (battle.mbe Battle +0x80 / +0x88: MbBattleInfo::GetCooperationScale, by the units in it)
    float coopNow_ = 1;                          // (the action's: calculateBasicDamage's x scale)
    int livingTarget(bool player, int preferred, const Cr3Command* by = nullptr);
};

} // namespace cr3
