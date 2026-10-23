#include "battle/cr3rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cr3 {

namespace {

// the rows' layouts (assets/mbe.hpp)
constexpr const char* kCharStats = "iiiiiiiii_ip_ipiiiiiiii"; // 7 the guest gene, 8 the attack command; 13 HP, 14 MP, 15 atk | def, 16 spd | luck, 17 .. 20 their growth
constexpr const char* kMonsterStats = "iipiiiiiiiiiiiiiiiiiii";          // 4 level (+0x14), 5 element, 6 HP, 7 MP, 8 atk | def, 9 spd | luck, 10 .. 13 their growth
constexpr const char* kMonsterCommand = "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"; // id, mode, -, -, -, the attack command, -, -, then (command, ratio, count) x 8, skills x 8 (+0x80)
constexpr const char* kWeapon = "iiiiiiiiiiiiiiiiiiiiiiiiiii";   // 14 the constant (+0x38), 15 hits, 17 consumption %, 18 / 19 lowered physical / magic, 20.. lowered element 0 ..
constexpr const char* kArmor = "iiiiiiiiiiiii_piiiiiiiiiii_p";  // 15 physical (+0x44), 16 magic, 17 .. 23 element 0 .. 6
constexpr const char* kAccessory = "iiiiiiiiiiiiiiiiii";         // 12 HP (+0x30), 13 MP, 14 attack, 15 defence, 16 speed, 17 luck
constexpr const char* kGene = "iiiiiiiiiiiiiiiiiiiiiii";         // 13 HP (+0x34), 14 MP, 15 atk | def, 16 spd | luck, 17 .. 20 their growth
constexpr const char* kGuestGene = "iiiiiiiiiiiiiiiiiii";        // id, gene, level, then (type, id) x 8
constexpr const char* kStatusEffect = "iiiiiiiiiiiiiiiiiiiiii"; // id, .., 5 / 6 turns (+0x14 / +0x18), 12 / 13 chance (+0x30 / +0x34; with the advantage), 14 .. 21 four (kind, constant)
constexpr const char* kBattleBreak = "ffii_ip_ip_ip_ipiiiiiiii_ipip"; // 12 .. 19 the break bonuses (+0x50: with the advantage base, critical, down, kill; +0x60 without), 20 / 21 the status effects monsters resist (+0x70 / +0x78), 22 / 23 the cooperation scales (+0x80 / +0x88, floats)
constexpr const char* kSkill = "iiiiiiiiiiiiiiiiiiiii";          // 8 its condition (+0x20), 12 / 13 and 14 / 15 its effects (kind, constant)
constexpr const char* kCommand = "iiiiiiffiii_ip_ip_ip_ip_ip_ip_ipiiiiiiiiiiiiiii_pi"; // 5 power, 6 multiplier, 8 packed (attacks, hits, break), 27 spread (+0xa8), 32 flags, 33 formula (+0xc0)
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

// a party member's numbers (MbCharInfo at the team level, its equipment, its gene's at its level)
bool Cr3BattleRules::playerStats(const Cr3BattleTables& t, int character, Cr3Fighter& f) const {
    MbeFile::Row r;
        // (MbCharInfo at CrxCharacter +0x34: the team level, a guest's too)
        if (!t.character.find("Char", character, kCharStats, r)) return false;
        f.id = character, f.level = std::max(teamLevel, 1);
        f.maxHp = grown(r.i(13), r.i(17), f.level), f.maxMp = grown(r.i(14), r.i(18), f.level);
        f.attack = grown(low16(r.i(15)), low16(r.i(19)), f.level), f.defence = grown(high16(r.i(15)), high16(r.i(19)), f.level);
        f.speed = grown(low16(r.i(16)), low16(r.i(20)), f.level), f.luck = grown(high16(r.i(16)), high16(r.i(20)), f.level);
        f.attackCommand = r.i(8); // (+0x20: BtlCharacterStatus::VGetAttackCommandId)
        // its equipment (CrxCharacter::initialize: a guest's - +0x0c bit 0 - its row's; anyone
        // else's the save's - partyEquip -, where it has none put on yet the row's)
        const bool guest = (r.i(3) & 1) != 0;
        const auto worn = partyEquip.find(character);
        const auto equip = [&](int k) { return !guest && worn != partyEquip.end() && worn->second[size_t(k)] >= 0 ? worn->second[size_t(k)] : r.i(4 + size_t(k)); };
        MbeFile::Row e;
        if (equip(0) > 0 && t.item.find("Weapon", equip(0), kWeapon, e)) {
            f.weaponConstant = e.i(14), f.weaponHits = e.i(15), f.weaponBreak = e.i(16), f.consumption = float(e.i(17)) * 0.01f;
            f.lowerPhysical = e.i(18), f.lowerMagic = e.i(19);
            for (size_t k = 0; k < 7; ++k) f.lowerElement[k] = e.i(20 + k);
            f.weight += std::max(e.i(13), 0);
        }
        if (equip(1) > 0 && t.item.find("Armor", equip(1), kArmor, e)) {
            f.physical = e.i(15), f.magic = e.i(16);
            for (size_t k = 0; k < 7; ++k) f.elementResist[k] = e.i(17 + k);
            f.weight += std::max(e.i(14), 0);
        }
        if (equip(2) > 0 && t.item.find("Accessory", equip(2), kAccessory, e)) {
            f.maxHp += e.i(12), f.maxMp += e.i(13), f.attack += e.i(14), f.defence += e.i(15), f.speed += e.i(16), f.luck += e.i(17);
        }
        // a guest's gene (CrxGene(int): GuestGene - its gene at its level, its commands; the
        // prologue hero's row 1: gene 453 at 50, Rakshasa Lotus, Augmented Electra, Augmented
        // Aquinas, Augmented Healing (All), as the reference's list) - its numbers added
        MbeFile::Row g, gi;
        if (r.i(7) > 0 && t.gene.find("GuestGene", r.i(7), kGuestGene, g)) {
            f.geneLevel = g.i(2);
            for (size_t k = 3; k + 1 < 19; k += 2) {
                if (g.i(k + 1) <= 0) continue;
                (g.i(k) == 1 ? f.skills : f.geneCommands).push_back(g.i(k + 1));
            }
            if (t.gene.find("Gene", g.i(1), kGene, gi)) {
                const int lv = f.geneLevel;
                f.maxHp += grown(gi.i(13), gi.i(17), lv), f.maxMp += grown(gi.i(14), gi.i(18), lv);
                f.attack += grown(low16(gi.i(15)), low16(gi.i(19)), lv), f.defence += grown(high16(gi.i(15)), high16(gi.i(19)), lv);
                f.speed += grown(low16(gi.i(16)), low16(gi.i(20)), lv), f.luck += grown(high16(gi.i(16)), high16(gi.i(20)), lv);
            }
        }
        // (anyone else's: the gene it wears - its level, slots and numbers, the items' additions)
        if (const auto pg = partyGenes.find(character); pg != partyGenes.end() && !(r.i(7) > 0 && t.gene.find("GuestGene", r.i(7), kGuestGene, g))) {
            f.geneLevel = pg->second.level;
            for (const auto& sl : pg->second.slots) (sl[0] == 1 ? f.skills : f.geneCommands).push_back(sl[1]);
            if (t.gene.find("Gene", pg->second.master, kGene, gi)) {
                const int lv = f.geneLevel;
                f.maxHp += grown(gi.i(13), gi.i(17), lv), f.maxMp += grown(gi.i(14), gi.i(18), lv);
                f.attack += grown(low16(gi.i(15)), low16(gi.i(19)), lv) + pg->second.adds[0];
                f.defence += grown(high16(gi.i(15)), high16(gi.i(19)), lv) + pg->second.adds[1];
                f.speed += grown(low16(gi.i(16)), low16(gi.i(20)), lv) + pg->second.adds[2];
                f.luck += grown(high16(gi.i(16)), high16(gi.i(20)), lv) + pg->second.adds[3];
            }
        }
        f.attack = std::max(f.attack, 1), f.defence = std::max(f.defence, 1);
    return true;
}

bool Cr3BattleRules::setup(const Cr3BattleTables& t, const Cr3Battle& b, int monsterLevel, uint32_t seed, std::string* error) {
    partyDamage = 0, partyMp = 0, killed = 0;
    tables_ = &t;
    rng_ = seed ? seed : 1;
    fighters.clear();
    gauge = 0, gaugeStarted = false; // (BtlBreakGaugeUI: even)
    usedSkills_.clear();
    MbeFile::Row bt;
    resistIds_.clear();
    if (const MbeFile::Table* tb = t.battle.table("Battle"); tb && t.battle.row(*tb, 0, kBattleBreak, bt)) {
        for (size_t k = 0; k < 4; ++k) breakWith_[k] = bt.i(12 + k), breakWithout_[k] = bt.i(16 + k);
        for (int k = 0; k < bt.i(20) && k < 6; ++k) resistIds_.push_back(bt.element(21, size_t(k)));
        for (int k = 0; k < bt.i(22) && k < 4; ++k) {
            const int32_t bits = bt.element(23, size_t(k));
            std::memcpy(&coopScale_[size_t(k)], &bits, 4);
        }
    }
    for (size_t k = 0; k < b.units.size(); ++k) {
        const Cr3BattleUnit& u = b.units[k];
        Cr3Fighter f;
        f.unit = int(k), f.player = u.player;
        MbeFile::Row r;
        if (u.player) {
            if (!playerStats(t, u.character, f)) continue;
        } else {
            if (!t.monster.find("Monster", u.monster, kMonsterStats, r)) continue;
            // (BtlMonsterStatus::vInitialize: its own level, +0x14, where it has one - Gunner's and
            // Axer's 50, the target list's - else the reservation's)
            f.id = u.monster, f.level = r.i(4) > 0 ? r.i(4) : std::max(monsterLevel, 1), f.element = r.i(5);
            f.geneLevel = f.level; // (BtlMonsterStatus::VGetGeneLevel: its level)
            // (MbMonsterInfo::GetStatusEffectResistance: an s16 each at +0x48, in battle.mbe's
            // list's order - 13, 14, 15, 16, 1)
            for (size_t k = 0; k < 6; ++k)
                f.effectResist[k] = (k & 1) ? high16(r.i(17 + k / 2)) : low16(r.i(17 + k / 2));
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
                    for (size_t k = 32; k < 40; ++k)
                        if (m.i(k) > 0) f.skills.push_back(m.i(k)); // (its skills: Gunner's and Axer's 304 - element 4 resistance -50)
                    break;
                }
        }
        f.hp = f.maxHp, f.mp = f.maxMp;
        if (const auto left = partyMpLeft.find(f.id); f.player && left != partyMpLeft.end()) f.mp = std::clamp(left->second, 0, f.maxMp);
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
    c.hitsOwn = hits < 0;
    c.repeat = std::max(int(int8_t(uint32_t(r.i(8)) & 0xff)), 1); // (+0x20: its attacks - Rakshasa Lotus's 3; the magic number)
    c.breakPoint = int16_t(uint32_t(r.i(8)) >> 16);                 // (+0x22)
    c.flags = uint32_t(r.i(32));                                      // (+0xbc)
    c.hitRate = r.f(7);                                               // (+0x1c)
    for (int k = 0; k < r.i(13) && k < 8; ++k)
        if (r.element(14, size_t(k)) > 0) c.effects.push_back(r.element(14, size_t(k))); // (+0x40 / +0x48)
    c.spread = r.i(27);
    c.effectsAnnounce = int16_t(uint32_t(r.i(41)) & 0xffff);
    c.type = r.i(1);
    c.target = r.i(4);
    c.all = c.target == 2 || c.target == 4 || c.target == 5 || c.target == 9 || c.target == 11 || c.target == 13;
    c.hpCost = r.i(28) == 0 ? r.i(29) : 0;
    c.mpKind = r.i(30);
    c.mpCost = (c.mpKind == 0 || c.mpKind == 3) ? std::max(r.i(31), 0) : 0;
    c.element = r.i(9);
    MbeFile::Row f;
    c.formula = tables_->command.find("CommandFomula", r.i(33), kFormula, f) ? f.i(2) : 1;
    c.damage = c.formula;
    MbeFile::Row d;
    if (tables_->direction.find("CommandDirection", id, kDirection, d)) c.direction = d.i(1), c.motion = d.i(2);
    return true;
}

int Cr3BattleRules::skillConstant(const Cr3Fighter& f, int kind) const {
    if (!tables_ || kind < 0) return 0;
    int sum = 0;
    MbeFile::Row r;
    for (int id : f.skills)
        if (tables_->skill.find("Skill", id, kSkill, r) && r.i(8) == 0) { // (only those without a condition)
            if (r.i(12) == kind) sum += r.i(13);
            if (r.i(14) == kind) sum += r.i(15);
        }
    return sum;
}

std::vector<int> Cr3BattleRules::skillConstants(const Cr3Fighter& f, int kind) const {
    std::vector<int> out;
    MbeFile::Row r;
    if (tables_)
        for (int id : f.skills)
            if (tables_->skill.find("Skill", id, kSkill, r) && r.i(8) == 0) {
                if (r.i(12) == kind) out.push_back(r.i(13));
                if (r.i(14) == kind) out.push_back(r.i(15));
            }
    return out;
}

int Cr3BattleRules::skillKindMax(int skill, int kind) const {
    MbeFile::Row r;
    if (!tables_ || !tables_->skill.find("Skill", skill, kSkill, r)) return 0;
    int best = 0;
    if (r.i(12) == kind) best = std::max(best, r.i(13));
    if (r.i(14) == kind) best = std::max(best, r.i(15));
    return best;
}

int Cr3BattleRules::mpCost(const Cr3Fighter& f, const Cr3Command& c) const {
    // (MbCommandInfo::GetMpCost: 0 the value; 1 / 2 its percentage of the max MP, at least 1; 3 x
    // the weapon's consumption - CrxWeapon::GetConsumptionFactor, Weapon +0x44 / 100 -, at least 1;
    // the party's x GetMpCostPercent - kind 129's constants / 100, multiplied: a lifeline item's 0.5)
    const float less = f.player ? effectMultiplier(f, 129) : 1.0f;
    switch (c.mpKind) {
    case 0: return c.mpCost > 0 ? std::max(int(float(c.mpCost) * less), 1) : 0;
    case 1:
    case 2: return 0; // (not used by a command CR3-U plays yet)
    case 3: return c.mpCost > 0 ? std::max(int(float(c.mpCost) * f.consumption * less), 1) : 0;
    default: return 0;
    }
}

bool Cr3BattleRules::hasEffect(int fi, int id) const {
    if (fi < 0 || size_t(fi) >= fighters.size()) return false;
    for (const auto& [e, turns] : fighters[size_t(fi)].effects)
        if (e == id) return true;
    return false;
}

void Cr3BattleRules::giveEffect(int fi, int id, bool lasting) {
    // (BtlStatusEffectList::Add: its turns from its range - StatusEffect +0x14 .. +0x18, -1 until
    // removed; one already on is renewed)
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size()) return;
    MbeFile::Row r;
    if (!tables_->skill.find("StatusEffect", id, kStatusEffect, r)) return;
    int turns = r.i(5);
    if (turns >= 0 && r.i(6) > turns) turns += int(random() * float(r.i(6) - turns + 1));
    if (lasting) turns = -1; // (TakeStatusEffectToCalculation(id, -1): the field's, for good)
    Cr3Fighter& f = fighters[size_t(fi)];
    for (auto& [e, t] : f.effects)
        if (e == id) {
            t = turns;
            return;
        }
    f.effects.emplace_back(id, turns);
}

void Cr3BattleRules::removeEffect(int fi, int id) {
    if (fi < 0 || size_t(fi) >= fighters.size()) return;
    auto& v = fighters[size_t(fi)].effects;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const std::pair<int, int>& e) { return e.first == id; }), v.end());
}

// (BtlStatusEffectVisitor: each effect on - its four (kind, constant) - of the kind)
template <typename F> static void eachEffectKind(const MbeFile& table, const Cr3Fighter& f, int kind, F&& fn) {
    MbeFile::Row r;
    for (const auto& [id, turns] : f.effects)
        if (table.find("StatusEffect", id, kStatusEffect, r))
            for (size_t k = 0; k < 4; ++k)
                if (r.i(14 + k * 2) == kind) fn(r.i(15 + k * 2));
}

bool Cr3BattleRules::hasKind(const Cr3Fighter& f, int kind) const {
    bool on = false;
    if (tables_) eachEffectKind(tables_->skill, f, kind, [&](int) { on = true; });
    return on;
}

float Cr3BattleRules::effectMultiplier(const Cr3Fighter& f, int kind) const {
    float m = 1;
    if (tables_) eachEffectKind(tables_->skill, f, kind, [&](int c) { m *= float(c) / 100.0f; });
    return m;
}

int Cr3BattleRules::effectMax(const Cr3Fighter& f, int kind) const {
    int most = 0;
    if (tables_) eachEffectKind(tables_->skill, f, kind, [&](int c) { most = std::max(most, c); });
    return most;
}

int Cr3BattleRules::effectSum(const Cr3Fighter& f, int kind) const {
    int sum = 0;
    if (tables_) eachEffectKind(tables_->skill, f, kind, [&](int c) { sum += c; });
    return sum;
}

// (BtlStatus::GetAttack etc.: the unit's x its status effects' multipliers - kinds 36 attack, 38
// defence (GetGuardMultipliers), 40 speed, 42 luck)
int Cr3BattleRules::attackOf(const Cr3Fighter& f) const { return std::max(int(float(f.attack) * effectMultiplier(f, 36)), 1); }
int Cr3BattleRules::defenceOf(const Cr3Fighter& f) const { return std::max(int(float(f.defence) * effectMultiplier(f, 38)), 1); }
int Cr3BattleRules::speedOf(const Cr3Fighter& f) const { return std::max(int(float(f.speed) * effectMultiplier(f, 40)), 1); }
int Cr3BattleRules::luckOf(const Cr3Fighter& f) const { return std::max(int(float(f.luck) * effectMultiplier(f, 42)), 1); }

bool Cr3BattleRules::menuEnabled(int fi, int button) const {
    if (fi < 0 || size_t(fi) >= fighters.size()) return true;
    static const int kinds[4] = {140, 47, 66, 65};
    return button < 0 || button > 3 || !hasKind(fighters[size_t(fi)], kinds[button]);
}

bool Cr3BattleRules::hits(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c) {
    // (calculateHit: a target with kind 123 always evades; +0xbc bit 2 always hits; else a random
    // byte under +0x1c x (the speed ratio's 192 / 213 / 233 / 251 / 269 / 290 at 0.5 / 0.9 / 1.1 /
    // 1.3 / 1.5) x the attacker's kind 48 multipliers, less evasion - none read yet)
    if (hasKind(t, 123)) return false;
    if (c.flags & 4) return true;
    const float ratio = float(speedOf(s)) / float(speedOf(t));
    const int table = ratio < 0.5f ? 192 : ratio < 0.9f ? 213 : ratio < 1.1f ? 233 : ratio < 1.3f ? 251 : ratio < 1.5f ? 269 : 290;
    const int chance = int(effectMultiplier(s, 48) * float(int(c.hitRate * float(table))));
    return int(random() * 256.0f) < chance;
}

std::vector<std::pair<int, int>> Cr3BattleRules::endTurn() {
    std::vector<std::pair<int, int>> out;
    for (size_t k = 0; k < fighters.size(); ++k) {
        Cr3Fighter& f = fighters[k];
        if (f.dead()) continue;
        // (calculateStatusEffectHpChange: (kind 45 heal rate - kind 46 damage rate) x the max HP)
        const int change = int(float(effectSum(f, 45) - effectSum(f, 46)) / 100.0f * float(f.maxHp));
        if (change != 0) {
            f.hp = std::clamp(f.hp + change, change < 0 ? 1 : 0, f.maxHp);
            out.emplace_back(int(k), change);
        }
        // (BtlStatusEffect::Update: a turn off each, gone at 0; -1 stays)
        for (auto& [id, turns] : f.effects)
            if (turns > 0) --turns;
        f.effects.erase(std::remove_if(f.effects.begin(), f.effects.end(), [](const std::pair<int, int>& e) { return e.second == 0; }),
                        f.effects.end());
    }
    return out;
}

void Cr3BattleRules::addBreak(int points) {
    // (BtlBreakPoint::Add: the battle's first points +-500 whatever they are; from above 0 to 0 or
    // below another -500 first - a break -, from below to 0 or above +500; held within +-1000)
    if (points == 0) return;
    if (!gaugeStarted) points = points > 0 ? 500 : -500;
    gaugeStarted = true;
    if (gauge > 0 && gauge + points < 1) gauge = std::clamp(gauge - 500, -1000, 1000);
    else if (gauge < 0 && gauge + points >= 0) gauge = std::clamp(gauge + 500, -1000, 1000);
    gauge = std::clamp(gauge + points, -1000, 1000);
}

int Cr3BattleRules::damage(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, bool* critOut, bool* downOut) {
    const bool adv = advantage(s.player);
    // (calculateCriticalHit: +0xbc bit 4 lets it, bit 5 always; else a random byte under the
    // attacker's luck / the target's - below 0.5 8, 0.7 13, 0.9 18, 1.1 24, 1.3 29, 1.5 34, else 39)
    bool crit = false;
    if (c.flags & 0x10) {
        if (c.flags & 0x20) crit = true;
        else {
            const float luck = float(luckOf(s)) / float(luckOf(t));
            // (+ BtlStatusEffectUtility::GetCriticalHitChance: kind 44's constants - a lifeline item's 50)
            const int chance = (luck < 0.5f ? 8 : luck < 0.7f ? 13 : luck < 0.9f ? 18 : luck < 1.1f ? 24 : luck < 1.3f ? 29 : luck < 1.5f ? 34 : 39) +
                               effectSum(s, 44);
            crit = int(random() * 256.0f) < chance;
        }
    }
    const int e = std::clamp(c.element, 0, 6);
    float rate = 1;
    if (tables_) {
        MbeFile::Row er;
        if (tables_->battle.find("Element", e, "ifffffff", er) && t.element >= 0 && t.element < 7) rate = er.f(size_t(1 + t.element));
    }
    // (calculateDown: the target downs where the element's rate is above 1, on a critical, where the
    // command always downs - bit 6 - or where it is already down)
    const bool down = crit || t.comboChance || rate > 1.0f || (c.flags & 0x40);
    if (critOut) *critOut = crit;
    if (downOut) *downOut = down;

    float ratio = float(attackOf(s)) / float(defenceOf(t));
    ratio = std::clamp(ratio, 0.2f, 5.0f);
    // (below 0: the unit's own - VGetWeaponConstant: a character's weapon's, a monster's attack
    // command's power)
    int power = c.power;
    if (power < 0) {
        Cr3Command own;
        power = s.player ? s.weaponConstant
                         : (s.attackCommand != c.id && command(s.attackCommand, own) && own.power >= 0 ? own.power : 0);
    }
    float d = ratio * float(power) * 0.5f;
    // ("Status": x 1.5 with the gauge's advantage, + 0.1 a down, + 0.5 a critical)
    d *= (adv ? 1.5f : 1.0f) + (down ? 0.1f : 0.0f) + (crit ? 0.5f : 0.0f);
    // ("Element": the attack element's row of battle.mbe Element, the target's element's column)
    d *= rate;
    // ("Resistance": the attacker's lowered element resistance - (the target's physical or magic
    // resistance + its element resistance) + the attacker's lowered physical or magic one; the
    // element's skill kinds BtlElement's: 3 + 4e a resistance, 4 + 4e a lowered one)
    const bool magic = c.damage == 2;
    const int lowerElem = (e > 0 ? skillConstant(s, 4 + 4 * e) : 0) + s.lowerElement[size_t(e)];
    const int lower = magic ? skillConstant(s, 5) + s.lowerMagic : skillConstant(s, 2) + s.lowerPhysical;
    const int resist = magic ? skillConstant(t, 4) + t.magic : skillConstant(t, 1) + t.physical;
    const int elemResist = (e > 0 ? skillConstant(t, 3 + 4 * e) : 0) + t.elementResist[size_t(e)];
    int r = lowerElem - (resist + elemResist) + lower;
    if (r < -99) r = -100;
    d *= float(r + 100) / 100.0f;
    d *= float(s.geneLevel) / 20.0f + 1.0f; // ("Level": VGetGeneLevel)
    d *= c.multiplier > 0 ? c.multiplier : 1.0f;
    d *= effectMultiplier(t, 34); // ("Status Effect": the target's kind 34 - Guard's status effect 2, x 0.5)
    d *= coopNow_;                // (x MbBattleInfo::GetCooperationScale: a cooperation's units)
    int out = int(d);
    // (calculatePhysicalDamage / calculateMagicDamage: x the attacker's kind 54 / 55 multipliers)
    out = int(float(out) * effectMultiplier(s, magic ? 55 : 54));
    if (c.spread > 0) out = int(((random() - 0.5f) * float(c.spread) * 2 + 100.0f) / 100.0f * float(out));
    // (calculateMagicChainBonus: x (1 + the target's combo count x 0.001))
    if (magic) out = int((float(t.combo) * 0.001f + 1.0f) * float(out));
    return std::max(out, 1);
}

int Cr3BattleRules::heal(const Cr3Fighter& s, const Cr3Command& c) {
    // (calculateHpHeal: clamp(defence / attack, 0.5, 2) x power x (gene level / 20 + 1) x (1 + the
    // skills' heal correction - none yet))
    float ratio = std::clamp(float(defenceOf(s)) / float(attackOf(s)), 0.5f, 2.0f);
    return std::max(int(ratio * float(c.power) * (float(s.geneLevel) / 20.0f + 1.0f)), 1);
}

bool Cr3BattleRules::targetable(int i) const {
    if (i < 0 || size_t(i) >= fighters.size()) return false;
    const Cr3Fighter& f = fighters[size_t(i)];
    return !f.dead() && !hasKind(f, 32) && !hasKind(f, 72);
}

int Cr3BattleRules::livingTarget(bool player, int preferred) {
    if (preferred >= 0 && size_t(preferred) < fighters.size() && targetable(preferred) && fighters[size_t(preferred)].player == player)
        return preferred;
    std::vector<int> alive;
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player == player && targetable(int(k))) alive.push_back(int(k));
    if (alive.empty()) // (none to take: a living one all the same)
        for (size_t k = 0; k < fighters.size(); ++k)
            if (fighters[k].player == player && !fighters[k].dead()) alive.push_back(int(k));
    if (alive.empty()) return -1;
    return alive[size_t(random() * float(alive.size())) % alive.size()];
}

bool Cr3BattleRules::effectLands(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, int id) {
    // (BtlCalculator::IsStatusEffectHit: +0xbc bit 3 always; else a random byte under its chance
    // - StatusEffect +0x30, +0x34 with the advantage - + the luck ratio's -52 / -26 / -13 / 0 / 13
    // / 26 / 52 at 0.5 / 0.7 / 0.9 / 1.1 / 1.3 / 1.5, less the target's resistance - a monster's,
    // to battle.mbe's listed effects)
    if (c.flags & 8) return true;
    MbeFile::Row r;
    if (!tables_ || !tables_->skill.find("StatusEffect", id, kStatusEffect, r)) return false;
    const float luck = float(luckOf(s)) / float(luckOf(t));
    const int bonus = luck < 0.5f ? -52 : luck < 0.7f ? -26 : luck < 0.9f ? -13 : luck < 1.1f ? 0 : luck < 1.3f ? 13 : luck < 1.5f ? 26 : 52;
    int resist = 0;
    for (size_t k = 0; k < resistIds_.size() && k < t.effectResist.size(); ++k)
        if (resistIds_[k] == id) resist = t.effectResist[k];
    const int chance = (advantage(s.player) ? r.i(13) : r.i(12)) + bonus - resist;
    return int(random() * 256.0f) < std::max(chance, 0);
}

std::vector<Cr3Action> Cr3BattleRules::turn(const std::vector<std::array<int, 3>>& choices) {
    std::vector<Cr3Action> out;
    // the party's (their choices) and the monsters' (by their commands' ratios): the side with the
    // gauge's advantage first, the party at even
    std::vector<std::array<int, 4>> order, monsters; // (fighter, command, target, 1: a skill's added command)
    for (const auto& c : choices)
        if (c[0] >= 0 && size_t(c[0]) < fighters.size() && fighters[size_t(c[0])].player) order.push_back({c[0], c[1], c[2], 0});
    const bool wait = monstersWait;
    monstersWait = false;
    for (size_t k = 0; k < fighters.size() && !wait; ++k) {
        const Cr3Fighter& f = fighters[k];
        if (f.player || f.dead()) continue;
        if (const auto sc = scripted.find(int(k)); sc != scripted.end() && !sc->second.empty()) { // (its script's, in its own's place)
            for (int id : sc->second) monsters.push_back({int(k), id, -1, 0});
            continue;
        }
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
        monsters.push_back({int(k), cmd, -1, 0});
    }
    order.insert(advantage(false) ? order.begin() : order.end(), monsters.begin(), monsters.end());
    scripted.clear();
    int lastSide = -1; // (BtlCommandManager::Clear: none yet this turn)
    bool lastHadChance = false, lastContinued = false;
    std::vector<int> lastTargets;
    for (size_t at = 0; at < order.size(); ++at) {
        const auto [src, cmd, tgt, fromSkill] = order[at];
        Cr3Fighter& s = fighters[size_t(src)];
        if (s.dead()) continue;
        Cr3Action a;
        a.source = src, a.command = cmd;
        // (MbStatusEffectInfo::IsNoAction: a kind 31 effect - stunned, asleep - and it does nothing;
        // a skill's command plays all the same)
        if (hasKind(s, 31) && !fromSkill) {
            a.command = -2, a.gaugeAfter = gauge;
            out.push_back(a);
            continue;
        }
        // (BtlCommandManager::resetChance: the other side acting clears every down - ResetAllChance;
        // the same side's next keeps them - the combo - unless the last action began with one and
        // did not carry it on: its targets' then go - BtlDamageChunk::ResetCombo)
        if (lastSide >= 0 && lastSide != int(s.player))
            for (Cr3Fighter& f : fighters) f.comboChance = false, f.combo = 0;
        else if (lastSide >= 0 && lastHadChance && !lastContinued)
            for (int ti : lastTargets) fighters[size_t(ti)].comboChance = false, fighters[size_t(ti)].combo = 0;
        lastSide = int(s.player);
        lastHadChance = std::any_of(fighters.begin(), fighters.end(), [](const Cr3Fighter& f) { return f.comboChance; });
        lastContinued = false;
        lastTargets.clear();
        if (cmd < 0) { // (Guard: command 51 - its status effect 2, damage taken x 0.5 till the turn's end)
            Cr3Command g;
            if (command(51, g))
                for (int e : g.effects) giveEffect(src, e);
            a.gaugeAfter = gauge;
            out.push_back(a);
            continue;
        }
        Cr3Command c;
        if (!command(cmd, c)) c.power = s.attack, c.hits = 1, c.damage = 1;
        Cr3Command own; // (hits below 0: VGetWeaponHitNum - a character's weapon's, a monster's attack command's)
        if (c.hitsOwn) c.hits = s.player ? std::max(s.weaponHits, 1) : command(s.attackCommand, own) ? own.hits : 1;
        c.hits *= c.repeat;
        // (calculateCooperation: a command allowing it - +0xbc bit 7 -, not a skill's, its unit free of
        // kind 47 for a gene's; a random byte under 154 with the gauge's advantage, else 77, + its
        // skills' kind 80 (GetCooperationCorrection) - then GetCooperation: each later command of
        // its side's the same, on the same target for one enemy, its unit able, joins - taken from
        // the list)
        if ((c.flags & 0x80) && !fromSkill && !((c.flags & 1) && hasKind(s, 47))) {
            const int chance = (advantage(s.player) ? 154 : 77) + skillConstant(s, 80);
            if (int(random() * 256.0f) < chance)
                for (size_t k = at + 1; k < order.size();) {
                    const std::array<int, 4> o = order[k];
                    const Cr3Fighter& p = fighters[size_t(o[0])];
                    if (p.player == s.player && !p.dead() && !hasKind(p, 31) && !hasKind(p, 47) && o[3] == 0 && o[1] == cmd &&
                        (c.target != 3 || o[2] == tgt)) {
                        a.partners.push_back(o[0]);
                        order.erase(order.begin() + long(k));
                    } else {
                        ++k;
                    }
                }
        }
        coopNow_ = coopScale_[std::min(a.partners.size() + 1, coopScale_.size() - 1)];
        a.mpCost = std::min(mpCost(s, c), s.mp);
        s.mp -= a.mpCost, s.hp = std::max(s.hp - c.hpCost, 1);
        if (s.player) partyMp += a.mpCost;
        for (int pi : a.partners) {
            Cr3Fighter& p = fighters[size_t(pi)];
            const int m = std::min(mpCost(p, c), p.mp);
            p.mp -= m;
            if (p.player) partyMp += m;
        }
        const bool ally = c.ally();
        bool anyCrit = false, anyDown = false;
        std::vector<int> targets;
        // (the target kind's side - BtlStatusList::collectDamageTarget*, by MbTargetType: 0 self, 1 a
        // party member, 2 all of them, 3 an enemy, 4 all enemies, 5 everyone, 6 a random enemy, 8 / 9
        // either side's one / all - a heal its own -, 10 / 11 the player's, 12 / 13 the other party
        // members' one / all)
        const bool ownSide = c.target == 2 || c.target == 11 || c.target == 13 || (c.target == 9 && ally);
        const bool ownOne = c.target == 1 || c.target == 12 || (c.target == 8 && ally);
        if (c.all)
            for (size_t k = 0; k < fighters.size(); ++k) {
                if ((c.target == 5 || fighters[k].player == (ownSide ? s.player : !s.player)) && targetable(int(k)) &&
                    (c.target != 13 || int(k) != src))
                    targets.push_back(int(k));
            }
        else if (c.target == 0) targets.push_back(src);
        else if (ally || ownOne) { // (the one chosen on its own side, else - a monster's - one at random; 12 never itself)
            if (tgt >= 0 && fighters[size_t(tgt)].player == s.player && !fighters[size_t(tgt)].dead() && (c.target != 12 || tgt != src)) targets.push_back(tgt);
            else {
                std::vector<int> side;
                for (size_t k = 0; k < fighters.size(); ++k)
                    if (fighters[k].player == s.player && !fighters[k].dead() && (c.target != 12 || int(k) != src)) side.push_back(int(k));
                if (!side.empty()) targets.push_back(side[size_t(random() * float(side.size())) % side.size()]);
            }
        }
        else if (int t = livingTarget(!s.player, tgt); t >= 0) targets.push_back(t);
        if (targets.empty()) break; // (no one left on the other side)
        std::vector<int> downed; // (its targets downed: their commands wait - delayCommandByDown)
        for (int ti : targets) {
            Cr3Fighter& t = fighters[size_t(ti)];
            Cr3Hit hit;
            hit.target = ti;
            bool landed = false;
            if (c.damage < 0) { // (CommandFomula -1: no damage - its status effects alone)
                landed = true;
            } else if (ally) {
                const int h = heal(s, c);
                hit.hits.push_back(-h), hit.flags.push_back(0);
                t.hp = std::min(t.hp + h, t.maxHp);
                landed = true;
            } else {
                // (every hit plays, past the target's death - the reference's 1089 on a 400 HP
                // Heterocera; the gauge's state is the action's own: its points land after it)
                lastTargets.push_back(ti);
                std::vector<int> who{src}; // (a cooperation's units in turn, each its hits)
                who.insert(who.end(), a.partners.begin(), a.partners.end());
                for (int wi : who)
                    for (int wh = 0; wh < c.hits; ++wh) {
                        const Cr3Fighter& u = fighters[size_t(wi)];
                        if (!hits(u, t, c)) { // (a miss: no damage, no points)
                            hit.hits.push_back(0), hit.flags.push_back(4);
                            continue;
                        }
                        landed = true;
                        bool crit = false, down = false;
                        lastContinued |= t.comboChance;
                        const int d = damage(u, t, c, &crit, &down);
                        hit.hits.push_back(d);
                        hit.flags.push_back(uint8_t((crit ? 1 : 0) | (down ? 2 : 0)));
                        // (BtlBasicStatus::TakeDamage: no lower than the least HP its effects allow -
                        // kind 53 a number, 121 a percentage of the max: an endure; a skill's added
                        // command past it - the spider's finisher)
                        const int least = fromSkill ? 0 : std::max(effectMax(t, 53), int(float(effectMax(t, 121)) * 0.01f * float(t.maxHp)));
                        const int before = t.hp;
                        t.hp = std::max(t.hp - d, t.hp > 0 ? std::min(least, t.hp) : 0);
                        if (s.player && !t.player) partyDamage = std::min(partyDamage + d, 9999999L), killed += before > 0 && t.hp <= 0;
                        if (down) t.comboChance = true, ++t.combo; // (BtlBasicStatus::TakeDamage, addComboNum)
                        anyCrit |= crit, anyDown |= down;
                        if (down && std::find(downed.begin(), downed.end(), ti) == downed.end()) downed.push_back(ti);
                    }
            }
            // its status effects, where it landed and the target lives (IsStatusEffectHit)
            if (landed && !t.dead())
                for (int e : c.effects)
                    if (effectLands(s, t, c, e)) giveEffect(ti, e), hit.effectsGiven.push_back(e);
            hit.hpAfter = t.hp, hit.killed = t.dead();
            a.targets.push_back(hit);
        }
        // its break points (calculateBreakPoint, BtlDamageTarget::SetBreakPoint): each landed hit its
        // base - the command's +0x22, or with flag bit 1 the unit's weapon's break constant - and a
        // share of the action's bonus (bit 1: +0x22; + battle.mbe Battle +0x60 without the advantage
        // - 100, a critical 50, a down 50 - or +0x50 with it - 0, 25, 25 -); a kill adds +0x6c / +0x5c
        // (100 / 50: addKillBreakPointBonus) to its target's last; a monster's negative; added hit by
        // hit (AddBreakPointToCalculation)
        if (!ally && c.breakPoint >= 0) {
            int base = c.breakPoint, bonus = 0;
            if (c.flags & 2) {
                base = s.player ? s.weaponBreak : command(s.attackCommand, own) ? std::max(own.breakPoint, 0) : 0;
                bonus = c.breakPoint;
            }
            const std::array<int, 4>& table = advantage(s.player) ? breakWith_ : breakWithout_;
            bonus += table[0];
            if (anyCrit) bonus += table[1];
            if (anyDown) bonus += table[2];
            int left = bonus, targetsLeft = int(a.targets.size());
            for (Cr3Hit& th : a.targets) {
                int share = targetsLeft > 0 ? left / targetsLeft : 0;
                left -= share, --targetsLeft;
                int landedHits = 0;
                for (uint8_t fl : th.flags) landedHits += (fl & 4) ? 0 : 1;
                int hitsLeft = landedHits, lastLanded = -1;
                for (size_t h = 0; h < th.flags.size(); ++h)
                    if (!(th.flags[h] & 4)) lastLanded = int(h);
                for (size_t h = 0; h < th.hits.size(); ++h) {
                    if (!(th.flags[h] & 4)) {
                        const int part = hitsLeft > 0 ? share / hitsLeft : 0;
                        share -= part, --hitsLeft;
                        int points = base + part;
                        if (th.killed && int(h) == lastLanded) points += table[3];
                        addBreak(points * (s.player ? 1 : -1));
                    }
                    th.gauge.push_back(gauge);
                }
            }
        }
        for (Cr3Hit& th : a.targets)
            while (th.gauge.size() < th.hits.size()) th.gauge.push_back(gauge);
        a.gaugeAfter = gauge;
        a.target = a.targets[0].target, a.hits = a.targets[0].hits, a.hpAfter = a.targets[0].hpAfter, a.killed = a.targets[0].killed;
        out.push_back(a);
        if (outcome() != 0) break;
        // (BtlSkill::Trigger: a skill adding a command - kind 100 - whose condition now holds, once;
        // condition 28: every foe's HP at or under its constant - the guest hero's 430, command 126
        // once the spider endures at 1)
        if (tables_)
            for (size_t k = 0; k < fighters.size(); ++k) {
                const Cr3Fighter& f = fighters[k];
                if (f.dead()) continue;
                for (int id : f.skills) {
                    MbeFile::Row r;
                    if (!tables_->skill.find("Skill", id, kSkill, r) || usedSkills_.count({int(k), id})) continue;
                    const int added = r.i(12) == 100 ? r.i(13) : r.i(14) == 100 ? r.i(15) : -1;
                    if (added < 0 || r.i(8) != 28) continue;
                    int most = 0;
                    for (const Cr3Fighter& o : fighters)
                        if (o.player != f.player) most = std::max(most, o.hp);
                    if (most > r.i(9)) continue;
                    usedSkills_.insert({int(k), id});
                    order.insert(order.begin() + long(at) + 1, {int(k), added, -1, 1});
                }
            }
        // (BtlDamageTarget::TriggerAfterDamageToCalculation - trigger 7, a hit landed on it: a
        // target's skills whose condition holds - 3 its HP at or under the constant's percentage,
        // IsHpPercentLessEqual -, once; kind 97's command interrupts, next - AddInterruptCommand -,
        // kind 100's follows it - AddCommand: battle_0005's 1109 and its 1756 at 60%)
        if (tables_)
            for (const Cr3Hit& th : a.targets) {
                const Cr3Fighter& t = fighters[size_t(th.target)];
                bool hurt = false;
                for (size_t h = 0; h < th.hits.size(); ++h) hurt |= th.hits[h] > 0 && !(th.flags[h] & 4);
                if (t.dead() || !hurt) continue;
                size_t next = at + 1;
                for (int id : t.skills) {
                    MbeFile::Row r;
                    if (!tables_->skill.find("Skill", id, kSkill, r) || r.i(7) != 7 || r.i(8) != 3 || usedSkills_.count({th.target, id})) continue;
                    if (float(t.hp) * 100.0f > float(r.i(9)) * float(t.maxHp)) continue;
                    usedSkills_.insert({th.target, id});
                    for (int e = 0; e < 2; ++e)
                        if (r.i(12 + 2 * e) == 97 || r.i(12 + 2 * e) == 100)
                            order.insert(order.begin() + long(next++), {th.target, r.i(13 + 2 * e), -1, 1});
                }
            }
        // (delayCommandByDown: a downed unit's command still to come goes to the back)
        for (int d : downed)
            for (size_t k = at + 1; k < order.size(); ++k)
                if (order[k][0] == d && order[k][3] == 0) { // (a skill's - an interrupt - stays)
                    const auto moved = order[k];
                    order.erase(order.begin() + long(k));
                    order.push_back(moved);
                    break;
                }
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
