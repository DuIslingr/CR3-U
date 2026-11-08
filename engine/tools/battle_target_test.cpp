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
};
Bytes tables(const std::vector<Table>& tables) {
    Bytes out{'E','X','P','A'}; append(out, uint32_t(tables.size()));
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
        for (const auto& values : t.rows) {
            const size_t base = out.size(); out.resize(base + size);
            for (const auto& [column, value] : values) put(out, base + columns.at(size_t(column)), value);
        }
    }
    out.insert(out.end(), {'C','H','N','K'}); append(out, 0); return out;
}
int main() {
    try {
        cr3::gWideAnimations = true;
        cr3::Cr3BattleTables t;
        constexpr const char* commandLayout = "iiiiiiffiii_ip_ip_ip_ip_ip_ip_ipiiiiiiiiiiiiiii_pi";
        Table commands{"Command", commandLayout, {}};
        // one/all flexible targets and their fixed-side equivalents; all use real damage rules.
        for (const auto [id, target, formula] : {std::array<int,3>{80,8,1}, {90,9,1}, {50,5,1}, {30,3,1}, {81,8,5}, {91,9,5}})
            commands.rows.push_back({{0,uint32_t(id)}, {1,1}, {4,uint32_t(target)}, {5,10}, {6,bits(1)}, {7,bits(1)}, {8,0x101}, {33,uint32_t(formula)}});
        Table formulas{"CommandFomula", "iiii", {{{0,1},{2,1}},{{0,5},{2,5}}}};
        check(t.command.parse(tables({commands, formulas})), "parse synthetic commands");
        Table monsters{"Monster", "iipiiiiiiiiiiiiiiiiiii", {{{0,1},{4,1},{6,1000},{7,100},{8,0x00010014},{9,0x00010001}}}};
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
        std::cout << "battle target side: all checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
