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
// Damage (BtlCalculator::calculateBasicDamage), a hit's: clamp(attack / defence, 0.2, 5) x
// (power + 0) x 0.5, x 1.5 with the break gauge's advantage ("Status"), x the element's rate
// ("Element"), x (resistance + 100) / 100, x (gene level / 20 + 1) ("Level": a monster's its
// level), x the command's multiplier, then the spread; at least 1. Not in yet: skills, status
// effects (battle_0001's Battle_Boot gives the hero four), elements, equipment, critical hits,
// misses, the break gauge (Even throughout) - the reference's first hit is 352 where this gives
// ~423.
//
// A turn (BtlCommandManager::Calculate: a command list per side, in order): the party's chosen
// commands, then each living monster's; a dead target's attack goes to another living one.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "battle/cr3battle.hpp"

namespace cr3 {

struct Cr3Command {
    int id = -1;
    int power = 0, hits = 1, formula = 1, spread = 0;
    float multiplier = 1;
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
    int attackCommand = -1;                     // (a character's +0x10; a monster's mode's +0x14)
    std::vector<std::pair<int, int>> commands; // (a monster's: command, ratio)
    bool guarding = false;
    bool dead() const { return hp <= 0; }
};

struct Cr3Action {
    int source = -1, target = -1, command = -1; // (fighters' indices; command -1: a guard)
    std::vector<int> hits;                      // each hit's damage
    int hpAfter = 0;
    bool killed = false;
};

class Cr3BattleRules {
public:
    bool setup(const Cr3BattleTables& tables, const Cr3Battle& battle, int monsterLevel, uint32_t seed, std::string* error = nullptr);
    bool command(int id, Cr3Command& out) const;
    // a turn: the party's choices (a fighter's index -> command (-1 a guard), target), then the
    // monsters' own; returns the actions in order, applied
    std::vector<Cr3Action> turn(const std::vector<std::array<int, 3>>& choices);
    int damage(const Cr3Fighter& source, const Cr3Fighter& target, const Cr3Command& c);
    int outcome() const; // 0 going on, 1 won, 2 lost
    std::vector<Cr3Fighter> fighters;

private:
    const Cr3BattleTables* tables_ = nullptr;
    uint32_t rng_ = 1;
    float random(); // [0, 1)
    int livingTarget(bool player, int preferred);
};

} // namespace cr3
