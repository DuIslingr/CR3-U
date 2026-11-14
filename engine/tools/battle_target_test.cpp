// Generated parameter tables and fighters only; no game data is needed.
#include "battle/cr3rules.hpp"
#include "assets/mvgl.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>

using Bytes = std::vector<uint8_t>;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void put(Bytes& b, size_t at, uint32_t n) { std::memcpy(b.data() + at, &n, 4); }
void append(Bytes& b, uint32_t n) { const size_t at = b.size(); b.resize(at + 4); put(b, at, n); }
uint32_t bits(float n) { uint32_t out; std::memcpy(&out, &n, 4); return out; }
struct Table {
    const char* name;
    const char* layout;
    std::vector<std::map<int, uint32_t>> rows;
    std::vector<std::map<int, std::vector<uint32_t>>> chunks;
};
Bytes tables(const std::vector<Table>& tables) {
    Bytes out{'E','X','P','A'}; append(out, uint32_t(tables.size()));
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> ptrs;
    for (const Table& t : tables) {
        std::vector<size_t> columns;
        size_t size = 0;
        for (const char* p = t.layout; *p; ++p) {
            if (*p == '_' || *p == 'p') size = (size + 7) / 8 * 8;
            if (*p == '_') continue;
            columns.push_back(size); size += *p == 'p' ? 8 : 4;
        }
        size = (size + 7) / 8 * 8;
        const size_t textLen = std::strlen(t.name), len = (textLen + 1 + 3) / 4 * 4;
        append(out, uint32_t(len));
        const size_t nameAt = out.size(); out.resize(nameAt + len);
        std::memcpy(out.data() + nameAt, t.name, textLen);
        append(out, uint32_t(size)); append(out, uint32_t(t.rows.size()));
        out.resize((out.size() + 7) / 8 * 8);
        for (size_t r = 0; r < t.rows.size(); ++r) {
            const size_t base = out.size(); out.resize(base + size);
            for (const auto& [column, value] : t.rows[r]) put(out, base + columns.at(size_t(column)), value);
            if (r < t.chunks.size())
                for (const auto& [column, vec] : t.chunks[r]) ptrs.push_back({uint32_t(base + columns.at(size_t(column))), vec});
        }
    }
    out.insert(out.end(), {'C','H','N','K'}); append(out, uint32_t(ptrs.size()));
    for (const auto& [slotOff, vec] : ptrs) {
        append(out, slotOff);
        append(out, uint32_t(vec.size() * 4));
        for (uint32_t v : vec) append(out, v);
    }
    return out;
}
int main() {
    try {
        cr3::gWideAnimations = true;
        cr3::Cr3BattleTables t;
        constexpr const char* commandLayout = "iiiiiiffiii_ip_ip_ip_ip_ip_ip_ipiiiiiiiiiiiiiii_pi";
        Table commands{"Command", commandLayout, {}};
        //one/all flexible targets and their fixed-side equivalents; plus priority/speedCost, elemental, multi-hit chain.
        for (const auto [id, target, formula] : {std::array<int,3>{80,8,1}, {90,9,1}, {50,5,1}, {30,3,1}, {81,8,5}, {91,9,5}})
            commands.rows.push_back({{0,uint32_t(id)}, {1,1}, {4,uint32_t(target)}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {33,uint32_t(formula)}});
        // Guard command 51 (priority +2), fast cost command 60 (speedCost +80), bucket-2 command 61 (priority -8),
        // bucket-5 command 62 (priority -9), fire attack 1070 (element 1), 3-hit chain attack 1071 (repeat 3, formula 2).
        commands.rows.push_back({{0,51}, {1,1}, {4,0}, {25,2}, {33,1}});
        commands.rows.push_back({{0,60}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {25,0}, {26,80}, {33,1}});
        commands.rows.push_back({{0,61}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {25,uint32_t(-8)}, {33,1}});
        commands.rows.push_back({{0,62}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {25,uint32_t(-9)}, {33,1}});
        commands.rows.push_back({{0,1070}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {9,1}, {24,100}, {32,4}, {33,1}});
        commands.rows.push_back({{0,1071}, {1,1}, {4,3}, {5,800}, {6,bits(1)}, {7,bits(1)}, {8,0x103}, {24,100}, {32,4}, {33,2}});
        // Round 2 audited formulas, costs, and revive/status target eligibility:
        // 203: HP-consumption physical (hpKind=1, hpCost=50%, mpKind=3, mpCost=5, formula=13 -> damage=13)
        // 302: Revive ally other (target=12, power=50, removes={1}, requiredEffects={1}, formula=7 -> damage=7)
        // 303: Full Revive (target=13, power=100, mpKind=1, mpCost=100, removes={1}, requiredEffects={1}, formula=7 -> damage=7)
        // 511: Potion / Chocolate (target=1, power=100, formula=18 -> damage=6, chain=19)
        // 514: Ether (target=1, power=60, formula=16 -> damage=16)
        // 517: Elixir (target=1, power=100, formula=17 -> damage=18)
        // 800: Gravity 25% HP (target=3, power=25, flags=4, formula=8 -> damage=8)
        // 1646: MP Damage 30 (target=3, power=30, flags=4, formula=20 -> damage=20)
        // 1650: Reduce HP to 1 (target=3, power=1, flags=4, formula=24 -> damage=24)
        commands.chunks.resize(commands.rows.size());
        commands.rows.push_back({{0,203}, {1,1}, {4,3}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {28,1}, {29,50}, {30,3}, {31,5}, {32,4}, {33,13}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,302}, {1,3}, {4,12}, {5,50}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {15,1}, {21,1}, {30,0}, {31,16}, {32,4}, {33,7}});
        commands.chunks.push_back({{16, {1}}, {22, {1}}});
        commands.rows.push_back({{0,303}, {1,3}, {4,13}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {15,1}, {21,1}, {28,1}, {29,0}, {30,1}, {31,100}, {32,4}, {33,7}});
        commands.chunks.push_back({{16, {1}}, {22, {1}}});
        commands.rows.push_back({{0,511}, {1,3}, {4,1}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {32,4}, {33,18}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,514}, {1,3}, {4,1}, {5,60}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {32,4}, {33,16}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,517}, {1,3}, {4,1}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {32,4}, {33,17}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,800}, {1,2}, {4,3}, {5,25}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {32,4}, {33,8}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,1646}, {1,0}, {4,3}, {5,30}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {32,4}, {33,20}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,1650}, {1,0}, {4,3}, {5,1}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {32,4}, {33,24}});
        commands.chunks.push_back({});
        // Round 3 audited commands:
        // 720: Fire spell (type=2, target=3, power=100, element=1, mpKind=0, mpCost=20, flags=4, formula=1)
        // 721: Ice spell (type=2, target=3, power=100, element=2, mpKind=0, mpCost=20, flags=4, formula=1)
        // 722: Formula 12 extraType 18 (+20 power per matching status in extraConstants={301, 302})
        // 723: Guaranteed status (flags=8, gives={301})
        // 724: Break force-set (extraType=13, extraConstants[0]=777)
        commands.rows.push_back({{0,720}, {1,2}, {4,3}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {9,1}, {24,100}, {30,0}, {31,20}, {32,0x84}, {33,1}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,721}, {1,2}, {4,3}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {9,2}, {24,100}, {30,0}, {31,20}, {32,4}, {33,1}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,722}, {1,1}, {4,3}, {5,100}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {15,2}, {24,100}, {32,0x14}, {33,12}, {38,18}, {39,2}});
        commands.chunks.push_back({{16, {301, 302}}, {40, {301, 302}}});
        commands.rows.push_back({{0,723}, {1,2}, {4,3}, {5,0}, {6,bits(1)}, {7,bits(1)}, {8,0x100}, {11,1}, {24,100}, {32,12}, {33,1}});
        commands.chunks.push_back({{12, {301}}});
        commands.rows.push_back({{0,724}, {1,2}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {32,4}, {33,1}, {38,13}, {39,1}});
        commands.chunks.push_back({{40, {777}}});
        // Round 10: 760 a one-hit attack that always hits and downs and can lead into a finisher (+0xbd bit 1 -
        // flags bit 9), 761 a Last Attack (flags 0x414, as command_para's 11 .. 35)
        commands.rows.push_back({{0,760}, {1,0}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {32,0x244}, {33,1}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,761}, {1,0}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {32,0x414}, {33,1}});
        commands.chunks.push_back({});
        // Round 30: 762 the same as 760, physical (+0xc4 1 - col 34): a Counter answers it
        commands.rows.push_back({{0,762}, {1,0}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {32,0x244}, {33,1}, {34,1}});
        commands.chunks.push_back({});
        // Round 18: 765 a random-enemy command (target kind 6) of 10 one-hit actions, as 1604 Omni Missiles
        commands.rows.push_back({{0,765}, {1,0}, {4,6}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x10a}, {24,100}, {32,4}, {33,1}});
        commands.chunks.push_back({});
        // Round 20 (the enemy AI): 900 an attack of select type 0 (+0xc8), select priority 10 (+0xcc); 901 the same of
        // priority 0; 902 a buff of select type 5 on an ally (target kind 1) giving 305, no damage (formula 40: -1);
        // none changes an element (+0x28 -1)
        commands.rows.push_back({{0,900}, {1,0}, {4,3}, {5,1}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {10,uint32_t(-1)}, {32,4}, {33,1}, {35,0}, {36,10}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,901}, {1,0}, {4,3}, {5,1}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {10,uint32_t(-1)}, {32,4}, {33,1}, {35,0}, {36,0}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,902}, {1,3}, {4,1}, {5,0}, {6,bits(1)}, {7,bits(1)}, {8,0x100}, {10,uint32_t(-1)}, {13,1}, {32,0xc}, {33,40}, {35,5}, {36,10}});
        commands.chunks.push_back({{14, {305}}});
        // Round 21: 1647 Money Collector - CommandFomula 20 (here 21): damage formula 21 calculateStealGold, chain -1
        commands.rows.push_back({{0,1647}, {1,0}, {4,3}, {5,140}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {32,4}, {33,21}});
        commands.chunks.push_back({});
        // Round 22 (the Auto Battle): 950 a heal of select type 3 on an ally (calculateHpHeal, power 50); 951 a strong
        // attack never on auto (+0xbe bit 0 - flags bit 16); 952 a stronger attack of select type 1 costing 30 MP
        commands.rows.push_back({{0,950}, {1,3}, {4,1}, {5,50}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {10,uint32_t(-1)}, {32,4}, {33,5}, {35,3}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,951}, {1,1}, {4,3}, {5,500}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {10,uint32_t(-1)}, {32,0x10004}, {33,1}, {35,1}});
        commands.chunks.push_back({});
        commands.rows.push_back({{0,952}, {1,1}, {4,3}, {5,50}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {10,uint32_t(-1)}, {30,0}, {31,30}, {32,4}, {33,1}, {35,1}});
        commands.chunks.push_back({});
        // Round 23 (status-effect triggers, fixed damage, blocks, assists): 1100 a physical attack (+0xc4 1 - col 34),
        // 1101 a magic one (+0xc4 2, formula 30 -> damage 2), 1107 a physical one of 3 hits; 1102 .. 1106 givers on
        // oneself (no damage, +0xbc bits 2 and 3) of Mental Wall 17, Moonlight Shadow 18, Rally 23, Counter 25,
        // Regenerate 92; 56 Counter's attack (formula 10, +0xc4 0); 306 Regenerate's revive (target 0, 50 % -
        // damage 7 -, removes 1 and 92, requires 1); the assists' 63 / 65 (heals of 50 %), 67 / 69 / 70 (removes
        // 13 / 14 / 15) and 71 (gives 3 .. 6, always landing)
        auto row = [&](std::map<int, uint32_t> r, std::map<int, std::vector<uint32_t>> chunk = {}) {
            r.emplace(6, bits(1)), r.emplace(7, bits(1));
            commands.rows.push_back(r), commands.chunks.push_back(chunk);
        };
        row({{0,1100}, {1,1}, {4,3}, {5,10}, {8,0x101}, {32,4}, {33,1}, {34,1}});
        row({{0,1101}, {1,2}, {4,3}, {5,10}, {8,0x101}, {32,4}, {33,30}, {34,2}});
        row({{0,1107}, {1,1}, {4,3}, {5,10}, {8,0x301}, {32,4}, {33,1}, {34,1}});
        int effectGiver = 1102;
        for (uint32_t e : {17u, 18u, 23u, 25u, 92u}) row({{0,uint32_t(effectGiver++)}, {1,4}, {4,0}, {8,0x100}, {13,1}, {32,0xc}, {33,40}}, {{14, {e}}});
        row({{0,56}, {1,7}, {4,3}, {5,100}, {8,0x101}, {25,uint32_t(-7)}, {32,0x404}, {33,10}});
        row({{0,306}, {1,3}, {4,0}, {5,50}, {8,0x100}, {15,2}, {21,1}, {25,9}, {32,0x4800c}, {33,7}}, {{16, {1, 92}}, {22, {1}}});
        row({{0,63}, {1,7}, {4,11}, {5,50}, {8,0x100}, {32,0x404}, {33,7}});
        row({{0,65}, {1,7}, {4,10}, {5,50}, {8,0x100}, {32,0x404}, {33,7}});
        row({{0,67}, {1,7}, {4,11}, {8,0x100}, {15,1}, {32,0x404}, {33,40}}, {{16, {13}}});
        row({{0,69}, {1,7}, {4,11}, {8,0x100}, {15,1}, {32,0x404}, {33,40}}, {{16, {14}}});
        row({{0,70}, {1,7}, {4,11}, {8,0x100}, {15,1}, {32,0x404}, {33,40}}, {{16, {15}}});
        row({{0,71}, {1,7}, {4,11}, {8,0x100}, {13,4}, {32,0x40c}, {33,40}}, {{14, {3, 4, 5, 6}}});
        // Round 24: the steals - 1390 an HP steal on one (+0xbc 0x2004: +0xbd bit 5, always hits), 1391 on every enemy
        // (target 4), 1392 an MP steal (0x4004: bit 6, formula 20 - calculateMpDamage)
        row({{0,1390}, {1,2}, {4,3}, {5,100}, {8,0x101}, {32,0x2004}, {33,1}});
        row({{0,1391}, {1,2}, {4,4}, {5,100}, {8,0x101}, {32,0x2004}, {33,1}});
        row({{0,1392}, {1,2}, {4,3}, {5,100}, {8,0x101}, {32,0x4004}, {33,20}});
        // Round 25: 1759 se 34's revive (target 1, 100 % - damage 7 -, removes / requires KO); the turn-end script's 72 Special
        // Support (target 11, formula 17 -> damage 18: HP and MP, removes KO) and 74 (gives 88, always landing); 1512 "I can't
        // stop dancing!" (Dance's kind 120 command: on itself, no damage, list -8)
        row({{0,1759}, {1,3}, {4,1}, {5,100}, {8,int(0xffff0100)}, {15,1}, {21,1}, {32,4}, {33,7}}, {{16, {1}}, {22, {1}}});
        row({{0,72}, {1,7}, {4,11}, {5,100}, {8,int(0xffff0100)}, {15,1}, {25,uint32_t(-9)}, {32,0x404}, {33,17}}, {{16, {1}}});
        row({{0,74}, {1,7}, {4,11}, {8,int(0xffff0100)}, {13,1}, {32,0x40c}, {33,40}}, {{14, {88}}});
        row({{0,1512}, {1,7}, {4,0}, {8,int(0xffff0100)}, {25,uint32_t(-8)}, {32,4}, {33,40}});
        // Round 27: 1108 an always-critical physical one (+0xbc 0x34), 1111 a lethal physical one (power 30000), 1525 Braveheart
        // (status effect 109's kind 97 command: here giving its caster 88)
        row({{0,1108}, {1,0}, {4,3}, {5,10}, {8,0x101}, {32,0x34}, {33,1}, {34,1}});
        row({{0,1111}, {1,1}, {4,3}, {5,30000}, {8,0x101}, {32,4}, {33,1}, {34,1}});
        row({{0,1112}, {1,0}, {4,3}, {5,10}, {8,0x101}, {32,0x14}, {33,1}, {34,1}}); // (1112: one that can be critical)
        row({{0,1525}, {1,4}, {4,0}, {8,0x100}, {13,1}, {32,0xc}, {33,40}}, {{14, {88}}});
        Table formulas{"CommandFomula", "iiii", {
            {{0,21},{2,21},{3,uint32_t(-1)}},
            {{0,1},{2,1}}, {{0,2},{2,1},{3,3}}, {{0,5},{2,5}}, {{0,7},{2,7}},
            {{0,8},{2,8}}, {{0,12},{2,12}}, {{0,13},{2,13}}, {{0,16},{2,16}}, {{0,17},{2,18}},
            {{0,18},{2,6},{3,19}}, {{0,20},{2,20}}, {{0,24},{2,24}}, {{0,40},{2,uint32_t(-1)}},
            {{0,10},{2,10}}, {{0,30},{2,2}},
        }};
        check(t.command.parse(tables({commands, formulas})), "parse synthetic commands");
        Table skills{"Skill", "iiiiiiiiiiiiiiiiiiiii", {
            {{0,91}, {8,0}, {12,78}, {13,50}, {17,78}, {18,50}},   // +50% heal correction (kind 78)
            {{0,92}, {8,0}, {12,77}, {13,50}, {17,77}, {18,50}},   // +50% chocolat heal correction (kind 77)
            {{0,93}, {8,0}, {12,35}, {13,10}, {17,35}, {18,10}},   // 10% gravity resistance (CR3 kind 35)
            {{0,94}, {8,0}, {12,102}, {13,1}, {17,102}, {18,1}},   // Fix Element (kind 102)
            {{0,95}, {8,0}, {12,103}, {13,50}, {17,103}, {18,50}}, // Same-Element +50% / Off-Element -50% (kind 103)
            {{0,96}, {8,0}, {12,44}, {13,256}, {17,44}, {18,256}}, // +256/256 crit rate (CR3 kind 44)
            {{0,97}, {8,0}, {12,45}, {13,10}, {17,45}, {18,10}},   // +10% passive HP regen at endTurn (kind 45)
            {{0,98}, {8,0}, {12,70}, {13,1}, {17,70}, {18,1}},     // Break Reset on disadvantage (kind 70)
            {{0,99}, {8,0}, {12,80}, {13,200}, {17,80}, {18,200}}, // +200 cooperation chance (kind 80)
            {{0,421}, {8,0}, {12,116}, {13,200}, {17,116}, {18,200}}, // the Mimics' gold release x2.0 (kind 0x74)
            // Round 23: 479 the Eggons' fixed damage (trigger 4 - +0x1c -, chance 256, kind 91 -1); 329 a physical block
            // (kind 3, no trigger); 107 a pierce (kind 89); 143 field-effect immunity (kind 0x6a)
            {{0,479}, {7,4}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,0}, {10,6}, {11,256}, {12,91}, {13,uint32_t(-1)}, {16,256}, {17,91}, {18,uint32_t(-1)}},
            {{0,329}, {8,0}, {11,256}, {12,3}, {13,0}, {16,256}, {17,3}, {18,0}},
            {{0,107}, {8,0}, {11,256}, {12,89}, {13,0}, {16,256}, {17,89}, {18,0}},
            {{0,143}, {8,0}, {11,256}, {12,106}, {13,0}, {16,256}, {17,106}, {18,0}},
            // Round 25 (TriggerSkillToCalculation): 600 trigger 4 - kind 60 gives 3; 601 a Catalyst (trigger 1, end 1: kind 100
            // -> command 81, kind 60 -> 4); 602 as 426 (trigger 7, condition 3 at 0 % - KO'd by a hit -: kind 60 -> 34)
            {{0,600}, {7,4}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,0}, {10,6}, {11,256}, {12,60}, {13,3}, {16,256}, {17,60}, {18,3}},
            {{0,601}, {7,1}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,0}, {10,1}, {11,256}, {12,100}, {13,81}, {14,60}, {15,4}, {16,256}, {17,100}, {18,81}, {19,60}, {20,4}},
            {{0,602}, {7,7}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,3}, {9,0}, {10,6}, {11,256}, {12,60}, {13,34}, {16,256}, {17,60}, {18,34}},
            {{0,603}, {8,0}, {11,256}, {12,118}, {13,1100}, {16,256}, {17,118}, {18,1100}}, // (as 435: Cover of command 1100)
            // Round 27, as the release's rows (4 / 5 / 6 the uses a battle / turn / command, 7 trigger, 8 condition, 9 its constant,
            // 10 end timing, 11 / 16 chance): 1 Critical x2 (trigger 3, condition 4 - the unit's attack -, end 9: kind 69 x200);
            // 2 Critical First Attack (the same, once a battle: kind 139); 127 Afterthought Guard (trigger 10, condition 4, end 9:
            // kind 60 -> 2; chance 256 here, 77 in the release); 134 Narrow Escape (trigger 5, condition 29 at 100 %, end 6, once a
            // battle and a command: kind 53 1, kind 60 -> 109); 126 Emergency Guard (trigger 6, end 5: kind 60 -> 2, nothing with
            // the advantage); 452 as the Death Mask's (no trigger: kind 61 -> 88, with the advantage kind 60 -> 88); 604 no trigger,
            // kind 60 -> 301 (two accumulations at most); 113 as the release's (trigger 12, condition 24 - day -, end 11: kind 45 10)
            {{0,1}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {7,3}, {8,4}, {10,9}, {11,256}, {12,69}, {13,200}, {16,256}, {17,69}, {18,200}},
            {{0,2}, {4,1}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {7,3}, {8,4}, {10,9}, {11,256}, {12,139}, {16,256}, {17,139}},
            {{0,127}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {7,10}, {8,4}, {10,9}, {11,256}, {12,60}, {13,2}, {16,256}, {17,60}, {18,2}},
            {{0,134}, {4,1}, {5,uint32_t(-1)}, {6,1}, {7,5}, {8,29}, {9,100}, {10,6}, {11,256}, {12,53}, {13,1}, {14,60}, {15,109},
             {16,256}, {17,53}, {18,1}, {19,60}, {20,109}},
            {{0,126}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {7,6}, {8,0}, {10,5}, {11,256}, {12,60}, {13,2}, {16,256}},
            {{0,452}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {11,256}, {12,61}, {13,88}, {16,256}, {17,60}, {18,88}},
            {{0,604}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {11,256}, {12,60}, {13,301}, {16,256}, {17,60}, {18,301}},
            {{0,113}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {7,12}, {8,24}, {10,11}, {11,256}, {12,45}, {13,10}, {16,256}, {17,45}, {18,10}},
        }};
        Table statusEffects{"StatusEffect", "iiiiiiiiiiiiiiiiiiiiiii_ipiiiiiiii", {
            {{0,1}, {1,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {12,256}, {13,256}},
            {{0,200}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,129}, {15,80}}, // MP cost 80% (kind 129)
            {{0,301}, {1,10}, {3,1}, {5,3}, {6,3}, {7,2}, {12,256}, {13,256}, {14,37}, {15,50}, {32,0x10}}, // group 10, prio 1, maxAcc 2, +50% ATK (37), CanCancel (bit 0x10)
            {{0,302}, {1,10}, {3,2}, {5,3}, {6,3}, {7,1}, {12,256}, {13,256}, {14,37}, {15,100}, {32,0x10}}, // group 10, prio 2, +100% ATK, CanCancel
            {{0,303}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,68}, {15,301}}, // status immunity to 301 (kind 68)
            {{0,304}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,82}, {15,300}}, // +300/256 evasion (CR3 kind 82)
            {{0,305}, {1,uint32_t(-1)}, {5,9}, {6,9}, {12,256}, {13,256}, {14,36}, {15,120}}, // +20% attack for 9 turns (kind 36)
            {{0,306}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,51}, {15,1}},   // berserk - attacks at random (kind 51)
            // Round 23, as the release's skill_para StatusEffect rows (4 uses +0x10, 8 trigger +0x20, 9 condition +0x24,
            // 10 its constant, 11 end timing +0x2c): 17 Mental Wall (4 / magic, kind 49, 1 use, end 10), 18 Moonlight
            // Shadow (4 / physical, kind 123), 23 Rally (3 / physical, kind 54 x200, end 9), 25 Counter (8 / physical,
            // kind 99 -> 56, 1 turn, end 7), 92 Regenerate (11 / HP at 0 %, kind 98 -> 306, end 10), 46 the field's
            // speed x0.66 (no trigger), 400 an assist-cost halving (kind 79); 3 .. 6 and 13 .. 15 plain ones
            {{0,17}, {1,uint32_t(-1)}, {4,1}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,4}, {9,7}, {11,10}, {12,256}, {13,256}, {14,49}, {32,0x21}},
            {{0,18}, {1,uint32_t(-1)}, {4,1}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,4}, {9,6}, {11,10}, {12,256}, {13,256}, {14,123}, {32,0x21}},
            {{0,23}, {1,uint32_t(-1)}, {4,1}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,3}, {9,6}, {11,9}, {12,256}, {13,256}, {14,54}, {15,200}, {32,1}},
            {{0,25}, {1,uint32_t(-1)}, {4,1}, {5,1}, {6,1}, {8,8}, {9,6}, {11,7}, {12,256}, {13,256}, {14,99}, {15,56}, {32,1}},
            {{0,92}, {1,uint32_t(-1)}, {4,1}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,11}, {9,3}, {10,0}, {11,10}, {12,256}, {13,256}, {14,98}, {15,306}, {16,128}, {32,1}},
            {{0,46}, {1,uint32_t(-1)}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {12,256}, {13,256}, {14,40}, {15,66}},
            {{0,400}, {1,uint32_t(-1)}, {4,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {12,256}, {13,256}, {14,79}, {15,0}},
            {{0,3}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}}, {{0,4}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}},
            {{0,5}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}}, {{0,6}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}},
            {{0,13}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}}, {{0,14}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}},
            {{0,15}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}},
            // Round 25: 34 (4 turns, trigger 12 at its turns at or under 1 - condition 17 -, end 11: kind 101 -> 1759); 88 a plain
            // one; 56 Dance (group 12, 3 turns, trigger 3, end 3: kind 120 -> 1512); 500 / 501 / 502 counters of triggers 7 / 8 / 9
            // (5 uses, no end)
            {{0,34}, {1,34}, {2,uint32_t(-1)}, {4,uint32_t(-1)}, {5,4}, {6,4}, {8,12}, {9,17}, {10,1}, {11,11}, {12,256}, {13,256}, {14,101}, {15,1759}},
            {{0,88}, {1,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {12,256}, {13,256}},
            {{0,56}, {1,12}, {2,uint32_t(-1)}, {4,uint32_t(-1)}, {5,3}, {6,3}, {8,3}, {11,3}, {12,256}, {13,256}, {14,120}, {15,1512}},
            {{0,500}, {1,uint32_t(-1)}, {4,5}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,7}, {12,256}, {13,256}},
            {{0,501}, {1,uint32_t(-1)}, {4,5}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,8}, {12,256}, {13,256}},
            {{0,502}, {1,uint32_t(-1)}, {4,5}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {8,9}, {12,256}, {13,256}},
            // Round 27: 2 Guard (1 turn: kind 34 x50); 109 Narrow Escape's (1 use, 1 turn, trigger 8 at HP 1 or under - condition
            // 22 -, end 7: kind 97 -> 1525 Braveheart)
            {{0,2}, {1,uint32_t(-1)}, {5,1}, {6,1}, {12,256}, {13,256}, {14,34}, {15,50}},
            {{0,109}, {1,26}, {4,1}, {5,1}, {6,1}, {8,8}, {9,22}, {10,1}, {11,7}, {12,256}, {13,256}, {14,97}, {15,1525}},
        }};
        check(t.skill.parse(tables({skills, statusEffects})), "parse synthetic skills and status effects");
        Table elements{"Element", "ifffffff", {
            {{0,0},{1,bits(1)},{2,bits(1)},{3,bits(1)},{4,bits(1)},{5,bits(1)},{6,bits(1)},{7,bits(1)}},
            {{0,1},{1,bits(1)},{2,bits(0.5f)},{3,bits(1.5f)},{4,bits(1)},{5,bits(1)},{6,bits(1)},{7,bits(1)}},
            {{0,2},{1,bits(1)},{2,bits(1.5f)},{3,bits(0.5f)},{4,bits(1)},{5,bits(1)},{6,bits(1)},{7,bits(1)}},
        }};
        // (battle.mbe OrderType row 0 and ThinkPointAddition row 2, as the release's)
        Table orderTypes{"OrderType", "iiiiiiiiiiii", {{{0,0},{1,60},{2,40},{3,40},{4,25},{5,10},{6,10},{7,10},{8,5},{9,51},{10,51},{11,51}}}};
        Table thinkAdd{"ThinkPointAddition", "iiiiiiiii", {{{0,0},{5,30}}, {{0,1},{4,20},{5,50}}, {{0,2},{8,50}}}};
        check(t.battle.parse(tables({elements, orderTypes, thinkAdd})), "parse synthetic element rates and think points");
        // monster 2's modes (MonsterCommand: id, mode, element, card, actions a turn, attack, last attack, finest arts,
        // (command, cost, uses) x 8): 0 - 900 twice at most, 901; 1 - the buff 902, 901; 2 - 901, two actions a turn;
        // 3 - 901 alone
        auto modeRow = [](int mode, int actions, std::vector<std::array<int, 3>> entries) {
            std::map<int, uint32_t> row{{0,2}, {1,uint32_t(mode)}, {2,uint32_t(-1)}, {3,1}, {4,uint32_t(actions)}, {5,901}, {6,uint32_t(-1)}, {7,uint32_t(-1)}};
            for (size_t k = 0; k < 8; ++k) {
                const std::array<int, 3> e = k < entries.size() ? entries[k] : std::array<int, 3>{-1, 1, -1};
                for (size_t c = 0; c < 3; ++c) row[int(8 + k * 3 + c)] = uint32_t(e[c]);
            }
            for (int k = 32; k < 40; ++k) row[k] = uint32_t(-1);
            return row;
        };
        Table monsterCommands{"MonsterCommand", "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", {
            modeRow(0, 1, {{900, 1, 2}, {901, 1, -1}}), modeRow(1, 1, {{902, 1, -1}, {901, 1, -1}}),
            modeRow(2, 2, {{901, 1, -1}}), modeRow(3, 1, {{901, 1, -1}}),
        }};
        Table monsters{"Monster", "iipiiiiiiiiiiiiiiiiiii", {{{0,1},{4,1},{6,5000},{7,100},{8,0x00010014},{9,0x00010001}},
                                                             {{0,2},{4,1},{6,5000},{7,100},{8,0x00010014},{9,0x00010001}}}};
        check(t.monster.parse(tables({monsters, monsterCommands})), "parse synthetic fighters");
        { // Round 16: BtlUnit::PlayCamera / updateWhiteFade - model.mbe AnimDirection's camera (+0x08) and white-fade frame (+0x24)
            auto text = [](const char* str) {
                std::vector<uint32_t> w((std::strlen(str) + 4) / 4, 0);
                std::memcpy(w.data(), str, std::strlen(str));
                return w;
            };
            Table animDir{"AnimDirection", "iippiiff", {{{0, 1}, {1, 19}, {7, bits(80)}}, {{0, 1}, {1, 18}, {7, bits(-1)}}},
                          {{{2, text("Cam_t02_j001_00")}}, {{2, text("Cam_t01_j001_00")}}}};
            check(t.model.parse(tables({animDir})), "parse synthetic AnimDirection");
            float white = 0;
            check(t.unitCamera(1, 19, &white) == "Cam_t02_j001_00" && white == 80, "a motion's own camera and its white-fade frame");
            check(t.unitCamera(1, 18, &white) == "Cam_t01_j001_00" && white < 0, "no white-fade frame: -1");
            check(t.unitCamera(1, 20, &white).empty() && white < 0, "no row: no camera");
        }
        cr3::Cr3Battle battle;
        for (int k = 0; k < 4; ++k) { cr3::Cr3BattleUnit u; u.monster = 1; battle.units.push_back(u); }
        auto run = [&](int command, int target) {
            cr3::Cr3BattleRules rules; check(rules.setup(t,battle,1,42), "setup synthetic battle");
            for (size_t k = 0; k < rules.fighters.size(); ++k) {
                auto& f = rules.fighters[k]; f.player = k < 2; f.attackCommand = -1; f.speed = k == 0 ? 100 : 1;
                f.hp = 500;
            }
            const auto actions = rules.turn({{0,command,target},{1,-1,-1}});
            for (const auto& a : actions) if (a.source == 0 && a.command == command) {
                std::vector<int> hit;
                for (const auto& h : a.targets) {
                    hit.push_back(h.target);
                    check(command == 81 || command == 91 ? rules.fighters[size_t(h.target)].hp > 500 : rules.fighters[size_t(h.target)].hp < 500,
                          "selected target receives command's real healing/damage");
                }
                return hit;
            }
            throw std::runtime_error("chosen action missing");
        };
        check(run(80,1) == std::vector<int>{1}, "flexible single attack can target a party member");
        check(run(80,2) == std::vector<int>{2}, "flexible single attack targets chosen enemy");
        check(run(81,2) == std::vector<int>{2}, "flexible healing command can target the enemy side");
        check(run(90,1) == std::vector<int>({0,1}), "flexible all-target attack honors chosen party side");
        check(run(90,2) == std::vector<int>({2,3}), "flexible all-target attack honors chosen enemy side");
        check(run(91,2) == std::vector<int>({2,3}), "flexible all-target heal honors chosen enemy side");
        check(run(90,-1) == std::vector<int>({2,3}), "unspecified flexible all-target attack keeps default enemy side");
        check(run(91,-1) == std::vector<int>({0,1}), "unspecified flexible all-target heal keeps default party side");
        check(run(50,-1) == std::vector<int>({0,1,2,3}), "unspecified everyone command retains everyone");
        check(run(50,2) == std::vector<int>({2,3}), "selected everyone command uses selected side as native target menu does");
        check(run(30,1).at(0) >= 2, "fixed enemy attack cannot select friendly side");

        // Audited BtlCommandList::Sort (priority buckets 0/2/5, Guard priority +2, speed - weight + speedCost, reverseSpeed).
        {
            cr3::Cr3BattleRules rules; check(rules.setup(t, battle, 1, 99), "setup turn-order battle");
            for (size_t k = 0; k < 4; ++k) {
                auto& f = rules.fighters[k];
                f.player = true; f.hp = 1000; f.maxHp = 1000; f.attackCommand = 30;
            }
            rules.fighters[0].speed = 100; // bucket -8 (late)
            rules.fighters[1].speed = 90;  // bucket -9 (last)
            rules.fighters[2].speed = 20;  // slow fighter, but uses +80 speedCost command 60 -> effective 100
            rules.fighters[3].speed = 50;  // Guard (-1) -> priority +2 from command 51
            rules.fighters.push_back(rules.fighters[0]);
            rules.fighters[4].player = false; rules.fighters[4].speed = 60; rules.fighters[4].attackCommand = 30;
            rules.fighters[4].commands = {{30, 1, -1}}; // (its AI's one command - MonsterCommand's)
            const auto order = rules.turn({{0, 61, 4}, {1, 62, 4}, {2, 60, 4}, {3, -1, -1}});
            std::vector<int> sources;
            for (const auto& a : order) sources.push_back(a.source);
            check(sources == std::vector<int>({3, 2, 4, 0, 1}),
                  "BtlCommandList::Sort orders Guard (+2) > speed+speedCost (100) > normal (60) > bucket -8 > bucket -9");

            rules.reverseSpeed = true; rules.reverseSpeedTurns = 5; // (not over this turn: no command 58 at its end)
            const auto rev = rules.turn({{0, 30, 4}, {1, 30, 4}, {2, 30, 4}, {3, -1, -1}});
            std::vector<int> revSources;
            for (const auto& a : rev) revSources.push_back(a.source);
            check(revSources.front() == 3 && revSources[1] == 2 && revSources.back() == 0,
                  "reverseSpeed inverts effective speed tie-break while keeping higher priority first");
        }

        // Audited BtlDamageUI popup colors (1 weak, 2 resist, 3 HP heal), per-hit chain scaling, and bootCharacter 10x.
        {
            cr3::Cr3BattleRules rules; check(rules.setup(t, battle, 1, 7), "setup damage-audit battle");
            for (size_t k = 0; k < 4; ++k) {
                auto& f = rules.fighters[k];
                f.player = k < 2; f.hp = 20000; f.maxHp = 50000; f.attack = 120; f.defence = 20; f.luck = 10;
                f.speed = 100 - int(k) * 10; f.attackCommand = 30;
            }
            rules.fighters[2].element = 2; // Ice: weak to Fire (element 1 -> 150%)
            rules.fighters[3].element = 1; // Fire: resists Fire (element 1 -> 50%)
            rules.monstersWait = true;
            const auto elemActions = rules.turn({{0, 1070, 2}, {1, 1070, 3}});
            check(elemActions.size() == 2 && elemActions[0].targets[0].colors == std::vector<uint8_t>{1}, "elemental weakness records popup color 1");
            check(elemActions[1].targets[0].colors == std::vector<uint8_t>{2}, "elemental resistance records popup color 2");

            rules.monstersWait = true;
            const auto healActions = rules.turn({{0, 81, 0}, {1, -1, -1}});
            check(!healActions.empty() && healActions[1].targets[0].colors == std::vector<uint8_t>{3}, "HP heal records popup color 3");

            rules.monstersWait = true;
            const auto chainActions = rules.turn({{0, 1071, 2}, {1, -1, -1}});
            const auto& hits = chainActions[1].targets[0].hits;
            check(hits.size() == 3 && hits[2] > hits[0], "multi-hit repeat command escalates chain bonus per hit");

            cr3::Cr3BattleRules bootRules = rules;
            bootRules.bootCharacter = true;
            rules.monstersWait = true;
            bootRules.monstersWait = true;
            const auto normalHit = rules.turn({{0, 30, 2}, {1, -1, -1}})[1].targets[0].hits[0];
            const auto bootHit = bootRules.turn({{0, 30, 2}, {1, -1, -1}})[1].targets[0].hits[0];
            check(bootHit >= normalHit * 8, "bootCharacter multiplies player basic damage by ~10x");
        }

        // Round 21: Steal Gold (calculateStealGold 0x7e20e8) - the Mimic example: a physical 350 against 1000 ML -
        // 350 taken at once, 650 left, no HP lost, the thief carrying 350; at victory 1 + int(2.0 x 350) = 701
        {
            cr3::Cr3BattleRules rules; check(rules.setup(t, battle, 1, 11), "setup steal battle");
            for (size_t k = 0; k < 4; ++k) {
                auto& f = rules.fighters[k];
                f.player = k < 2; f.hp = f.maxHp = 5000; f.attack = 20; f.defence = 20; f.luck = 10; f.geneLevel = 0;
                f.speed = 100 - int(k) * 10; f.attackCommand = 30; f.skills.clear();
            }
            rules.fighters[0].attack = 100, rules.fighters[0].skills = {421}; // (ratio 5 - its top - x power 140 x 0.5 = 350)
            rules.gold = 1000, rules.monstersWait = true;
            cr3::Cr3Hit stole;
            for (const auto& a : rules.turn({{0, 1647, 2}, {1, -1, -1}}))
                if (a.source == 0 && !a.targets.empty()) stole = a.targets[0];
            check(stole.stolen == 350 && rules.gold == 650 && stole.hits == std::vector<int>{0} && rules.fighters[2].hp == 5000,
                  "Steal Gold: min(350, 1000) taken, 650 left, no HP lost");
            check(rules.fighters[0].stolenGold == 350, "Steal Gold: the thief carries what it took (AddGold)");
            float k = 1;
            for (int c : rules.skillConstants(rules.fighters[0], 0x74)) k *= float(c) / 100;
            check(1 + int(k * float(rules.fighters[0].stolenGold)) == 701, "Steal Gold: VAcquireMoney gives back 1 + 2.0 x 350 = 701");
            rules.gold = 200, rules.monstersWait = true;
            for (const auto& a : rules.turn({{0, 1647, 2}, {1, -1, -1}}))
                if (a.source == 0 && !a.targets.empty()) stole = a.targets[0];
            check(stole.stolen == 200 && rules.gold == 0, "Steal Gold: no more than the party has");
            rules.monstersWait = true;
            for (const auto& a : rules.turn({{0, 1647, 2}, {1, -1, -1}}))
                if (a.source == 0 && !a.targets.empty()) stole = a.targets[0];
            check(stole.stolen == 0 && rules.gold == 0, "Steal Gold: the party broke - 0 (AnnounceBattle 0x29)");
        }

        // Round 2 audited formulas: mpCost double truncation & % current MP, hpCost % current HP + formula 13,
        // Potion constant heal + Chocolat bonus + color 3, Heal correction (78), Ether/Elixir MP & HP+MP heals,
        // Gravity (8), MP damage (20), Constant HP=1 (24), and Revive KO-only target eligibility.
        {
            cr3::Cr3BattleRules rules; check(rules.setup(t, battle, 1, 123), "setup round-2 formula battle");
            for (size_t k = 0; k < 4; ++k) {
                auto& f = rules.fighters[k];
                f.player = k < 2;
                f.hp = 400; f.maxHp = 1000;
                f.mp = 37;  f.maxMp = 100;
                f.attack = 100; f.defence = 100; f.speed = 100 - int(k) * 10; f.luck = 10;
                f.geneLevel = 40;
                f.attackCommand = 30;
            }
            cr3::Cr3Command c203, c302, c303, c511, c514, c517;
            check(rules.command(203, c203) && rules.command(302, c302) && rules.command(303, c303) &&
                  rules.command(511, c511) && rules.command(514, c514) && rules.command(517, c517),
                  "lookup round-2 synthetic commands");
            check(c514.usable() && c517.usable() && c302.usable() && c203.usable(), "MP/HP+MP/Revive/HP-cost commands are usable");

            // mpCost kind 1 (100% current MP, min 1) and kind 3 double truncation (5 * 0.5 -> 2, * 0.8 -> 1).
            check(rules.mpCost(rules.fighters[0], c303) == 37, "mpKind 1 costs 100% of current MP");
            rules.fighters[0].mp = 0;
            check(rules.mpCost(rules.fighters[0], c303) == 1, "mpKind 1 clamps cost to at least 1");
            rules.fighters[0].mp = 50;
            rules.fighters[0].consumption = 0.5f;
            rules.giveEffect(0, 200); // kind 129: 80% MP cost
            check(rules.mpCost(rules.fighters[0], c203) == 1, "mpKind 3 truncates weapon consumption before status MP discount (5->2->1)");

            // Heal skill kind 78 (+50% heal correction).
            const int baseHeal = rules.heal(rules.fighters[0], cr3::Cr3Command{81, 100});
            rules.fighters[0].skills = {91};
            const int boostedHeal = rules.heal(rules.fighters[0], cr3::Cr3Command{81, 100});
            check(boostedHeal == int(float(baseHeal) * 1.5f), "skill kind 78 scales calculateHpHeal by +50%");

            // Potion (511: damage 6, chain 19) heals constant 100 HP with color 3, and 150 HP with target skill 77.
            rules.monstersWait = true;
            auto potAct = rules.turn({{0, 511, 1}, {1, -1, -1}});
            check(potAct[1].targets[0].hits == std::vector<int>{-100} && potAct[1].targets[0].colors == std::vector<uint8_t>{3} &&
                  rules.fighters[1].hp == 500,
                  "constant HP heal restores exact power with popup color 3 (not MP color 4)");
            rules.fighters[1].skills = {92};
            rules.monstersWait = true;
            potAct = rules.turn({{0, 511, 1}, {1, -1, -1}});
            check(potAct[1].targets[0].hits == std::vector<int>{-150} && rules.fighters[1].hp == 650,
                  "chain 19 applies target GetChocolatHealCorrection (skill kind 77)");

            // Ether (514: damage 16) restores 60 MP with color 4 without hurting HP; Elixir (517: damage 18) restores 100% HP and MP.
            rules.fighters[1].mp = 10;
            rules.monstersWait = true;
            const auto etherAct = rules.turn({{0, 514, 1}, {1, -1, -1}});
            check(etherAct[1].targets[0].hits == std::vector<int>{0} && etherAct[1].targets[0].mpHits == std::vector<int>{-60} &&
                  etherAct[1].targets[0].colors == std::vector<uint8_t>{4} && rules.fighters[1].hp == 650 && rules.fighters[1].mp == 70,
                  "Ether restores MP with popup color 4 instead of dealing physical damage");
            rules.fighters[1].hp = 120; rules.fighters[1].mp = 15;
            rules.monstersWait = true;
            const auto elixirAct = rules.turn({{0, 517, 1}, {1, -1, -1}});
            check(elixirAct[1].targets[0].hits == std::vector<int>{-1000} && elixirAct[1].targets[0].mpHits == std::vector<int>{-100} &&
                  rules.fighters[1].hp == 1000 && rules.fighters[1].mp == 100,
                  "Elixir restores both HP and MP to 100%");

            // Revive (302: removes {1}, requiredEffects {1}) rejects living allies and revives KO'd allies to 50% Max HP.
            check(!rules.targetable(1, &c302), "Revive cannot target a living ally lacking KO status");
            rules.fighters[1].hp = 0;
            check(rules.targetable(1, &c302), "Revive targets a fallen ally");
            rules.monstersWait = true;
            const auto revAct = rules.turn({{0, 302, 1}});
            check(revAct.size() == 1 && rules.fighters[1].hp == 500 && !rules.fighters[1].dead(),
                  "Revive restores fallen ally to 50% of max HP");

            // HP-consumption physical (203: 50% current HP -> (0.5 * 4 + 1) = 3x basic damage).
            rules.fighters[0].hp = 800; rules.fighters[0].maxHp = 800;
            rules.fighters[2].hp = 5000; rules.fighters[2].maxHp = 5000;
            const int plain = rules.damage(rules.fighters[0], rules.fighters[2], c203);
            rules.monstersWait = true;
            const auto hpConsAct = rules.turn({{0, 203, 2}, {1, -1, -1}});
            check(rules.fighters[0].hp == 400 && hpConsAct[1].targets[0].hits[0] == plain * 3,
                  "hpKind 1 deducts 50% current HP and scales formula 13 damage by 3x");

            // Gravity (800: 25% current HP less target skill 35's 10% = 15%), MP damage (1646), and Constant HP=1 (1650).
            rules.fighters[2].hp = 2000; rules.fighters[2].mp = 80; rules.fighters[2].skills = {93};
            rules.monstersWait = true;
            cr3::Cr3BattleRules probe = rules; // (calculateMpDamage 0x7e2010: calculateMagicDamage's own number, at the gauge then)
            const auto gravAct = rules.turn({{0, 800, 2}, {1, 1646, 2}});
            check(gravAct[0].targets[0].hits == std::vector<int>{300} && rules.fighters[2].hp == 1700,
                  "calculatePercentDamage applies (power - skill 35)% of current HP");
            cr3::Cr3Command c1646;
            check(probe.command(1646, c1646), "command 1646");
            probe.gauge = gravAct[0].gaugeAfter, probe.gaugeStarted = true;
            const int mpWant = std::min(probe.damage(probe.fighters[1], probe.fighters[2], c1646), 80);
            check(mpWant > 0 && mpWant != 30 && gravAct[1].targets[0].mpHits == std::vector<int>{mpWant} &&
                      gravAct[1].targets[0].colors == std::vector<uint8_t>{4} && rules.fighters[2].mp == 80 - mpWant,
                  "calculateMpDamage drains the magic damage's number from the target's MP (at most its MP), popup color 4");
            rules.monstersWait = true;
            const auto constHpAct = rules.turn({{0, 1650, 2}, {1, -1, -1}});
            check(constHpAct[1].targets[0].hits == std::vector<int>{1699} && rules.fighters[2].hp == 1 && !rules.fighters[2].dead(),
                  "calculateConstantHp reduces target HP to 1 without killing");
        }

        // Round 3 audited parity: Unison single execution & bucket0 restriction, Same-Element (103) + Element Shift + Fix Element (102),
        // status group priority/stacking (37) + immunity (68) vs flags=8, Formula 12 extraType 18, CR3 evasion (82) & crit (44),
        // mid-turn HaveEnoughMp abort, passive regen (45), Break Reset (70), and extraType 13 Break force-set.
        {
            cr3::Cr3BattleRules rules; check(rules.setup(t, battle, 1, 555), "setup round-3 parity battle");
            for (size_t k = 0; k < 4; ++k) {
                auto& f = rules.fighters[k];
                f.player = k < 2;
                f.hp = 5000; f.maxHp = 5000;
                f.mp = 100;  f.maxMp = 100;
                f.attack = 100; f.defence = 50; f.speed = 100 - int(k) * 5; f.luck = 0;
                f.element = 0; f.geneLevel = 30; f.attackCommand = 30;
            }

            // 1. Unison executes single action with 1.2x scaling (not 2x per-partner loop), and bucket -8 never joins unison.
            rules.monstersWait = true;
            const auto soloAct = rules.turn({{0, 720, 2}, {1, -1, -1}});
            const int soloDmg = soloAct[1].targets[0].hits[0];
            // Reset fighter 0 element, fighter 2 magic chain combo, and break gauge before unison test!
            check(rules.fighters[0].element == 1, "casting Fire spell shifts caster element to Fire (1) after damage");
            rules.fighters[0].element = 0; rules.fighters[1].element = 0;
            rules.fighters[2].combo = 0;
            rules.gauge = 0; rules.gaugeStarted = false;
            rules.fighters[0].skills = {99}; // guarantee cooperation roll
            rules.fighters[2].hp = 5000;
            rules.monstersWait = true;
            const auto uniAct = rules.turn({{0, 720, 2}, {1, 720, 2}});
            rules.fighters[0].skills.clear();
            check(uniAct.size() == 1 && uniAct[0].partners == std::vector<int>{1} && uniAct[0].targets[0].hits.size() == 1,
                  "2-caster unison produces a single 1-hit action (not duplicated per partner)");
            check(uniAct[0].targets[0].hits[0] >= soloDmg * 3 && uniAct[0].targets[0].hits[0] <= soloDmg * 3 + 2,
                  "2-caster unison scales damage once by coopScale_[2] (3x synthetic default, not 6x)");
            check(rules.fighters[0].element == 1 && rules.fighters[1].element == 1,
                  "unison shifts both source and partner elements after calculation");
            rules.monstersWait = true;
            const auto bucketAct = rules.turn({{0, 61, 2}, {1, 61, 2}});
            check(bucketAct.size() == 2 && bucketAct[0].partners.empty(), "bucket -8 commands do not form a unison");

            // 2. Fix Element (102) prevents element shift; Same-Element (103) adds +50% on match and -50% on mismatch.
            rules.fighters[0].element = 1; // Fire
            rules.fighters[0].skills = {94, 95}; // Fix Element + Same-Element 50%
            cr3::Cr3Command cFire, cIce;
            check(rules.command(720, cFire) && rules.command(721, cIce), "lookup Fire/Ice spells");
            const int sameDmg = rules.damage(rules.fighters[0], rules.fighters[2], cFire);
            const int offDmg = rules.damage(rules.fighters[0], rules.fighters[2], cIce);
            check(sameDmg > offDmg * 2, "Same-Element (103) boosts matching element (+50%) and penalizes off-element (-50%)");
            rules.monstersWait = true;
            rules.turn({{0, 721, 2}, {1, -1, -1}});
            check(rules.fighters[0].element == 1, "Fix Element (102) prevents Ice spell from shifting caster element");
            rules.fighters[0].skills.clear();

            // 3. StatusEffect full layout: stacking up to maxAccumulation=2, priority override, odd stat mult (37), and immunity (68) vs flags=8.
            rules.giveEffect(0, 301);
            rules.giveEffect(0, 301);
            rules.giveEffect(0, 301); // capped at maxAccumulation=2 -> +100% ATK (200)
            check(rules.attackOf(rules.fighters[0]) == 200, "status 301 stacks twice (maxAccumulation=2) for +100% Attack");
            rules.giveEffect(0, 302); // higher priority 2 in group 10 cancels both 301 stacks -> +100% ATK (200)
            check(!rules.hasEffect(0, 301) && rules.hasEffect(0, 302), "higher-priority group status cancels lower-priority stacks");
            rules.giveEffect(2, 303); // immune to 301
            rules.monstersWait = true;
            rules.turn({{0, 723, 2}, {1, -1, -1}});
            check(!rules.hasEffect(2, 301), "status immunity (68) blocks guaranteed flags=8 status application");

            // 4. Formula 12 extraType 18 (+20 power per matching status on target).
            rules.fighters[2].effects.clear();
            cr3::Cr3Command c722;
            check(rules.command(722, c722), "lookup formula 12 command");
            const int f12Base = rules.damage(rules.fighters[0], rules.fighters[2], c722);
            rules.giveEffect(2, 302);
            const int f12Boost = rules.damage(rules.fighters[0], rules.fighters[2], c722);
            check(f12Boost > f12Base, "Formula 12 extraType 18 adds +20 power per matching status on target");

            // 5. CR3 evasion status (82) forces miss, and CR3 crit skill (44) forces critical hit.
            rules.fighters[2].effects.clear();
            rules.giveEffect(2, 304); // +200% evasion
            rules.monstersWait = true;
            const auto missAct = rules.turn({{0, 30, 2}, {1, -1, -1}});
            check((missAct[1].targets[0].flags[0] & 4) != 0, "CR3 evasion status kind 82 causes physical attack to miss");
            rules.fighters[2].effects.clear();
            rules.fighters[0].skills = {96}; // +256/256 crit (kind 44)
            rules.monstersWait = true;
            const auto critAct = rules.turn({{0, 722, 2}, {1, -1, -1}});
            check((critAct[1].targets[0].flags[0] & 1) != 0, "CR3 crit skill kind 44 triggers critical hit");

            // 6. Mid-turn HaveEnoughMp abort when faster enemy drains MP before slower player's spell.
            rules.fighters[0].skills.clear();
            rules.fighters[0].mp = 25; // needs 20 MP for 720
            rules.fighters[0].speed = 10;
            rules.fighters[2].speed = 200; // faster monster drains 30 MP via scripted 1646
            rules.scripted[2].push_back({1, 1646});
            rules.fighters[1].hp = 0; // keep fighter 1 out so monster 2 targets fighter 0
            rules.fighters[3].hp = 0; // keep monster 3 out so only monster 2 & fighter 0 act
            const auto drainTurn = rules.turn({{0, 720, 2}});
            check(rules.fighters[0].mp == 0 && drainTurn.size() == 2 && drainTurn[1].source == 0 && drainTurn[1].command == -2,
                  "player spell aborts mid-turn (command -2) when faster enemy drains MP below mpCost");

            // 7. Passive HP regen (45) in endTurn, extraType 13 Break force-set, and Break Reset (70) on disadvantage.
            rules.fighters[1].hp = 1000; rules.fighters[1].maxHp = 5000; rules.fighters[1].skills = {97}; // +10% regen -> +500 HP
            const auto endTicks = rules.endTurn();
            check(rules.fighters[1].hp == 1500 && !endTicks.empty(), "passive skill kind 45 regenerates 10% maxHp at endTurn");
            rules.fighters[0].speed = 100; rules.fighters[1].speed = 90;
            rules.monstersWait = true;
            rules.turn({{0, 30, 2}, {1, 724, 2}});
            check(rules.gauge == 777, "extraType 13 force-sets Break Gauge to extraConstants[0]");
            rules.gauge = -400; // enemy advantage -> player disadvantage
            rules.fighters[0].skills = {98}; // Break Reset (70)
            rules.monstersWait = true;
            // (round 28: SetBreakPointToFront - the first target's first landed hit's points -gauge, through BtlBreakPoint::Add:
            // -400 + 400 reaches 0 from below, a break - +500 first -, so 500 after that hit; not zeroed after the action)
            const auto resetTurn = rules.turn({{0, 30, 2}, {1, -1, -1}});
            const cr3::Cr3Action* resetAct = nullptr;
            for (const auto& a : resetTurn)
                if (a.source == 0) resetAct = &a;
            check(resetAct && !resetAct->targets.empty() && !resetAct->targets[0].gauge.empty() &&
                      !(resetAct->targets[0].flags[0] & 4) && resetAct->targets[0].gauge[0] == 500 && resetAct->broke,
                  "passive skill kind 70 sets the first landed hit's break points to -gauge (a break from -400 to 500)");
            rules.gauge = -400, rules.fighters[0].skills.clear();
            const auto plainTurn = rules.turn({{0, 30, 2}, {1, -1, -1}});
            for (const auto& a : plainTurn)
                if (a.source == 0)
                    check(!a.targets.empty() && !a.targets[0].gauge.empty() && a.targets[0].gauge[0] < 0,
                          "without kind 70 the first hit adds its usual points");
        }
        // Round 10: BtlCommandManager::addLastAttack - two linked actions (the second a combo on the downed
        // target), the last's command able to lead into one, the next from the other side: the last
        // attacker's Last Attack before it, marked; a gauge at the side's full advantage and none with
        // Finest Arts: still the Last Attack; one action alone, or no combo: none
        {
            cr3::Cr3BattleRules rules; check(rules.setup(t, battle, 1, 31), "setup finisher battle");
            auto reset = [&] {
                for (size_t k = 0; k < 4; ++k) {
                    auto& f = rules.fighters[k];
                    f.player = k < 2, f.hp = f.maxHp = 5000, f.mp = f.maxMp = 100, f.attack = 50, f.defence = 50, f.luck = 0;
                    f.speed = k < 2 ? 10 : 200 - int(k), f.attackCommand = 30, f.comboChance = false, f.combo = 0, f.chain = 0;
                    f.effects.clear(), f.lastAttack = k >= 2 ? 761 : -1, f.finestArts = -1;
                }
                rules.fighters[1].hp = 0; // (one to hit: the party's first)
                rules.scripted.clear(), rules.monstersWait = false;
            };
            reset();
            rules.scripted[2].push_back({1, 760}), rules.scripted[3].push_back({1, 760});
            auto acts = rules.turn({{0, 30, 2}});
            std::vector<std::array<int, 3>> seen; // (source, command, finisher)
            for (const auto& a : acts) seen.push_back({a.source, a.command, int(a.finisher)});
            check(seen.size() == 4 && seen[0][1] == 760 && seen[1][1] == 760 && !acts[0].combo && acts[1].combo &&
                      seen[2][0] == seen[1][0] && seen[2][1] == 761 && seen[2][2] == 1 && seen[3][0] == 0,
                  "a comboing pair's last attacker's Last Attack comes before the other side's action, marked a finisher");
            reset();
            rules.gauge = -1000; // (the monsters' full advantage: Finest Arts first - none here, so the Last Attack)
            rules.scripted[2].push_back({1, 760}), rules.scripted[3].push_back({1, 760});
            acts = rules.turn({{0, 30, 2}});
            check(acts.size() == 4 && acts[2].finisher && acts[2].command == 761, "at full advantage without Finest Arts, still the Last Attack");
            reset();
            rules.fighters[3].hp = 0; // (one action alone: no finisher)
            rules.scripted[2].push_back({1, 760});
            acts = rules.turn({{0, 30, 2}});
            bool any = false;
            for (const auto& a : acts) any |= a.finisher;
            check(!any, "a single action leads into no finisher");
            // Round 18: a Finest Arts (the monsters' side at its full advantage, a participant's Finest Arts) sets the
            // gauge to -500 after its own points (calculateForceBreakPoint: +-500 by the side)
            reset();
            rules.gauge = -1000, rules.gaugeStarted = true;
            rules.fighters[2].finestArts = 761, rules.fighters[3].finestArts = 761;
            rules.scripted[2].push_back({1, 760}), rules.scripted[3].push_back({1, 760});
            acts = rules.turn({{0, 30, 2}});
            check(acts.size() == 4 && acts[2].finisher && acts[2].command == 761 && acts[2].gaugeAfter == -500 && acts[1].gaugeAfter == -1000,
                  "a Finest Arts sets the break gauge to -500 for the monsters after its points");
            // Round 18: target kind 6 - one living opponent at random per action, picks repeating, each pick its hits
            reset();
            rules.fighters[1].hp = 5000;
            rules.monstersWait = false;
            rules.scripted[2].push_back({1, 765}), rules.fighters[3].hp = 0;
            acts = rules.turn({{0, -1, -1}, {1, -1, -1}});
            bool seen765 = false;
            for (const auto& a : acts) {
                if (a.command != 765) continue;
                seen765 = true;
                size_t total = 0;
                std::vector<int> who;
                for (const auto& h : a.targets) total += h.hits.size(), who.push_back(h.target);
                std::sort(who.begin(), who.end());
                check(total == 10, "a random-enemy command's 10 actions land 10 hits in all");
                check(who == std::vector<int>({0, 1}), "a random-enemy command spreads its actions over the living opponents, one entry each");
            }
            check(seen765, "the random-enemy command acts");
            // Round 28: +0x188 / +0xb8 last the whole turn (BtlCommandManager::Clear alone resets them) - a comboing pair as
            // the turn's last commands gets its finisher before the turn end's first command, a sourceless one resetting the
            // chance (HaveChanceReset: no source -> 1); none within the turn itself
            reset();
            rules.fighters[0].speed = 400;
            rules.scripted[2].push_back({1, 760}), rules.scripted[3].push_back({1, 760});
            acts = rules.turn({{0, 30, 2}});
            any = false;
            for (const auto& a : acts) any |= a.finisher;
            check(acts.size() == 3 && acts[0].source == 0 && acts[2].combo && !any, "the turn's last combo: no finisher within the turn");
            const auto endActs = rules.scriptCommands({{72, -1}});
            check(endActs.size() == 2 && endActs[0].finisher && endActs[0].command == 761 && endActs[0].source == acts[2].source &&
                      endActs[1].source < 0 && endActs[1].command == 72,
                  "the turn's last combo leads into a finisher before the turn end's sourceless command");
            reset();
            rules.fighters[0].speed = 400;
            rules.scripted[2].push_back({1, 760}), rules.scripted[3].push_back({1, 760});
            (void)rules.turn({{0, 30, 2}});
            reset();
            (void)rules.turn({{0, -1, -1}}); // (a new turn: Clear - the chunk gone)
            any = false;
            for (const auto& a : rules.scriptCommands({{72, -1}})) any |= a.finisher;
            check(!any, "a new turn clears the last chunk");
            // Round 30: a counter is VAdd'ed (AddCounterCommand 0x9b0f64: BtlCommand +0x18 set, +0x1a not) - appended to the
            // list in course and sorted again before the next command: 56 (priority -7) after the other monster's 762 still
            // to come, not straight after the command it answers; and not "inserted" - addLastAttack tests +0x1a alone -, so
            // the comboing pair's Last Attack goes in before it
            reset();
            rules.giveEffect(0, 25);
            rules.scripted[2].push_back({1, 762}), rules.scripted[3].push_back({1, 762});
            acts = rules.turn({});
            seen.clear();
            for (const auto& a : acts) seen.push_back({a.source, a.command, int(a.finisher)});
            check(seen.size() == 4 && seen[0][1] == 762 && seen[1][1] == 762 && seen[0][0] != seen[1][0] && acts[1].combo &&
                      seen[2] == std::array<int, 3>({seen[1][0], 761, 1}) && seen[3][0] == 0 && seen[3][1] == 56 && acts[3].target == seen[0][0],
                  "a counter goes after the list's commands of a higher priority, and a finisher goes in before it");
        }
        // Round 20: the enemy AI (BtlStatusList::addCommandByAI / Select / getCommandTarget, BtlOrderPoint)
        {
            cr3::Cr3Battle aiBattle;
            for (int m : {1, 2}) { cr3::Cr3BattleUnit u; u.monster = m; aiBattle.units.push_back(u); }
            cr3::Cr3BattleRules rules; check(rules.setup(t, aiBattle, 1, 2024), "setup AI battle");
            check(rules.fighters.size() == 2 && rules.fighters[1].commands.size() == 2 && rules.fighters[1].commands[0].command == 900 &&
                      rules.fighters[1].commands[0].cost == 1 && rules.fighters[1].commands[0].uses == 2 && rules.fighters[1].commands[1].uses == -1 &&
                      rules.fighters[1].actionsPerTurn == 1 && rules.fighters[1].card == 1,
                  "MonsterCommand's entries are (command, cost, uses), +0x10 the actions a turn, +0x0c the card");
            rules.fighters[0].player = true;
            auto heal = [&] { for (auto& f : rules.fighters) f.hp = f.maxHp = 100000; };
            auto picks = [&](int turns) {
                std::vector<int> out;
                for (int n = 0; n < turns; ++n) {
                    heal();
                    for (const auto& a : rules.turn({{0, -1, -1}}))
                        if (a.source == 1) out.push_back(a.command);
                }
                return out;
            };
            // uses: 900 (priority 10 against 901's 0, so always first while it may be used) twice, then 901; a mode
            // change gives them back (VChangeMode -> initializeCommandSkill)
            check(picks(4) == std::vector<int>({900, 900, 901, 901}), "a command of 2 uses stops after 2");
            rules.changeMode(1, 0);
            check(picks(1) == std::vector<int>({900}), "a mode change resets the uses");
            // the think points: OrderType row 0 a turn, -50 from the type used (clamped to 50 first), +50 to type 7 every third turn
            cr3::Cr3BattleRules points; check(points.setup(t, aiBattle, 1, 7), "setup think point battle");
            points.fighters[0].player = true;
            points.changeMode(1, 3);
            auto pointTurn = [&] { for (auto& f : points.fighters) f.hp = f.maxHp = 100000; points.turn({{0, -1, -1}}); };
            pointTurn();
            check(points.enemyPoints == std::array<int, 8>({10, 40, 40, 25, 10, 10, 10, 5}), "turn 1: +OrderType, -50 to the type used");
            pointTurn();
            check(points.enemyPoints == std::array<int, 8>({20, 80, 80, 50, 20, 20, 20, 10}), "turn 2: +OrderType, -50 to the type used");
            pointTurn();
            check(points.enemyPoints == std::array<int, 8>({30, 120, 120, 75, 30, 30, 30, 65}), "turn 3: +50 to type 7");
            points.addEnemyOrderPoint(7, 500), points.addEnemyOrderPoint(1, -500);
            check(points.enemyPoints[7] == 200 && points.enemyPoints[1] == 0, "AddEnemyOrderPoint clamps to 0 .. 200");
            // two actions a turn (+0x10 = 2): two picks, the second the same command on the same target (not preferred -
            // BtlCommandList::Has - but the only one: the fallback)
            points.changeMode(1, 2);
            int acted = 0;
            for (auto& f : points.fighters) f.hp = f.maxHp = 100000;
            for (const auto& a : points.turn({{0, -1, -1}})) acted += a.source == 1;
            check(acted == 2 && points.fighters[1].actionsPerTurn == 2, "a monster of 2 actions a turn acts twice");
            // a buff (select type 5) only where its effect is missing: with type 5's points the most, 902 on itself
            // first, then 901 while 305 lasts, and 902 again once it is gone
            cr3::Cr3BattleRules buff; check(buff.setup(t, aiBattle, 1, 11), "setup buff battle");
            buff.fighters[0].player = true;
            buff.changeMode(1, 1);
            buff.orderType = -1; // (no points a turn: type 5's 200 against 0 decides)
            buff.enemyPoints = {0, 0, 0, 0, 0, 200, 0, 0};
            auto buffTurn = [&] {
                std::vector<int> out;
                for (auto& f : buff.fighters) f.hp = f.maxHp = 100000;
                for (const auto& a : buff.turn({{0, -1, -1}}))
                    if (a.source == 1) out.push_back(a.command);
                return out;
            };
            check(buffTurn() == std::vector<int>({902}) && buff.hasEffect(1, 305), "the buff is picked where its effect is missing");
            check(buffTurn() == std::vector<int>({901}) && buffTurn() == std::vector<int>({901}), "the buff is not picked again while its effect is on");
            buff.removeEffect(1, 305);
            check(buffTurn() == std::vector<int>({902}), "the buff is picked again once its effect is gone");
            // BattleCommand.Add(unit, 1, command): its one action spent - the AI picks nothing more
            buff.scripted[1].push_back({1, 760});
            check(buffTurn() == std::vector<int>({760}), "a script's command takes the turn's action");
        }
        // Round 22: the get-set's Mass Attack (phaseAllAttack: one target for all - GetLowestHpTarget, the lowest
        // absolute HP among the first member's attack's targets; a kind 51 unit's at random) and Auto Battle
        // (BtlAutomaticManager: the attack pass - kills first -, IsEasyPrey, the heal gate; never an item, a buff
        // or a noAuto command; the battle's RNG left alone)
        {
            cr3::Cr3Battle autoBattle;
            for (int m : {1, 1, 2, 2, 2}) { cr3::Cr3BattleUnit u; u.monster = m; autoBattle.units.push_back(u); }
            auto fresh = [&](cr3::Cr3BattleRules& r, uint32_t seed) {
                check(r.setup(t, autoBattle, 1, seed), "setup auto battle");
                for (size_t k = 0; k < r.fighters.size(); ++k) {
                    cr3::Cr3Fighter& f = r.fighters[k];
                    f.player = k < 2, f.attackCommand = 900, f.maxHp = 100000, f.hp = 100000, f.mp = f.maxMp = 100;
                    if (f.player) f.geneCommands = {950, 951, 902, 511, 517};
                }
            };
            cr3::Cr3BattleRules mass;
            fresh(mass, 3);
            mass.fighters[2].hp = 500, mass.fighters[3].hp = 120, mass.fighters[4].hp = 120;
            auto massPicks = mass.massAttackChoices();
            check(massPicks.size() == 2 && massPicks[0] == std::array<int, 3>{0, 900, 3} && massPicks[1] == std::array<int, 3>{1, 900, 3},
                  "Mass Attack: every member's own attack on the lowest HP foe, the first collected on ties");
            mass.fighters[1].hp = 0;
            check(mass.massAttackChoices().size() == 1, "Mass Attack: the fallen take no part");
            mass.fighters[1].hp = 100000;
            mass.giveEffect(1, 306);
            bool spread = false;
            for (int n = 0; n < 40; ++n) {
                const auto p = mass.massAttackChoices();
                check(p[0][2] == 3, "Mass Attack: the target is kept for the others");
                spread |= p[1][2] != 3;
            }
            check(spread, "Mass Attack: a kind 51 unit's target is at random");
            // the attack pass: a foe it can kill is taken first (the most kills), not the strongest hit elsewhere;
            // 951 (never on auto) nor the buff 902 nor the items' 511 / 517 taken
            cr3::Cr3BattleRules plan;
            fresh(plan, 5);
            plan.fighters[3].hp = 2;
            auto picks = plan.autoChoices(true);
            check(picks.size() == 2 && picks[0][2] == 3 && picks[0][1] != 951, "Auto Battle: a kill is preferred");
            check(picks[1][2] != 3, "Auto Battle: a foe already killed by the plan is not attacked again");
            for (int n = 0; n < 30; ++n) {
                fresh(plan, uint32_t(100 + n));
                for (size_t k = 2; k < 5; ++k) plan.fighters[k].hp = 1000 + n * 37;
                plan.fighters[0].hp = 5000 + n * 1000; // (5 % .. 34 %: heals on and off)
                for (const auto& c : plan.autoChoices(true))
                    check(c[1] == -1 || c[1] == 900 || c[1] == 950, "Auto Battle: never an item, a buff or a noAuto command");
            }
            // the heal gate on the lowest living HP rate: above 50 % never; 20 % or less always (the foes not easy prey)
            fresh(plan, 7);
            plan.fighters[0].hp = 60000;
            picks = plan.autoChoices(true);
            check(picks[0][1] == 900 && picks[1][1] == 900, "Auto Battle: no heal above 50 % HP");
            fresh(plan, 7);
            plan.fighters[0].hp = 15000;
            picks = plan.autoChoices(true);
            check((picks[0][1] == 950 && picks[0][2] == 0) || (picks[1][1] == 950 && picks[1][2] == 0), "Auto Battle: a heal on the one at 15 % HP");
            // easy prey: every foe dead or losing half its HP to the plan - attacks only, no heal at 15 %
            fresh(plan, 7);
            plan.fighters[0].hp = 15000;
            plan.fighters[2].hp = plan.fighters[3].hp = 2, plan.fighters[4].hp = 0;
            picks = plan.autoChoices(true);
            check(picks[0][1] == 900 && picks[1][1] == 900, "Auto Battle: the foes easy prey - attacks alone");
            // the predictions on a copy: the battle's RNG is where it was
            cr3::Cr3BattleRules a, b;
            fresh(a, 99), fresh(b, 99);
            (void)a.autoChoices(true);
            const auto ta = a.turn({{0, 900, 2}}), tb = b.turn({{0, 900, 2}});
            check(ta.size() == tb.size() && !ta.empty() && ta[0].hits == tb[0].hits && ta.back().hits == tb.back().hits, "Auto Battle: the battle's RNG untouched");
        }
        // Round 23: the status effects' triggers (BtlStatusEffect::Trigger 0x9c7ad8 / End 0x9c7bd4), fixed damage and
        // blocks (calculateHpFluctuation 0x7e35e0, CanBlockBySkill 0x7e2924), the field's effects at every turn's start
        // (GiveFieldStatusEffectToCalculation 0x9c2220) and the assists (calculateAssist 0x7dbbf4)
        {
            auto fresh = [&](cr3::Cr3BattleRules& r, uint32_t seed) {
                check(r.setup(t, battle, 1, seed), "setup round 23 battle");
                for (size_t k = 0; k < r.fighters.size(); ++k) {
                    cr3::Cr3Fighter& f = r.fighters[k];
                    f.player = k < 2, f.hp = f.maxHp = 5000, f.mp = f.maxMp = 100, f.attack = f.defence = 100, f.luck = 10, f.geneLevel = 0;
                    f.speed = 100 - int(k), f.attackCommand = 1100, f.skills.clear(), f.commands.clear();
                }
                r.monstersWait = true;
            };
            auto actionOf = [](const std::vector<cr3::Cr3Action>& acts, int source, int command) -> const cr3::Cr3Action* {
                for (const auto& a : acts) if (a.source == source && a.command == command) return &a;
                return nullptr;
            };
            // Mental Wall: dormant until a magic command reaches it, then that command is turned on the caster - its
            // damage on the caster, the wall's bearer untouched - and the wall is gone (1 use, end timing 10)
            {
                cr3::Cr3BattleRules r; fresh(r, 31);
                r.giveEffect(2, 17);
                check(r.hasEffect(2, 17) && !r.hasKind(r.fighters[2], 49), "Mental Wall is dormant (kind 49 not read) until triggered");
                auto acts = r.turn({{0, 1100, 2}});
                check(r.hasEffect(2, 17) && r.fighters[2].hp < 5000, "a physical command neither triggers Mental Wall nor is reflected");
                r.fighters[2].hp = 5000;
                r.monstersWait = true;
                acts = r.turn({{0, 1101, 2}});
                const cr3::Cr3Action* m = actionOf(acts, 0, 1101);
                check(m && m->targets.size() == 1 && m->targets[0].target == 0 && m->targets[0].reflectedFrom == 2 && r.fighters[0].hp < 5000 &&
                          r.fighters[2].hp == 5000, "Mental Wall reflects the magic command onto its caster");
                check(!r.hasEffect(2, 17), "Mental Wall is used up once it has reflected");
                r.monstersWait = true;
                acts = r.turn({{0, 1101, 2}});
                m = actionOf(acts, 0, 1101);
                check(m && m->targets[0].target == 2 && m->targets[0].reflectedFrom < 0 && r.fighters[2].hp < 5000, "the next magic command lands");
            }
            // Moonlight Shadow: one physical command evaded, then gone; magic leaves it be
            {
                cr3::Cr3BattleRules r; fresh(r, 32);
                r.giveEffect(2, 18);
                auto acts = r.turn({{0, 1101, 2}});
                check(r.hasEffect(2, 18) && r.fighters[2].hp < 5000, "magic does not trigger Moonlight Shadow");
                r.fighters[2].hp = 5000, r.monstersWait = true;
                acts = r.turn({{0, 1107, 2}});
                const cr3::Cr3Action* a = actionOf(acts, 0, 1107);
                check(a && a->targets[0].flags.size() == 3 && std::all_of(a->targets[0].flags.begin(), a->targets[0].flags.end(), [](uint8_t f) { return (f & 4) != 0; }) &&
                          r.fighters[2].hp == 5000, "Moonlight Shadow evades every hit of one physical command");
                check(!r.hasEffect(2, 18), "Moonlight Shadow is used up after one command");
                r.monstersWait = true;
                acts = r.turn({{0, 1107, 2}});
                check(r.fighters[2].hp < 5000, "the next physical command lands");
            }
            // Rally: x2 on the next physical command alone, then gone; a magic one leaves it dormant
            {
                cr3::Cr3BattleRules a, b; fresh(a, 33), fresh(b, 33);
                a.giveEffect(0, 23);
                const int plain = actionOf(b.turn({{0, 1100, 2}}), 0, 1100)->targets[0].hits[0];
                const int rallied = actionOf(a.turn({{0, 1100, 2}}), 0, 1100)->targets[0].hits[0];
                check(rallied == plain * 2 && !a.hasEffect(0, 23), "Rally doubles the next physical command, then is used up");
                a.monstersWait = b.monstersWait = true;
                check(actionOf(a.turn({{0, 1100, 3}}), 0, 1100)->targets[0].hits[0] == actionOf(b.turn({{0, 1100, 3}}), 0, 1100)->targets[0].hits[0],
                      "Rally applies once");
                cr3::Cr3BattleRules c; fresh(c, 33);
                c.giveEffect(0, 23);
                (void)c.turn({{0, 1101, 2}});
                check(c.hasEffect(0, 23) && !c.hasKind(c.fighters[0], 54), "a magic command leaves Rally dormant");
            }
            // Counter: the bearer, hit by a physical command of the other side, answers with 56 on the attacker - once
            {
                cr3::Cr3BattleRules r; fresh(r, 34);
                r.giveEffect(2, 25);
                auto acts = r.turn({{0, 1100, 2}});
                check(acts.size() == 2 && acts[0].source == 0 && acts[1].source == 2 && acts[1].command == 56 && acts[1].target == 0 && r.fighters[0].hp < 5000,
                      "Counter: command 56 on the attacker right after its command");
                check(!r.hasEffect(2, 25), "Counter is used up");
                r.monstersWait = true;
                acts = r.turn({{0, 1100, 2}});
                check(acts.size() == 1, "no second counter");
            }
            // Regenerate: a bearer knocked out is revived at 50 % by 306 with no actor, right after the command
            {
                cr3::Cr3BattleRules r; fresh(r, 35);
                r.giveEffect(1, 92);
                r.fighters[0].speed = 1000, r.fighters[1].hp = 1, r.fighters[1].speed = 1;
                auto acts = r.turn({{0, 80, 1}});
                const cr3::Cr3Action* rev = actionOf(acts, -1, 306);
                check(rev && rev->target == 1 && !rev->assist && r.fighters[1].hp == 2500 && !r.hasEffect(1, 92), "Regenerate revives its KO'd bearer at 50 %");
            }
            // kind 91: every hit on an Eggon (skill 479) does 1; a block (skill 329, kind 3) stops a physical hit - not a
            // magic one -, a pierce (attacker kind 89) gets through
            {
                cr3::Cr3BattleRules r; fresh(r, 36);
                r.fighters[2].skills = {479};
                const cr3::Cr3Action* a = nullptr;
                auto acts = r.turn({{0, 1107, 2}});
                a = actionOf(acts, 0, 1107);
                check(a && a->targets[0].hits == std::vector<int>({1, 1, 1}) && r.fighters[2].hp == 4997, "kind 91: 1 damage a hit");
                r.fighters[2].skills = {329}, r.monstersWait = true;
                acts = r.turn({{0, 1100, 2}});
                a = actionOf(acts, 0, 1100);
                check(a && a->targets[0].hits == std::vector<int>{0} && (a->targets[0].flags[0] & 8) && r.fighters[2].hp == 4997, "kind 3 blocks a physical hit");
                r.monstersWait = true;
                acts = r.turn({{0, 1101, 2}});
                check(r.fighters[2].hp < 4997, "kind 3 lets magic through");
                const int before = r.fighters[2].hp;
                r.fighters[0].skills = {107}, r.monstersWait = true;
                acts = r.turn({{0, 1100, 2}});
                check(r.fighters[2].hp < before, "the attacker's kind 89 pierces a block");
            }
            // the field's effects: the living players alone, again at every turn's start; none for one with skill kind 0x6a
            {
                cr3::Cr3BattleRules r; fresh(r, 37);
                r.fieldEffects = {46};
                r.fighters[1].hp = 0;
                r.giveFieldEffects();
                check(r.hasEffect(0, 46) && !r.hasEffect(1, 46) && !r.hasEffect(2, 46) && !r.hasEffect(3, 46),
                      "field effects: the living players alone, no monster, no fallen one");
                r.fighters[1].hp = 100, r.removeEffect(0, 46);
                (void)r.turn({{0, -1, -1}});
                check(r.hasEffect(0, 46) && r.hasEffect(1, 46) && r.fighters[0].effectStacks[46] == 1, "field effects: given again at the next turn's start, not stacked");
                r.fighters[0].skills = {143}, r.monstersWait = true;
                (void)r.turn({{0, -1, -1}});
                check(!r.hasEffect(0, 46) && r.hasEffect(1, 46), "field effects: taken away from one with skill kind 0x6a");
            }
            // the assists: the chance by the cheer points, the ten handlers in order, the costs (halved under kind 79)
            {
                using R = cr3::Cr3BattleRules;
                check(R::assistChance(9) == 0 && R::assistChance(10) == 0.1f && R::assistChance(24) == 0.1f && R::assistChance(25) == 0.2f &&
                          R::assistChance(49) == 0.2f && R::assistChance(50) == 0.3f && R::assistChance(79) == 0.3f && R::assistChance(80) == 0.4f &&
                          R::assistChance(99) == 0.4f && R::assistChance(100) == 0.5f && R::assistChance(1000) == 0.5f,
                      "assist chance: <10 0, 10 0.1, 25 0.2, 50 0.3, 80 0.4, 100+ 0.5");
                cr3::Cr3BattleRules r; fresh(r, 38);
                int target = -2, cost = -1;
                auto pick = [&](int points) { r.cheer = points; return r.pickAssist(&target, &cost); };
                r.fighters[0].hp = r.fighters[1].hp = 0;
                check(pick(100) == 61 && target == -1 && cost == 50, "assist 1: two fallen, 50 points - AllResurrection 61");
                check(pick(49) == 62 && target == 0 && cost == 25, "assist 2: under 50 points - Resurrection 62 on the first fallen");
                r.fighters[0].hp = 5000;
                check(pick(100) == 62 && target == 1, "assist 2: one fallen - Resurrection 62");
                r.fighters[0].hp = 1000;
                check(pick(24) == 63 && cost == 10, "assist 3: under 25 points, every player at 20 % or under (the fallen too) - AllHealHp 63");
                r.fighters[0].hp = r.fighters[1].hp = 5000;
                r.fighters[0].mp = r.fighters[1].mp = 5;
                check(pick(100) == 64 && cost == 20, "assist 4: every player at 10 % MP - AllHealMp 64");
                r.fighters[0].mp = 100, r.fighters[1].hp = 900;
                check(pick(100) == 65 && target == 1 && cost == 5, "assist 5: one at 20 % HP - HealHp 65");
                check(pick(4) == -1, "assist 5: under 5 points none");
                r.fighters[1].hp = 5000;
                check(pick(100) == 66 && target == 1 && cost == 10, "assist 6: one at 10 % MP - HealMp 66");
                r.fighters[1].mp = 100;
                r.giveEffect(0, 14);
                check(pick(100) == 69 && cost == 5, "assist 8: a player with 14 - RemoveDisableGene 69");
                r.giveEffect(1, 13);
                check(pick(100) == 67, "assist 7 before 8: RemovePoison 67");
                r.removeEffect(0, 14), r.removeEffect(1, 13);
                check(pick(20) == 71 && cost == 20 && pick(19) == -1, "assist 10: Enhance 71 at 20 points");
                r.giveEffect(0, 400);
                check(pick(19) == 71 && cost == 10, "assist costs halved while a player has an enabled kind 79");
                // the roll: at the turn's end, after its commands, source -1, the points spent
                cr3::Cr3BattleRules a; fresh(a, 39);
                a.cheer = 100, a.fighters[1].hp = 900;
                cr3::Cr3Action got;
                bool seen = false;
                for (int n = 0; n < 40 && !seen; ++n) {
                    a.monstersWait = true;
                    const auto acts = a.turn({{0, 1100, 2}});
                    if (acts.back().source == -1) {
                        seen = true, got = acts.back();
                        check(acts.size() == 2 && acts[0].source == 0, "the assist comes after the turn's commands");
                    }
                }
                check(seen && got.assist && got.command == 65 && got.target == 1 && a.fighters[1].hp == 3400 && a.cheer == 95,
                      "an assist: HealHp 65 - 50 % of the max HP -, 5 points spent");
                cr3::Cr3BattleRules none; fresh(none, 39);
                none.cheer = 100, none.fighters[1].hp = 900, none.noAssist = true;
                for (int n = 0; n < 40; ++n) {
                    none.monstersWait = true;
                    check(none.turn({{0, 1100, 2}}).back().source == 0, "no assist with MbCouplingInfo +0x44 bit 6");
                }
                cr3::Cr3BattleRules low; fresh(low, 39);
                low.cheer = 9, low.fighters[1].hp = 900;
                for (int n = 0; n < 40; ++n) {
                    low.monstersWait = true;
                    check(low.turn({{0, 1100, 2}}).back().source == 0, "no assist under 10 points");
                }
            }
        }
        // Round 24: the steals (BtlDamageTargetList::Calculate 0x97f81c / SetStealDamage 0x988eb4): the caster's own entry
        // last, its hit k the targets' landed hit-k damage summed - an HP steal at most each target's HP, an MP steal its MP
        {
            cr3::Cr3BattleRules r;
            check(r.setup(t, battle, 1, 24), "setup the steals' battle");
            for (size_t k = 0; k < r.fighters.size(); ++k) {
                cr3::Cr3Fighter& f = r.fighters[k];
                f.player = k < 2, f.hp = f.maxHp = 5000, f.mp = f.maxMp = 100, f.attack = 200, f.defence = 1, f.speed = k == 0 ? 100 : 1;
                f.attackCommand = 30;
            }
            auto stealOf = [](const std::vector<cr3::Cr3Action>& acts) -> const cr3::Cr3Action& {
                for (const cr3::Cr3Action& a : acts)
                    if (a.source == 0 && a.command >= 1390) return a;
                throw std::runtime_error("the steal's action missing");
            };
            r.fighters[0].hp = 1000, r.fighters[2].hp = 7;
            r.monstersWait = true;
            const auto one = r.turn({{0, 1390, 2}, {1, -1, -1}});
            const cr3::Cr3Action& a1 = stealOf(one);
            check(a1.targets.size() == 2 && !a1.targets[0].stealBack && a1.targets[0].hits == std::vector<int>{7} && r.fighters[2].hp == 0,
                  "an HP steal takes no more than the target's HP (calculateHpFluctuation's cap)");
            check(a1.targets[1].stealBack && a1.targets[1].target == 0 && a1.targets[1].hits == std::vector<int>{-7} &&
                      a1.targets[1].colors == std::vector<uint8_t>{3} && r.fighters[0].hp == 1007 && a1.targets[1].hpAfter == 1007 && a1.target == 2,
                  "an HP steal: the caster's own entry last, healed by what landed");
            r.fighters[2].hp = 30, r.fighters[3].hp = 40, r.fighters[0].hp = 1000;
            r.monstersWait = true;
            const auto turna2 = r.turn({{0, 1391, 2}, {1, -1, -1}});
            const cr3::Cr3Action& a2 = stealOf(turna2);
            check(a2.targets.size() == 3 && a2.targets[2].stealBack && a2.targets[2].hits == std::vector<int>{-70} && r.fighters[0].hp == 1070,
                  "an HP steal on all: one heal, the sum of the targets' (30 + 40)");
            r.fighters[2].hp = r.fighters[3].hp = 5000, r.fighters[2].mp = 5, r.fighters[0].mp = 10;
            r.monstersWait = true;
            const auto turna3 = r.turn({{0, 1392, 2}, {1, -1, -1}});
            const cr3::Cr3Action& a3 = stealOf(turna3);
            check(a3.targets[0].mpHits == std::vector<int>{5} && a3.targets[1].stealBack && a3.targets[1].mpHits == std::vector<int>{-5} &&
                      a3.targets[1].hits == std::vector<int>{0} && a3.targets[1].colors == std::vector<uint8_t>{4} && r.fighters[0].mp == 15 &&
                      r.fighters[2].mp == 0,
                  "an MP steal: at most the target's MP, the caster's MP back (colour 4)");
            r.fighters[2].hp = 5000, r.fighters[0].hp = 4990;
            r.monstersWait = true;
            (void)r.turn({{0, 1390, 2}, {1, -1, -1}});
            check(r.fighters[0].hp == 5000 && r.fighters[2].hp < 4800, "an HP steal heals the caster to its max at most");
        }
        // Round 25: the turn end's commands (BtlSysStateAction::VUpdate case 0x12, phaseStatusEffect / TriggerTurnEnd), the
        // command change (status kind 120 - GetCommandChange), the skills' kind 60 (TriggerSkillToCalculation) and the opening,
        // and triggers 4 / 7 / 8 / 9 once a target (TriggerAfterDamageToCalculation 0x988498)
        {
            auto fresh = [&](cr3::Cr3BattleRules& r, uint32_t seed) {
                check(r.setup(t, battle, 1, seed), "setup round 25 battle");
                for (size_t k = 0; k < r.fighters.size(); ++k) {
                    cr3::Cr3Fighter& f = r.fighters[k];
                    f.player = k < 2, f.hp = f.maxHp = 5000, f.mp = f.maxMp = 100, f.attack = f.defence = 100, f.luck = 10, f.geneLevel = 0;
                    f.speed = 100 - int(k), f.attackCommand = 1100, f.skills.clear(), f.commands.clear();
                }
                r.monstersWait = true;
            };
            auto actionOf = [](const std::vector<cr3::Cr3Action>& acts, int source, int command) -> const cr3::Cr3Action* {
                for (const auto& a : acts) if (a.source == source && a.command == command) return &a;
                return nullptr;
            };
            // the script's commands: end timing 11 first, then each with no source - 74 gives 88 to every player, 72 revives the
            // fallen and fills HP and MP
            {
                cr3::Cr3BattleRules r; fresh(r, 51);
                r.fighters[0].mp = 10, r.fighters[1].hp = 0;
                const auto acts = r.scriptCommands({{74, -1}, {72, -1}});
                check(acts.size() == 2 && acts[0].source == -1 && acts[0].command == 74 && acts[0].targets.size() == 1 && r.hasEffect(0, 88) &&
                          !r.hasEffect(1, 88), "script command 74 gives its status effect (88) to the living players");
                check(acts[1].command == 72 && acts[1].targets.size() == 2 && r.fighters[1].hp == 5000 && r.fighters[0].mp == 100 &&
                          acts[1].targets[1].hits == std::vector<int>{-5000}, "script command 72 revives the fallen and fills HP and MP (damage 18)");
                check(r.scriptCommands({}).empty(), "no script command: nothing");
            }
            // 34: given to a monster KO'd by a hit (skill 602's kind 60 at trigger 7, condition 3 at 0 %); three turns on, at the
            // turn's end, its 1759 revives it with no source
            {
                cr3::Cr3BattleRules r; fresh(r, 52);
                r.fighters[2].skills = {602}, r.fighters[2].hp = 1;
                auto acts = r.turn({{0, 1100, 2}});
                check(r.fighters[2].dead() && r.hasEffect(2, 34) && r.outcome() == 0, "skill kind 60 at trigger 7: the KO'd monster gets 34");
                int revivedAt = -1;
                for (int n = 1; n <= 5 && revivedAt < 0; ++n) {
                    if (n > 1) r.monstersWait = true, acts = r.turn({{0, -1, -1}});
                    const auto end = r.turnEndActions();
                    (void)r.endTurn();
                    if (const cr3::Cr3Action* rev = actionOf(end, -1, 1759)) {
                        check(rev->target == 2 && r.fighters[2].hp == 5000 && !r.hasEffect(2, 34), "1759 revives 34's bearer at 100 %");
                        revivedAt = n;
                    } else check(end.empty() && r.fighters[2].dead(), "34 waits for its turns");
                }
                check(revivedAt == 4, "34 revives at the end of the third turn after the KO (condition 17: turns at or under 1)");
            }
            // kind 120: Dance (56) on the actor - trigger 3 enables it, its 1512 replaces the command - its target chosen again,
            // itself -, end timing 3 disables it; a turn later the same, its three turns
            {
                cr3::Cr3BattleRules r; fresh(r, 53);
                r.giveEffect(0, 56);
                check(r.commandChange(r.fighters[0]) < 0, "a dormant kind 120: no command change");
                auto acts = r.turn({{0, 1100, 2}});
                const cr3::Cr3Action* d = actionOf(acts, 0, 1512);
                check(d && !actionOf(acts, 0, 1100) && d->target == 0 && r.fighters[2].hp == 5000, "kind 120: 1512 played in place of the attack, on itself");
                check(r.hasEffect(0, 56) && r.fighters[0].effectsDormant.count(56), "end timing 3 disables the kind 120 effect again");
                r.monstersWait = true;
                acts = r.turn({{0, -1, -1}});
                check(actionOf(acts, 0, 1512) && !actionOf(acts, 0, -1), "kind 120 replaces a guard too");
                r.removeEffect(0, 56), r.monstersWait = true;
                acts = r.turn({{0, 1100, 2}});
                check(actionOf(acts, 0, 1100) && r.fighters[2].hp < 5000, "no kind 120: the command as chosen");
            }
            // skill kind 60 at its trigger (600: trigger 4 gives 3 as it is hit), and the opening (601: trigger 1's kind 100 command
            // and its kind 60 status effect)
            {
                cr3::Cr3BattleRules r; fresh(r, 54);
                r.fighters[2].skills = {600}, r.fighters[0].skills = {601};
                check(!r.hasEffect(2, 3), "skill 600: nothing before its trigger");
                (void)r.turn({{0, 1100, 2}});
                check(r.hasEffect(2, 3) && !r.hasEffect(3, 3), "skill kind 60 gives its status effect at its trigger (4)");
                cr3::Cr3BattleRules o; fresh(o, 54);
                o.fighters[0].skills = {601}, o.fighters[3].skills = {601}, o.fighters[3].hp = 0;
                const auto open = o.opening();
                check(open == std::vector<std::pair<int, int>>{{0, 81}} && o.hasEffect(0, 4) && !o.hasEffect(3, 4),
                      "the opening: trigger 1's kind 100 command {unit, 81} and kind 60's status effect, the fallen left out");
            }
            // Cover (skill kind 118 = the command): the coverer takes the hit, the original untouched; another command not
            {
                cr3::Cr3BattleRules r; fresh(r, 56);
                r.fighters[3].skills = {603};
                auto acts = r.turn({{0, 1100, 2}});
                const cr3::Cr3Action* a = actionOf(acts, 0, 1100);
                check(a && a->targets.size() == 1 && a->targets[0].target == 3 && a->targets[0].coveredFrom == 2 && r.fighters[2].hp == 5000 &&
                          r.fighters[3].hp < 5000, "Cover: skill kind 118 of the command - the coverer takes the hit");
                r.monstersWait = true, r.fighters[3].hp = 5000;
                acts = r.turn({{0, 1101, 2}});
                a = actionOf(acts, 0, 1101);
                check(a && a->targets[0].target == 2 && a->targets[0].coveredFrom < 0 && r.fighters[3].hp == 5000, "Cover: another command not covered");
            }
            // Round 27, the skills' enabled state (BtlSkill +0x14): Critical x2 (1) - enabled at trigger 3 by the unit's own attack
            // (condition 4) alone, its kind 69 doubling a critical, ended at end timing 9
            {
                const auto critHit = [&](int attackCommand, int skill) {
                    cr3::Cr3BattleRules r; fresh(r, 57);
                    r.fighters[0].attackCommand = attackCommand, r.fighters[0].skills = {skill};
                    const auto acts = r.turn({{0, 1108, 2}});
                    const cr3::Cr3Action* a = actionOf(acts, 0, 1108);
                    check(a && (a->targets[0].flags[0] & 1) && !r.fighters[0].skillsOn.count(skill), "an always-critical hit, its skill ended at 9");
                    return a->targets[0].hits[0];
                };
                check(critHit(1108, 603) == 8, "a critical: x (1 + 0.5 + 0.1)");
                check(critHit(1108, 1) == 16, "Critical x2 (kind 69 200) on the unit's own attack: the critical doubled");
                check(critHit(1100, 1) == 8, "Critical x2: another command than the unit's attack - condition 4 fails, no double");
            }
            // Critical First Attack (2): the first attack critical, its use a battle spent - never enabled again
            {
                cr3::Cr3BattleRules r; fresh(r, 58);
                r.fighters[0].skills = {2}, r.fighters[0].attackCommand = 1112;
                auto acts = r.turn({{0, 1112, 2}});
                const cr3::Cr3Action* a = actionOf(acts, 0, 1112);
                check(a && (a->targets[0].flags[0] & 1) && r.fighters[0].skillUses[2][0] == 0, "Critical First Attack: kind 139 - critical, its use spent");
                r.monstersWait = true;
                (void)r.turn({{0, 1112, 2}});
                check(r.fighters[0].skillUses[2][0] == 0 && !r.fighters[0].skillsOn.count(2), "Critical First Attack: no uses left - not enabled again");
            }
            // Afterthought Guard (127: trigger 10, condition 4): Guard after the unit's own attack, not after another command
            {
                cr3::Cr3BattleRules r; fresh(r, 59);
                r.fighters[0].skills = {127};
                (void)r.turn({{0, 1101, 2}});
                check(!r.hasEffect(0, 2), "Afterthought Guard: no guard after a command not the unit's attack");
                r.monstersWait = true;
                (void)r.turn({{0, 1100, 2}});
                check(r.hasEffect(0, 2) && !r.fighters[0].skillsOn.count(127), "Afterthought Guard: Guard after its attack, the skill ended at 9 - after trigger 10");
            }
            // Narrow Escape (134: trigger 5 - before the HP change, the total -15000 -, condition 29 at full HP): kind 53 holds it at 1,
            // 109 then casts Braveheart (1525) at trigger 8; end 6 ends it; once a battle - the next lethal hit kills
            {
                cr3::Cr3BattleRules r; fresh(r, 60);
                r.fighters[2].skills = {134};
                auto acts = r.turn({{0, 1111, 2}});
                check(r.fighters[2].hp == 1 && actionOf(acts, 2, 1525) && r.hasEffect(2, 88), "Narrow Escape: lethal damage at full HP - left at 1, Braveheart cast");
                check(!r.fighters[2].skillsOn.count(134) && r.fighters[2].skillUses[134][0] == 0, "Narrow Escape: ended at 6, its use a battle spent");
                r.fighters[2].hp = 5000, r.monstersWait = true;
                (void)r.turn({{0, 1111, 2}});
                check(r.fighters[2].dead(), "Narrow Escape: once a battle - the next lethal hit kills");
                cr3::Cr3BattleRules n; fresh(n, 60);
                n.fighters[2].skills = {134}, n.fighters[2].hp = 4999;
                (void)n.turn({{0, 1111, 2}});
                check(n.fighters[2].dead() && n.fighters[2].skillUses[134][0] == 1, "Narrow Escape: not at full HP - condition 29 fails");
            }
            // Emergency Guard (126: trigger 6, the gauge crossing 0, every unit, end 5 straight after): Guard for the side that lost
            // the advantage alone (the other's pair empty)
            {
                cr3::Cr3BattleRules r; fresh(r, 61);
                for (auto& f : r.fighters) f.skills = {126};
                r.gauge = 300, r.gaugeStarted = true;
                check(!r.addBreak(100) && !r.hasEffect(0, 2), "no crossing: no trigger 6");
                check(r.addBreak(-700), "the gauge crossing 0: a break");
                check(r.hasEffect(0, 2) && r.hasEffect(1, 2) && !r.hasEffect(2, 2) && !r.hasEffect(3, 2) && r.gauge == -800,
                      "Emergency Guard: the party - its advantage lost - guards, the monsters not");
                check(r.fighters[0].skillsOn.empty() && r.fighters[2].skillsOn.empty(), "Emergency Guard: ended at 5");
            }
            // a no-trigger skill's kinds 60 / 61 at every trigger with the advantage as it stands (452: the Death Mask's); a held
            // effect given again one accumulation more, its turns not refreshed (604 -> 301)
            {
                cr3::Cr3BattleRules r; fresh(r, 62);
                r.fighters[2].skills = {452}, r.fighters[3].skills = {604};
                r.giveEffect(2, 88), (void)r.triggerSkills(2, 0);
                check(!r.hasEffect(2, 88), "452 without the advantage: kind 61 takes 88 away");
                r.gauge = -500, r.gaugeStarted = true, (void)r.triggerSkills(2, 12);
                check(r.hasEffect(2, 88), "452 with the advantage: kind 60 gives 88");
                (void)r.triggerSkills(3, 0);
                for (auto& [id, turns] : r.fighters[3].effects) if (id == 301) turns = 2;
                (void)r.triggerSkills(3, 12), (void)r.triggerSkills(3, 12);
                int turns = 0;
                for (const auto& [id, n] : r.fighters[3].effects) if (id == 301) turns = n;
                check(r.fighters[3].effectStacks[301] == 2 && turns == 2, "a held effect re-given: an accumulation more (at most 2), its turns kept");
            }
            // condition 24 / 25 (113: the device clock's day): trigger 12 enables it by day alone; end 11 ends it
            {
                cr3::Cr3BattleRules r; fresh(r, 63);
                r.fighters[0].skills = {113};
                r.localHour = 22, (void)r.triggerSkills(0, 12);
                check(!r.skillOn(r.fighters[0], 113), "condition 24 at 22:00: not day - not enabled");
                r.localHour = 9, (void)r.triggerSkills(0, 12);
                check(r.skillOn(r.fighters[0], 113) && r.skillConstant(r.fighters[0], 45) == 10, "condition 24 at 09:00: enabled, its kind 45 read");
                r.endEffects(0, 11);
                check(!r.skillOn(r.fighters[0], 113), "end timing 11 ends it");
            }
            // triggers 7 / 8 / 9 once a target: 500 / 501 / 502 each one use a trigger
            {
                cr3::Cr3BattleRules r; fresh(r, 55);
                r.giveEffect(2, 500), r.giveEffect(2, 501), r.giveEffect(2, 502);
                auto uses = [&](int fi, int id) { return r.fighters[size_t(fi)].effectUses[id]; };
                (void)r.turn({{0, 1107, 2}});
                check(uses(2, 500) == 4 && uses(2, 501) == 4 && uses(2, 502) == 5, "three landed hits: trigger 7 and 8 once, no trigger 9");
                r.fighters[2].skills = {329}, r.monstersWait = true;
                (void)r.turn({{0, 1100, 2}});
                check(uses(2, 500) == 3 && uses(2, 501) == 4, "a blocked first hit: trigger 7, no trigger 8");
                r.fighters[2].skills.clear(), r.giveEffect(2, 18), r.monstersWait = true;
                (void)r.turn({{0, 1107, 2}});
                check(uses(2, 500) == 3 && uses(2, 501) == 4 && uses(2, 502) == 5, "every hit evaded: no trigger at all");
                r.giveEffect(1, 500), r.giveEffect(1, 501), r.fighters[1].hp = 1000, r.monstersWait = true;
                (void)r.turn({{0, 81, 1}});
                check(uses(1, 500) == 4 && uses(1, 501) == 5 && r.fighters[1].hp > 1000, "a heal: trigger 7, no trigger 8 (a net heal)");
                r.giveEffect(0, 502), r.monstersWait = true;
                (void)r.turn({{0, 1102, 0}});
                check(uses(0, 502) == 4, "a status command (no hits): trigger 9");
            }
        }
        // Round 20 against the release's tables (CR3_DATA): monster 913 mode 2's 2 actions, 865's Augmented Core
        // costing 2, and battle 22's monster 1408 (weights 15 / 10 / 10 / 5 / 5: each U(0,1) x w / sum, the
        // highest - 54.4 / 21.2 / 21.2 / 1.7 / 1.7 %)
        if (const char* data = std::getenv("CR3_DATA"); data && *data) {
            cr3::MvglArchive archive;
            cr3::Cr3BattleTables real;
            std::string err;
            if (archive.open(data, &err) && real.load(archive, &err)) {
                int character = -1;
                if (const cr3::MbeFile::Table* ch = real.character.table("Char")) {
                    cr3::MbeFile::Row r;
                    for (size_t k = 0; k < ch->rows.size() && character <= 0; ++k)
                        if (real.character.row(*ch, k, "i", r)) character = r.i(0);
                }
                auto battleOf = [&](int coupling, cr3::Cr3BattleRules& rules, uint32_t seed) {
                    cr3::Cr3Battle b;
                    check(b.setup(real, coupling, -1, {character}, seed, &err), "set up a release battle");
                    check(rules.setup(real, b, 10, seed, &err), "set up a release battle's rules");
                };
                cr3::Cr3BattleRules r913;
                battleOf(2612, r913, 5);
                int m913 = -1;
                for (size_t k = 0; k < r913.fighters.size(); ++k) if (r913.fighters[k].id == 913) m913 = int(k);
                check(m913 >= 0 && r913.fighters[size_t(m913)].actionsPerTurn == 1, "monster 913 mode 0: one action a turn");
                r913.changeMode(m913, 2);
                check(r913.fighters[size_t(m913)].actionsPerTurn == 2, "monster 913 mode 2: two actions a turn (+0x10)");
                cr3::Cr3Fighter f865;
                {
                    cr3::Cr3BattleRules r; battleOf(2612, r, 5);
                    r.fighters[size_t(m913)].id = 865;
                    r.changeMode(m913, 0);
                    f865 = r.fighters[size_t(m913)];
                }
                check(f865.actionsPerTurn == 2 && f865.commands.size() == 3 && f865.commands[2].command == 267 && f865.commands[2].cost == 2 &&
                          f865.commands[0].cost == 1 && f865.commands[1].cost == 1,
                      "monster 865: two actions a turn, Augmented Core (267) costing 2");
                // Round 23 against the release: coupling 501's Eggon has skill 479 (kind 91 -1) - every landed hit 1; the
                // release's StatusEffect 17 (Mental Wall) comes dormant with 1 use, 46 (a field's) on at once
                {
                    cr3::Cr3BattleRules egg;
                    battleOf(501, egg, 5);
                    int eggon = -1, hero = -1;
                    for (size_t k = 0; k < egg.fighters.size(); ++k) {
                        const auto& sk = egg.fighters[k].skills;
                        if (eggon < 0 && std::find(sk.begin(), sk.end(), 479) != sk.end()) eggon = int(k);
                        if (hero < 0 && egg.fighters[k].player) hero = int(k);
                    }
                    check(eggon >= 0 && hero >= 0, "coupling 501: an Eggon with skill 479");
                    int landed = 0;
                    for (int n = 0; n < 10 && landed == 0; ++n) {
                        for (auto& f : egg.fighters) f.hp = f.maxHp = 100000;
                        egg.monstersWait = true;
                        for (const auto& a : egg.turn({{hero, egg.attackCommandOf(hero), eggon}}))
                            if (a.source == hero)
                                for (const auto& th : a.targets)
                                    for (size_t h = 0; h < th.hits.size(); ++h)
                                        if (!(th.flags[h] & 4)) check(th.hits[h] == 1, "an Eggon takes 1 a hit"), ++landed;
                    }
                    check(landed > 0, "a hit landed on the Eggon");
                    egg.giveEffect(hero, 17), egg.giveEffect(hero, 46);
                    check(egg.hasEffect(hero, 17) && egg.fighters[size_t(hero)].effectsDormant.count(17) && egg.fighters[size_t(hero)].effectUses[17] == 1 &&
                              !egg.fighters[size_t(hero)].effectsDormant.count(46),
                          "release StatusEffect 17 dormant with 1 use, 46 enabled");
                }
                cr3::Cr3BattleRules r22;
                battleOf(22, r22, 12345);
                int boss = -1;
                for (size_t k = 0; k < r22.fighters.size(); ++k) if (r22.fighters[k].id == 1408) boss = int(k);
                check(boss >= 0 && r22.orderTolerance == 0, "battle 22: monster 1408, tolerance 0");
                std::map<int, int> count;
                const int turns = 6000;
                for (int n = 0; n < turns; ++n) {
                    for (auto& f : r22.fighters) f.hp = f.maxHp = 1000000, f.mp = f.maxMp = 1000, f.effects.clear(), f.effectStacks.clear();
                    r22.gauge = 0;
                    std::vector<std::array<int, 3>> guard;
                    for (size_t k = 0; k < r22.fighters.size(); ++k) if (r22.fighters[k].player) guard.push_back({int(k), -1, -1});
                    for (const auto& a : r22.turn(guard))
                        if (a.source == boss && !a.finisher) ++count[a.command];
                }
                const auto share = [&](int id) { return 100.0 * count[id] / turns; };
                std::cout << "battle 22's 1408 over " << turns << " turns: White " << share(1608) << " % Red " << share(1611) << " % Blue " << share(1612)
                          << " % Green " << share(1613) << " % Observing " << share(1501) << " %\n";
                check(std::abs(share(1608) - 54.4) < 2.5 && std::abs(share(1611) - 21.2) < 2.0 && std::abs(share(1612) - 21.2) < 2.0 &&
                          std::abs(share(1613) - 1.7) < 1.0 && std::abs(share(1501) - 1.7) < 1.0,
                      "battle 22's 1408 picks by max(U x weight / sum): 54 / 21 / 21 / 2 / 2 %");
                // Round 24 against the release's tables: a call for help (Rainforcement, extraType 0x11), Reverse Gravity
                // (387, extraType 0xb) and the Mimics' escape (1741 -> status effect 51): battle 2612's party, its monster
                // made the caster (one action a turn), any other gone; the party unhurt, guarding, no assist
                auto special = [&](cr3::Cr3BattleRules& r, uint32_t seed, int caster) {
                    battleOf(2612, r, seed);
                    int m = -1;
                    for (size_t k = 0; k < r.fighters.size(); ++k) {
                        if (r.fighters[k].player) continue;
                        if (m < 0) m = int(k);
                        else r.fighters[k].hp = 0;
                    }
                    cr3::Cr3Fighter f;
                    check(m >= 0 && r.monsterFighter(caster, 10, f), "a release monster as the caster");
                    f.unit = r.fighters[size_t(m)].unit, f.actionsPerTurn = 1;
                    r.fighters[size_t(m)] = f;
                    r.noAssist = true, r.partyUnhurt = true;
                    return m;
                };
                auto guards = [](const cr3::Cr3BattleRules& r) {
                    std::vector<std::array<int, 3>> g;
                    for (size_t k = 0; k < r.fighters.size(); ++k)
                        if (r.fighters[k].player) g.push_back({int(k), -1, -1});
                    return g;
                };
                {
                    // 1742 (Pigtapir Tank 1405's): its own species at 180 / 256; the fallen banked first
                    cr3::Cr3BattleRules r;
                    const int m = special(r, 77, 1405);
                    cr3::Cr3Fighter fallen = r.fighters[size_t(m)];
                    fallen.hp = 0, fallen.level = 7;
                    r.fighters.push_back(fallen);
                    const int slot = int(r.fighters.size()) - 1;
                    int came = 0, tries = 0;
                    for (int n = 0; n < 3000; ++n) {
                        r.fighters[size_t(slot)] = fallen, r.banked.clear();
                        r.scripted[m].push_back({1, 1742});
                        for (const cr3::Cr3Action& a : r.turn(guards(r))) {
                            if (a.source != m || a.command != 1742) continue;
                            ++tries;
                            check(a.reinforced.fighter == slot, "1742 aims at the fallen slot");
                            const cr3::Cr3Fighter& now = r.fighters[size_t(slot)];
                            if (a.reinforced.success) {
                                ++came;
                                check(a.reinforced.monster == 1405 && now.id == 1405 && now.hp == now.maxHp && now.mp == now.maxMp && now.effects.empty() &&
                                          now.mode == 0 && r.banked.size() == 1 && r.banked[0].dead() && r.banked[0].level == 7,
                                      "1742: the caster's species, full, mode 0, no effects; the fallen banked as it was");
                            } else {
                                check(now.dead() && r.banked.empty(), "1742 failed: the slot still fallen, nothing banked");
                            }
                        }
                    }
                    const double share = 100.0 * came / std::max(tries, 1);
                    std::cout << "1742's call for help over " << tries << " tries: " << share << " % came\n";
                    check(tries == 3000 && std::abs(share - 70.3) < 2.5, "1742 calls its own species at 180 / 256 (70.3 %)");
                }
                {
                    // Round 28: VRainforceToCalculation's trigger 0 then BtlStatusEffectList::Clear - the Death Mask 554 (skill 454:
                    // no trigger, kind 60 -> status effect 79) called in by 1742
                    cr3::Cr3BattleRules r;
                    const int m = special(r, 78, 554);
                    (void)r.triggerSkills(m, 0); // (special() swaps the fighter in after setup's trigger 0)
                    check(r.hasEffect(m, 79), "554's trigger 0 gives it status effect 79 at the start (skill 454)");
                    cr3::Cr3Fighter fallen = r.fighters[size_t(m)];
                    fallen.hp = 0;
                    r.fighters.push_back(fallen);
                    const int slot = int(r.fighters.size()) - 1;
                    bool came = false;
                    for (int n = 0; n < 50 && !came; ++n) {
                        r.fighters[size_t(slot)] = fallen, r.banked.clear();
                        r.scripted[m].push_back({1, 1742});
                        for (const cr3::Cr3Action& a : r.turn(guards(r)))
                            if (a.source == m && a.command == 1742 && a.reinforced.success) came = true;
                    }
                    // (79 is back afterwards: the next trigger - 11, after the command - gives a no-trigger skill's kind 60 again)
                    const auto& got = r.fighters[size_t(slot)];
                    check(came && got.id == 554 && got.hp == got.maxHp && r.hasEffect(slot, 79) && got.effects.size() == 1 &&
                              got.effectStacks.count(79) && got.effectStacks.at(79) == 1,
                          "a reinforcement: trigger 0, its status effects cleared, the no-trigger skill's 79 given again once");
                }
                {
                    // 1773 (Ark Raptor 1555's): the constant's species - 1554 Raptor -, always; its level the row's or the slot's
                    cr3::Cr3BattleRules r;
                    const int m = special(r, 78, 1555);
                    cr3::Cr3Fighter fallen = r.fighters[size_t(m)];
                    fallen.hp = 0, fallen.level = 9;
                    r.fighters.push_back(fallen);
                    const int slot = int(r.fighters.size()) - 1;
                    r.scripted[m].push_back({1, 1773});
                    bool seen = false;
                    for (const cr3::Cr3Action& a : r.turn(guards(r)))
                        if (a.source == m && a.command == 1773) seen = a.reinforced.success && a.reinforced.monster == 1554;
                    cr3::Cr3Fighter want;
                    check(r.monsterFighter(1554, 9, want), "monster 1554");
                    const cr3::Cr3Fighter& now = r.fighters[size_t(slot)];
                    check(seen && now.id == 1554 && now.level == want.level && now.hp == want.maxHp && r.outcome() == 0 && r.banked.size() == 1 &&
                              r.banked[0].id == 1555,
                          "1773 calls a Raptor 1554 into the fallen slot, at full HP");
                }
                {
                    // 387: the order reversed for the rest of the cast turn and four more, then command 58 with no source
                    cr3::Cr3BattleRules r;
                    const int m = special(r, 79, 554);
                    check(r.fighters[size_t(m)].unlimitedMp && r.fighters[size_t(m)].maxMp == 0,
                          "554's MP -1: unlimited (HaveEnoughMp 0x7e2828 passes MP below 0) - 387's 16 MP not wanted");
                    int p = -1;
                    for (size_t k = 0; k < r.fighters.size() && p < 0; ++k)
                        if (r.fighters[k].player) p = int(k);
                    r.fighters.push_back(r.fighters[size_t(p)]);
                    const int q = int(r.fighters.size()) - 1;
                    r.fighters[size_t(p)].speed = 300, r.fighters[size_t(q)].speed = 3, r.fighters[size_t(p)].weight = r.fighters[size_t(q)].weight = 0;
                    auto guardOrder = [&](const std::vector<cr3::Cr3Action>& acts) {
                        std::vector<int> order;
                        for (const cr3::Cr3Action& a : acts)
                            if (a.source == p || a.source == q) order.push_back(a.source);
                        return order;
                    };
                    r.scripted[m].push_back({1, 387});
                    const auto cast = r.turn(guards(r));
                    bool opened = false;
                    for (const cr3::Cr3Action& a : cast) opened |= a.source == m && a.command == 387 && a.fieldToggle == 1;
                    check(opened && r.reverseSpeed && r.reverseSpeedTurns == 4 && guardOrder(cast) == std::vector<int>({p, q}),
                          "387 opens the strange space after the turn's guards (bucket 2) - 4 turns left at the cast turn's end");
                    for (int n = 1; n <= 4; ++n) {
                        r.monstersWait = true;
                        const auto acts = r.turn(guards(r));
                        check(guardOrder(acts) == std::vector<int>({q, p}), "reverse gravity: the slowest first");
                        const bool over = !acts.empty() && acts.back().source == -1 && acts.back().command == 58;
                        check(over == (n == 4) && r.reverseSpeed == (n < 4) && r.reverseSpeedTurns == 4 - n,
                              "reverse gravity over at the end of the fourth turn after the cast, with command 58");
                    }
                    r.monstersWait = true;
                    check(guardOrder(r.turn(guards(r))) == std::vector<int>({p, q}), "reverse gravity over: the fastest first again");
                    // a recast within its turns closes it - no command 58
                    r.scripted[m].push_back({1, 387});
                    (void)r.turn(guards(r));
                    r.scripted[m].push_back({1, 387});
                    const auto again = r.turn(guards(r));
                    bool closed = false, line58 = false;
                    for (const cr3::Cr3Action& a : again) closed |= a.command == 387 && a.fieldToggle == -1, line58 |= a.command == 58;
                    check(closed && !line58 && !r.reverseSpeed && r.reverseSpeedTurns == 0, "387 again within its turns: closed (line 183), no command 58");
                }
                {
                    // 1741 (the Mimics' Escape): status effect 51 - kinds 72 + 33, not 76 -: defeated, not dead - the battle won
                    cr3::Cr3BattleRules r;
                    const int m = special(r, 80, 1810);
                    r.scripted[m].push_back({1, 1741});
                    bool gave = false;
                    for (const cr3::Cr3Action& a : r.turn(guards(r)))
                        if (a.source == m && a.command == 1741 && !a.targets.empty())
                            gave = std::find(a.targets[0].effectsGiven.begin(), a.targets[0].effectsGiven.end(), 51) != a.targets[0].effectsGiven.end();
                    check(gave && r.hasEffect(m, 51) && !r.fighters[size_t(m)].dead() && r.defeated(r.fighters[size_t(m)]) && r.outcome() == 1,
                          "1741: the Mimic escaped - status effect 51, alive (no EXP, money or drop: not IsDead), the battle won");
                }
                {
                    // Round 25 against the release: battle_0005's turn-end 72 Special Support (an assist direction, kind 13) revives
                    // and fills the party; Dance (56) makes a party member's command 1512; se 34 revives with 1759 three turns on
                    cr3::Cr3BattleRules r;
                    battleOf(2612, r, 25);
                    std::vector<int> party;
                    int foe = -1;
                    for (size_t k = 0; k < r.fighters.size(); ++k) (r.fighters[k].player ? party.push_back(int(k)) : void(foe = foe < 0 ? int(k) : foe));
                    check(!party.empty() && foe >= 0, "battle 2612: a party and a monster");
                    r.fighters.push_back(r.fighters[size_t(party[0])]), party.push_back(int(r.fighters.size()) - 1); // (a second member)
                    r.noAssist = true, r.partyUnhurt = true;
                    r.fighters[size_t(party[1])].hp = 0, r.fighters[size_t(party[0])].mp = 0;
                    const auto acts = r.scriptCommands({{72, -1}});
                    const cr3::Cr3Fighter& p0 = r.fighters[size_t(party[0])];
                    const cr3::Cr3Fighter& p1 = r.fighters[size_t(party[1])];
                    check(acts.size() == 1 && acts[0].assist && acts[0].source == -1 && acts[0].targets.size() == party.size() && p1.hp == p1.maxHp &&
                              p0.mp == p0.maxMp,
                          "release 72 Special Support: an assist direction, every player revived and filled (HP and MP)");
                    r.giveEffect(party[0], 56);
                    r.monstersWait = true;
                    bool danced = false;
                    for (const cr3::Cr3Action& a : r.turn({{party[0], r.attackCommandOf(party[0]), foe}}))
                        danced |= a.source == party[0] && a.command == 1512 && a.target == party[0];
                    check(danced && r.fighters[size_t(party[0])].effectsDormant.count(56), "release Dance (56): 1512 in place of the attack, end timing 3");
                    r.removeEffect(party[0], 56);
                    r.fighters[size_t(party[1])].hp = 0, r.giveEffect(party[1], 34);
                    int revived = -1;
                    for (int n = 1; n <= 5 && revived < 0; ++n) {
                        r.monstersWait = true;
                        (void)r.turn({{party[0], -1, -1}});
                        for (const cr3::Cr3Action& a : r.turnEndActions())
                            if (a.command == 1759 && a.target == party[1] && p1.hp == p1.maxHp) revived = n;
                        (void)r.endTurn();
                    }
                    check(revived == 4, "release se 34: 1759 revives its bearer at the end of the third turn on");
                }
            } else {
                std::cout << "CR3_DATA: " << err << " - the release checks skipped\n";
            }
        }
        std::cout << "battle target side: all checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
