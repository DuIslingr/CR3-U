#include "battle/cr3rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cr3 {

namespace {

// the rows' layouts (assets/mbe.hpp)
constexpr const char* kCharStats = "iiiiiiiii_ip_ipiiiiiiii"; // 4 the attack command; 13 HP, 14 MP, 15 atk | def, 16 spd | luck, 17 .. 20 their growth
constexpr const char* kMonsterStats = "iipiiiiiiiiiiii";          // 6 HP, 7 MP, 8 atk | def, 9 spd | luck, 10 .. 13 their growth
constexpr const char* kMonsterCommand = "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"; // id, mode, -, -, -, the attack command, -, -, then (command, ratio, count) x 8
constexpr const char* kCommand = "iiiiiiffiii_ip_ip_ip_ip_ip_ip_ipiiiiiiiiiiiiiii_pi"; // 5 power, 6 multiplier, 8 packed (target, hits, ..), 27 spread (+0xa8), 33 formula (+0xc0)
constexpr const char* kFormula = "iiii";  // id, action, damage, chain
constexpr const char* kDirection = "iii"; // command, kind, motion

int16_t low16(int32_t v) { return int16_t(uint32_t(v) & 0xffff); }
int16_t high16(int32_t v) { return int16_t(uint32_t(v) >> 16); }
int grown(int base, int growth, int level) { return base + (level - 1) * growth / 100; }

} // namespace

float Cr3BattleRules::random() {
    rng_ = rng_ * 1103515245u + 12345u;
    return float((rng_ >> 8) & 0xffffff) / float(0x1000000);
}

bool Cr3BattleRules::setup(const Cr3BattleTables& t, const Cr3Battle& b, int monsterLevel, uint32_t seed, std::string* error) {
    tables_ = &t;
    rng_ = seed ? seed : 1;
    fighters.clear();
    for (size_t k = 0; k < b.units.size(); ++k) {
        const Cr3BattleUnit& u = b.units[k];
        Cr3Fighter f;
        f.unit = int(k), f.player = u.player;
        MbeFile::Row r;
        if (u.player) {
            // (MbCharInfo: the level the save's - a character at 1 until those are kept)
            if (!t.character.find("Char", u.character, kCharStats, r)) continue;
            f.id = u.character, f.level = 1;
            f.maxHp = grown(r.i(13), r.i(17), f.level), f.maxMp = grown(r.i(14), r.i(18), f.level);
            f.attack = grown(low16(r.i(15)), low16(r.i(19)), f.level), f.defence = grown(high16(r.i(15)), high16(r.i(19)), f.level);
            f.speed = grown(low16(r.i(16)), low16(r.i(20)), f.level), f.luck = grown(high16(r.i(16)), high16(r.i(20)), f.level);
            f.attackCommand = r.i(4);
        } else {
            if (!t.monster.find("Monster", u.monster, kMonsterStats, r)) continue;
            f.id = u.monster, f.level = std::max(monsterLevel, 1);
            f.maxHp = grown(r.i(6), r.i(10), f.level), f.maxMp = std::max(grown(r.i(7), r.i(11), f.level), 0);
            f.attack = grown(low16(r.i(8)), low16(r.i(12)), f.level), f.defence = grown(high16(r.i(8)), high16(r.i(12)), f.level);
            f.speed = grown(low16(r.i(9)), low16(r.i(13)), f.level), f.luck = grown(high16(r.i(9)), high16(r.i(13)), f.level);
            // (MbMonsterInfo's mode 0: its MonsterCommand row)
            if (const MbeFile::Table* mc = t.monster.table("MonsterCommand"))
                for (size_t row = 0; row < mc->rows.size(); ++row) {
                    MbeFile::Row m;
                    if (!t.monster.row(*mc, row, kMonsterCommand, m) || m.i(0) != u.monster || m.i(1) != 0) continue;
                    f.attackCommand = m.i(5);
                    for (size_t c = 0; c < 8; ++c)
                        if (m.i(8 + c * 3) >= 0) f.commands.emplace_back(m.i(8 + c * 3), std::max(m.i(9 + c * 3), 0));
                    break;
                }
        }
        f.hp = f.maxHp, f.mp = f.maxMp;
        fighters.push_back(f);
    }
    if (fighters.empty()) {
        if (error) *error = "no fighters";
        return false;
    }
    return true;
}

bool Cr3BattleRules::command(int id, Cr3Command& c) const {
    c = Cr3Command();
    MbeFile::Row r;
    if (!tables_ || !tables_->command.find("Command", id, kCommand, r)) return false;
    c.id = id;
    c.power = r.i(5);
    c.multiplier = r.f(6);
    const int8_t hits = int8_t((uint32_t(r.i(8)) >> 8) & 0xff); // (+0x21)
    c.hits = hits > 0 ? hits : 1;
    c.spread = r.i(27);
    MbeFile::Row f;
    c.formula = tables_->command.find("CommandFomula", r.i(33), kFormula, f) ? f.i(2) : 1;
    MbeFile::Row d;
    if (tables_->direction.find("CommandDirection", id, kDirection, d)) c.direction = d.i(1), c.motion = d.i(2);
    return true;
}

int Cr3BattleRules::damage(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c) {
    float ratio = float(s.attack) / float(std::max(t.defence, 1));
    ratio = std::clamp(ratio, 0.2f, 5.0f);
    const int power = c.power < 0 ? s.attack : c.power; // (below 0: the unit's own - its vtable's 0x60)
    float d = ratio * float(power) * 0.5f;
    d *= s.player ? 1.0f : float(s.level) / 20.0f + 1.0f; // ("Level": a character's gene level - none yet - a monster's level)
    d *= c.multiplier > 0 ? c.multiplier : 1.0f;
    if (t.guarding) d *= 0.5f; // (Guard: an approximation - its status effect's multiplier not read yet)
    if (c.spread > 0) d *= ((random() - 0.5f) * float(c.spread) * 2 + 100.0f) / 100.0f;
    return std::max(int(d), 1);
}

int Cr3BattleRules::livingTarget(bool player, int preferred) {
    if (preferred >= 0 && size_t(preferred) < fighters.size() && !fighters[size_t(preferred)].dead() &&
        fighters[size_t(preferred)].player == player)
        return preferred;
    std::vector<int> alive;
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player == player && !fighters[k].dead()) alive.push_back(int(k));
    if (alive.empty()) return -1;
    return alive[size_t(random() * float(alive.size())) % alive.size()];
}

std::vector<Cr3Action> Cr3BattleRules::turn(const std::vector<std::array<int, 3>>& choices) {
    std::vector<Cr3Action> out;
    for (Cr3Fighter& f : fighters) f.guarding = false;
    // the party's (their choices), then the monsters' (by their commands' ratios)
    std::vector<std::array<int, 3>> order;
    for (const auto& c : choices)
        if (c[0] >= 0 && size_t(c[0]) < fighters.size() && fighters[size_t(c[0])].player) order.push_back(c);
    for (size_t k = 0; k < fighters.size(); ++k) {
        const Cr3Fighter& f = fighters[k];
        if (f.player || f.dead()) continue;
        int cmd = f.attackCommand;
        int total = 0;
        for (const auto& [id, ratio] : f.commands) total += ratio;
        if (total > 0) {
            int pick = int(random() * float(total));
            for (const auto& [id, ratio] : f.commands)
                if ((pick -= ratio) < 0) {
                    cmd = id;
                    break;
                }
        }
        order.push_back({int(k), cmd, -1});
    }
    for (const auto& [src, cmd, tgt] : order) {
        Cr3Fighter& s = fighters[size_t(src)];
        if (s.dead()) continue;
        Cr3Action a;
        a.source = src, a.command = cmd;
        if (cmd < 0) { // (Guard)
            s.guarding = true;
            out.push_back(a);
            continue;
        }
        a.target = livingTarget(!s.player, tgt);
        if (a.target < 0) break; // (no one left on the other side)
        Cr3Fighter& t = fighters[size_t(a.target)];
        Cr3Command c;
        if (!command(cmd, c)) c.power = s.attack, c.hits = 1;
        for (int h = 0; h < c.hits && !t.dead(); ++h) {
            const int d = damage(s, t, c);
            a.hits.push_back(d);
            t.hp = std::max(t.hp - d, 0);
        }
        a.hpAfter = t.hp, a.killed = t.dead();
        out.push_back(a);
        if (outcome() != 0) break;
    }
    return out;
}

int Cr3BattleRules::outcome() const {
    bool party = false, monsters = false;
    for (const Cr3Fighter& f : fighters)
        if (!f.dead()) (f.player ? party : monsters) = true;
    return !monsters ? 1 : !party ? 2 : 0;
}

} // namespace cr3
