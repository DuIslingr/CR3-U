// Generated parameter tables and fighters only; no game data is needed.
#include "battle/cr3rules.hpp"
#include "assets/mvgl.hpp"
#include <algorithm>
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
        // bucket-5 command 62 (priority -9), fire attack 70 (element 1), 3-hit chain attack 71 (repeat 3, formula 2).
        commands.rows.push_back({{0,51}, {1,1}, {4,0}, {25,2}, {33,1}});
        commands.rows.push_back({{0,60}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {25,0}, {26,80}, {33,1}});
        commands.rows.push_back({{0,61}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {25,uint32_t(-8)}, {33,1}});
        commands.rows.push_back({{0,62}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {24,100}, {25,uint32_t(-9)}, {33,1}});
        commands.rows.push_back({{0,70}, {1,1}, {4,3}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {9,1}, {24,100}, {32,4}, {33,1}});
        commands.rows.push_back({{0,71}, {1,1}, {4,3}, {5,800}, {6,bits(1)}, {7,bits(1)}, {8,0x103}, {24,100}, {32,4}, {33,2}});
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
        Table formulas{"CommandFomula", "iiii", {
            {{0,1},{2,1}}, {{0,2},{2,1},{3,3}}, {{0,5},{2,5}}, {{0,7},{2,7}},
            {{0,8},{2,8}}, {{0,12},{2,12}}, {{0,13},{2,13}}, {{0,16},{2,16}}, {{0,17},{2,18}},
            {{0,18},{2,6},{3,19}}, {{0,20},{2,20}}, {{0,24},{2,24}},
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
        }};
        Table statusEffects{"StatusEffect", "iiiiiiiiiiiiiiiiiiiiiii_ipiiiiiiii", {
            {{0,1}, {1,uint32_t(-1)}, {5,uint32_t(-1)}, {6,uint32_t(-1)}, {12,256}, {13,256}},
            {{0,200}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,129}, {15,80}}, // MP cost 80% (kind 129)
            {{0,301}, {1,10}, {3,1}, {5,3}, {6,3}, {7,2}, {12,256}, {13,256}, {14,37}, {15,50}, {32,0x10}}, // group 10, prio 1, maxAcc 2, +50% ATK (37), CanCancel (bit 0x10)
            {{0,302}, {1,10}, {3,2}, {5,3}, {6,3}, {7,1}, {12,256}, {13,256}, {14,37}, {15,100}, {32,0x10}}, // group 10, prio 2, +100% ATK, CanCancel
            {{0,303}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,68}, {15,301}}, // status immunity to 301 (kind 68)
            {{0,304}, {1,uint32_t(-1)}, {5,3}, {6,3}, {12,256}, {13,256}, {14,82}, {15,300}}, // +300/256 evasion (CR3 kind 82)
        }};
        check(t.skill.parse(tables({skills, statusEffects})), "parse synthetic skills and status effects");
        Table elements{"Element", "ifffffff", {
            {{0,0},{1,bits(1)},{2,bits(1)},{3,bits(1)},{4,bits(1)},{5,bits(1)},{6,bits(1)},{7,bits(1)}},
            {{0,1},{1,bits(1)},{2,bits(0.5f)},{3,bits(1.5f)},{4,bits(1)},{5,bits(1)},{6,bits(1)},{7,bits(1)}},
            {{0,2},{1,bits(1)},{2,bits(1.5f)},{3,bits(0.5f)},{4,bits(1)},{5,bits(1)},{6,bits(1)},{7,bits(1)}},
        }};
        check(t.battle.parse(tables({elements})), "parse synthetic element rates");
        Table monsters{"Monster", "iipiiiiiiiiiiiiiiiiiii", {{{0,1},{4,1},{6,5000},{7,100},{8,0x00010014},{9,0x00010001}}}};
        check(t.monster.parse(tables({monsters})), "parse synthetic fighters");
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
            const auto order = rules.turn({{0, 61, 4}, {1, 62, 4}, {2, 60, 4}, {3, -1, -1}});
            std::vector<int> sources;
            for (const auto& a : order) sources.push_back(a.source);
            check(sources == std::vector<int>({3, 2, 4, 0, 1}),
                  "BtlCommandList::Sort orders Guard (+2) > speed+speedCost (100) > normal (60) > bucket -8 > bucket -9");

            rules.reverseSpeed = true; rules.reverseSpeedTurns = 1;
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
            const auto elemActions = rules.turn({{0, 70, 2}, {1, 70, 3}});
            check(elemActions.size() == 2 && elemActions[0].targets[0].colors == std::vector<uint8_t>{1}, "elemental weakness records popup color 1");
            check(elemActions[1].targets[0].colors == std::vector<uint8_t>{2}, "elemental resistance records popup color 2");

            rules.monstersWait = true;
            const auto healActions = rules.turn({{0, 81, 0}, {1, -1, -1}});
            check(!healActions.empty() && healActions[1].targets[0].colors == std::vector<uint8_t>{3}, "HP heal records popup color 3");

            rules.monstersWait = true;
            const auto chainActions = rules.turn({{0, 71, 2}, {1, -1, -1}});
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
            const auto gravAct = rules.turn({{0, 800, 2}, {1, 1646, 2}});
            check(gravAct[0].targets[0].hits == std::vector<int>{300} && rules.fighters[2].hp == 1700,
                  "calculatePercentDamage applies (power - skill 35)% of current HP");
            check(gravAct[1].targets[0].mpHits == std::vector<int>{30} && gravAct[1].targets[0].colors == std::vector<uint8_t>{4} &&
                  rules.fighters[2].mp == 50,
                  "calculateMpDamage drains target MP with popup color 4");
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
            rules.scripted[2].push_back(1646);
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
            rules.turn({{0, 30, 2}, {1, -1, -1}});
            check(rules.gauge == 0, "passive skill kind 70 resets Break Gauge to 0 when party acts");
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
            rules.scripted[2].push_back(760), rules.scripted[3].push_back(760);
            auto acts = rules.turn({{0, 30, 2}});
            std::vector<std::array<int, 3>> seen; // (source, command, finisher)
            for (const auto& a : acts) seen.push_back({a.source, a.command, int(a.finisher)});
            check(seen.size() == 4 && seen[0][1] == 760 && seen[1][1] == 760 && !acts[0].combo && acts[1].combo &&
                      seen[2][0] == seen[1][0] && seen[2][1] == 761 && seen[2][2] == 1 && seen[3][0] == 0,
                  "a comboing pair's last attacker's Last Attack comes before the other side's action, marked a finisher");
            reset();
            rules.gauge = -1000; // (the monsters' full advantage: Finest Arts first - none here, so the Last Attack)
            rules.scripted[2].push_back(760), rules.scripted[3].push_back(760);
            acts = rules.turn({{0, 30, 2}});
            check(acts.size() == 4 && acts[2].finisher && acts[2].command == 761, "at full advantage without Finest Arts, still the Last Attack");
            reset();
            rules.fighters[3].hp = 0; // (one action alone: no finisher)
            rules.scripted[2].push_back(760);
            acts = rules.turn({{0, 30, 2}});
            bool any = false;
            for (const auto& a : acts) any |= a.finisher;
            check(!any, "a single action leads into no finisher");
        }
        std::cout << "battle target side: all checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
