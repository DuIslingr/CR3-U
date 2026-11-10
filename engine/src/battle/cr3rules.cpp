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
constexpr const char* kGene = "iiiiiiiiiiiiiiiiiiiiiii";         // 12 element (+0x30), 13 HP (+0x34), 14 MP, 15 atk | def, 16 spd | luck, 17 .. 20 their growth
constexpr const char* kGuestGene = "iiiiiiiiiiiiiiiiiii";        // id, gene, level, then (type, id) x 8
constexpr const char* kStatusEffect = "iiiiiiiiiiiiiiiiiiiiii"; // id, group, -, priority, -, 5 / 6 turns, 7 maxAccumulation, .., 12 / 13 chance, 14 .. 21 four (kind, constant)
constexpr const char* kStatusEffectFull = "iiiiiiiiiiiiiiiiiiiiiii_ipiiiiiiii"; // + col 32 (+0x8c) flags (bit 0x10 CanCancel)
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
        // (an attachment's constant: the category asked for, else 0 - MbItemInfo::GetConstant)
        auto attached = [&](int slot, int category) {
            const auto a = attachments.find((slot + 1) * 100000 + equip(slot));
            Cr3BattleTables::Attachment at;
            return a != attachments.end() && t.attachmentOf(a->second, at) && at.category == category ? at.amount : 0;
        };
        if (equip(0) > 0 && t.item.find("Weapon", equip(0), kWeapon, e)) {
            f.weaponConstant = e.i(14), f.weaponHits = e.i(15), f.weaponBreak = e.i(16), f.consumption = float(e.i(17)) * 0.01f;
            f.lowerPhysical = e.i(18), f.lowerMagic = e.i(19);
            for (size_t k = 0; k < 7; ++k) f.lowerElement[k] = e.i(20 + k) + (k >= 1 ? attached(0, int(k) + 1) : 0); // (BtlElement: element e's category e + 1)
            f.weight += std::max(e.i(13), 0) + attached(0, 0);
            f.weaponBreak += attached(0, 1);
        }
        if (equip(1) > 0 && t.item.find("Armor", equip(1), kArmor, e)) {
            f.physical = e.i(15), f.magic = e.i(16);
            for (size_t k = 0; k < 7; ++k) f.elementResist[k] = e.i(17 + k) + (k >= 1 ? attached(1, int(k) + 7) : 0); // (element e's category e + 7)
            f.weight += std::max(e.i(14), 0) + attached(1, 0);
        }
        if (equip(2) > 0 && t.item.find("Accessory", equip(2), kAccessory, e)) {
            f.maxHp += e.i(12) + attached(2, 14), f.maxMp += e.i(13) + attached(2, 15), f.attack += e.i(14) + attached(2, 16);
            f.defence += e.i(15) + attached(2, 17), f.speed += e.i(16) + attached(2, 18), f.luck += e.i(17) + attached(2, 19);
        }
        // a guest's gene (CrxGene(int): GuestGene - its gene at its level, its commands; the
        // prologue hero's row 1: gene 453 at 50, Rakshasa Lotus, Augmented Electra, Augmented
        // Aquinas, Augmented Healing (All), as the reference's list) - its numbers added
        MbeFile::Row g, gi;
        if (r.i(7) > 0 && t.gene.find("GuestGene", r.i(7), kGuestGene, g)) {
            f.geneMaster = g.i(1);
            f.geneLevel = g.i(2);
            for (size_t k = 3; k + 1 < 19; k += 2) {
                if (g.i(k + 1) <= 0) continue;
                (g.i(k) == 1 ? f.skills : f.geneCommands).push_back(g.i(k + 1));
            }
            if (t.gene.find("Gene", g.i(1), kGene, gi)) {
                const int lv = f.geneLevel;
                f.element = gi.i(12); // (BtlCharacterStatus::initializeGene @ 0x009b25a4)
                f.maxHp += grown(gi.i(13), gi.i(17), lv), f.maxMp += grown(gi.i(14), gi.i(18), lv);
                f.attack += grown(low16(gi.i(15)), low16(gi.i(19)), lv), f.defence += grown(high16(gi.i(15)), high16(gi.i(19)), lv);
                f.speed += grown(low16(gi.i(16)), low16(gi.i(20)), lv), f.luck += grown(high16(gi.i(16)), high16(gi.i(20)), lv);
            }
        }
        // (anyone else's: the gene it wears - its level, slots and numbers, the items' additions)
        if (const auto pg = partyGenes.find(character); pg != partyGenes.end() && !(r.i(7) > 0 && t.gene.find("GuestGene", r.i(7), kGuestGene, g))) {
            f.geneMaster = pg->second.master;
            f.geneLevel = pg->second.level;
            for (const auto& sl : pg->second.slots) (sl[0] == 1 ? f.skills : f.geneCommands).push_back(sl[1]);
            if (t.gene.find("Gene", pg->second.master, kGene, gi)) {
                const int lv = f.geneLevel;
                f.element = gi.i(12); // (BtlCharacterStatus::initializeGene @ 0x009b25a4)
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
    bootCharacter = (b.couplingFlags & 8) != 0;
    reverseSpeed = false, reverseSpeedTurns = 0;
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
    for (int k = 0; k < r.i(15) && k < 8; ++k)
        if (r.element(16, size_t(k)) > 0) c.removes.push_back(r.element(16, size_t(k))); // (+0x50 / +0x58)
    for (int k = 0; k < r.i(21) && k < 8; ++k)
        if (r.element(22, size_t(k)) > 0) c.requiredEffects.push_back(r.element(22, size_t(k))); // (+0x80 / +0x88)
    for (int k = 0; k < r.i(23) && k < 8; ++k)
        if (r.element(24, size_t(k)) > 0) c.forbiddenEffects.push_back(r.element(24, size_t(k))); // (+0x90 / +0x98)
    c.spread = r.i(27);
    c.priority = r.i(25);
    c.speedCost = r.i(26);
    c.effectsAnnounce = int16_t(uint32_t(r.i(41)) & 0xffff);
    c.type = r.i(1);
    c.target = r.i(4);
    c.all = c.target == 2 || c.target == 4 || c.target == 5 || c.target == 9 || c.target == 11 || c.target == 13;
    c.hpKind = r.i(28);
    c.hpCost = std::max(r.i(29), 0);
    c.mpKind = r.i(30);
    c.mpCost = std::max(r.i(31), 0);
    c.element = r.i(9);
    c.extraType = r.i(38);
    for (int k = 0; k < r.i(39) && k < 16; ++k) c.extraConstants.push_back(r.element(40, size_t(k)));
    MbeFile::Row f;
    if (tables_->command.find("CommandFomula", r.i(33), kFormula, f)) c.action = f.i(1), c.formula = f.i(2), c.chain = f.i(3);
    else c.formula = 1;
    c.damage = c.formula;
    MbeFile::Row d;
    if (tables_->direction.find("CommandDirection", id, kDirection, d)) c.direction = d.i(1), c.motion = d.i(2);
    return true;
}

int Cr3BattleRules::skillConstant(const Cr3Fighter& f, int kind) const {
    if (!tables_ || kind < 0) return 0;
    int sum = 0;
    const int base = advantage(f.player) ? 17 : 12;
    MbeFile::Row r;
    for (int id : f.skills)
        if (tables_->skill.find("Skill", id, kSkill, r) && r.i(8) == 0) { // (only those without a condition)
            if (r.i(base) == kind) sum += r.i(base + 1);
            if (r.i(base + 2) == kind) sum += r.i(base + 3);
        }
    return sum;
}

std::vector<int> Cr3BattleRules::skillConstants(const Cr3Fighter& f, int kind) const {
    std::vector<int> out;
    const int base = advantage(f.player) ? 17 : 12;
    MbeFile::Row r;
    if (tables_)
        for (int id : f.skills)
            if (tables_->skill.find("Skill", id, kSkill, r) && r.i(8) == 0) {
                if (r.i(base) == kind) out.push_back(r.i(base + 1));
                if (r.i(base + 2) == kind) out.push_back(r.i(base + 3));
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

std::vector<std::pair<int, int>> Cr3BattleRules::skillEffects(int skill, bool adv) const {
    std::vector<std::pair<int, int>> out;
    MbeFile::Row r;
    if (!tables_ || !tables_->skill.find("Skill", skill, kSkill, r)) return out;
    const int base = adv ? 17 : 12; // (+0x30 .. / +0x44 ..)
    for (int e = 0; e < 2; ++e)
        if (r.i(base + 2 * e) != 0) out.push_back({r.i(base + 2 * e), r.i(base + 1 + 2 * e)});
    return out;
}

std::vector<int> Cr3BattleRules::skillCommands(int fighter) const {
    std::vector<int> out;
    if (fighter < 0 || size_t(fighter) >= fighters.size()) return out;
    const Cr3Fighter& f = fighters[size_t(fighter)];
    MbeFile::Row r;
    for (int id : f.skills)
        if (tables_ && tables_->skill.find("Skill", id, kSkill, r) && r.i(8) == 0)
            for (const auto& [kind, value] : skillEffects(id, advantage(f.player)))
                if (kind == 127) out.push_back(value);
    return out;
}

int Cr3BattleRules::mpCost(const Cr3Fighter& f, const Cr3Command& c) const {
    // (MbCommandInfo::GetMpCost + BtlStatus::GetMpCost: 0 flat, 1 % current MP, 2 % max MP,
    // 3 max(int(cost * weaponConsumption), 1), then scaled by GetMpCostPercent (kind 129) * mpCostScale)
    const float less = float(mpCostPercent(f)) * 0.01f * mpCostScale;
    switch (c.mpKind) {
    case 0: return c.mpCost > 0 ? std::max(int(float(c.mpCost) * less), 1) : 0;
    case 1: return c.mpCost > 0 ? std::max((c.mpCost * f.mp) / 100, 1) : 0;
    case 2: return c.mpCost > 0 ? std::max((c.mpCost * f.maxMp) / 100, 1) : 0;
    case 3: {
        if (c.mpCost <= 0) return 0;
        const int step = std::max(int(float(c.mpCost) * f.consumption), 1);
        return std::max(int(float(step) * less), 1);
    }
    default: return 0;
    }
}

int Cr3BattleRules::hpCost(const Cr3Fighter& f, const Cr3Command& c) const {
    // (MbCommandInfo::GetHpCost: 0 flat, 1 % current HP, 2 % max HP, 3 max(int(cost * weaponConsumption), 1))
    switch (c.hpKind) {
    case 0: return c.hpCost;
    case 1: return (c.hpCost * f.hp) / 100;
    case 2: return (c.hpCost * f.maxHp) / 100;
    case 3: return c.hpCost > 0 ? std::max(int(float(c.hpCost) * f.consumption), 1) : 0;
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
    // BtlStatusEffectList::Add + CanCancel (0x009b874c): group slot (col 1), priority (col 3),
    // turns (cols 5..6 + skill 84 continue boost), maxAccumulation (col 7), CanCancel (col 32 bit 0x10).
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size()) return;
    MbeFile::Row r;
    const bool full = tables_->skill.find("StatusEffect", id, kStatusEffectFull, r);
    if (!full && !tables_->skill.find("StatusEffect", id, kStatusEffect, r)) return;
    const int group = r.i(1), priority = r.i(3), maxAcc = std::max(r.i(7), 1), flags = full ? r.i(32) : 0;
    int turns = r.i(5);
    if (turns >= 0 && r.i(6) > turns) turns += int(random() * float(r.i(6) - turns + 1));
    if (!lasting && turns >= 0) turns += statusTurnBoost_;
    if (lasting) turns = -1; // (TakeStatusEffectToCalculation(id, -1): the field's, for good)
    Cr3Fighter& f = fighters[size_t(fi)];
    for (size_t i = 0; i < f.effects.size(); ++i) {
        const int curId = f.effects[i].first;
        if (curId == id) {
            f.effects[i].second = turns;
            f.effectStacks[id] = std::min(std::max(f.effectStacks[id], 1) + 1, maxAcc);
            return;
        }
        if (group < 0) continue;
        MbeFile::Row cr;
        if (!tables_->skill.find("StatusEffect", curId, kStatusEffect, cr) || cr.i(1) != group) continue;
        const int curPri = cr.i(3);
        if (curPri == priority && (flags & 0x10)) { // mutual cancellation (e.g. ATK Up vs ATK Down)
            f.effectStacks.erase(curId);
            f.effects.erase(f.effects.begin() + long(i));
            return;
        }
        if (priority >= curPri) {
            f.effectStacks.erase(curId);
            f.effects[i] = {id, turns};
            f.effectStacks[id] = 1;
        }
        return;
    }
    f.effects.emplace_back(id, turns);
    f.effectStacks[id] = 1;
}

void Cr3BattleRules::removeEffect(int fi, int id) {
    if (fi < 0 || size_t(fi) >= fighters.size()) return;
    fighters[size_t(fi)].effectStacks.erase(id);
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

bool Cr3BattleRules::hasSkillKind(const Cr3Fighter& f, int kind) const {
    if (!tables_ || kind < 0) return false;
    const bool adv = advantage(f.player);
    MbeFile::Row r;
    for (int id : f.skills)
        if (tables_->skill.find("Skill", id, kSkill, r) && r.i(8) == 0)
            for (const auto& [k, v] : skillEffects(id, adv))
                if (k == kind) return true;
    return false;
}

float Cr3BattleRules::effectMultiplier(const Cr3Fighter& f, int kind) const {
    float m = 1;
    if (tables_) eachEffectKind(tables_->skill, f, kind, [&](int c) { m *= float(c) / 100.0f; });
    return m;
}

float Cr3BattleRules::effectStackMultiplier(const Cr3Fighter& f, int kind) const {
    // FUN_009b164c (@ 0x009b164c): sum of (accumulation * constant) / 100.0f across matching status slots.
    float sum = 0.0f;
    if (!tables_) return sum;
    MbeFile::Row r;
    for (const auto& [id, turns] : f.effects) {
        if (!tables_->skill.find("StatusEffect", id, kStatusEffect, r)) continue;
        const auto sit = f.effectStacks.find(id);
        const int stacks = sit != f.effectStacks.end() ? std::max(sit->second, 1) : 1;
        for (size_t k = 0; k < 4; ++k)
            if (r.i(14 + k * 2) == kind) sum += float(stacks * r.i(15 + k * 2)) / 100.0f;
    }
    return sum;
}

int Cr3BattleRules::mpCostPercent(const Cr3Fighter& f) const {
    // (BtlStatusEffectUtility::GetMpCostPercent: integer accumulation starting from 100)
    int pct = 100;
    if (tables_) eachEffectKind(tables_->skill, f, 129, [&](int c) { pct = (pct * c) / 100; });
    return pct;
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

// (BtlStatus::GetAttack etc. @ 0x009b5ff8, 0x009b1574..0x009b18d8: even kinds 36/38/40/42 + stacking
// odd kinds 37/39/41/43 via FUN_009b164c, plus passive skill kind 36 for Attack)
int Cr3BattleRules::attackOf(const Cr3Fighter& f) const {
    const float statusMult = effectMultiplier(f, 36) + effectStackMultiplier(f, 37);
    float skillMult = 1.0f;
    for (int c : skillConstants(f, 36)) skillMult *= float(c) / 100.0f;
    return std::max(int(skillMult * float(f.attack) * statusMult), 1);
}
int Cr3BattleRules::defenceOf(const Cr3Fighter& f) const {
    return std::max(int(float(f.defence) * (effectMultiplier(f, 38) + effectStackMultiplier(f, 39))), 1);
}
int Cr3BattleRules::speedOf(const Cr3Fighter& f) const {
    return std::max(int(float(f.speed) * (effectMultiplier(f, 40) + effectStackMultiplier(f, 41))), 1);
}
int Cr3BattleRules::luckOf(const Cr3Fighter& f) const {
    return std::max(int(float(f.luck) * (effectMultiplier(f, 42) + effectStackMultiplier(f, 43))), 1);
}

bool Cr3BattleRules::menuEnabled(int fi, int button) const {
    if (fi < 0 || size_t(fi) >= fighters.size()) return true;
    static const int kinds[4] = {140, 47, 66, 65};
    return button < 0 || button > 3 || !hasKind(fighters[size_t(fi)], kinds[button]);
}

bool Cr3BattleRules::hits(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c) {
    // (calculateHit @ 0x007e0f3c: requiredEffects must hold; target kind 123 always evades;
    // +0xbc bit 2 or attacker kind 122 always hits; else speed table + monster +13, + attacker
    // skill kind 81 (0x009aff20), - target status kind 82 (0x009b1c08), - target skill kind 82 (0x009affd0),
    // x attacker status kind 48 multipliers)
    for (int req : c.requiredEffects) {
        const bool has = (req == 1 && t.dead()) || std::any_of(t.effects.begin(), t.effects.end(), [req](const auto& e) { return e.first == req; });
        if (!has) return false;
    }
    if (hasKind(t, 123)) return false;
    if ((c.flags & 4) || hasKind(s, 122)) return true;
    const float ratio = float(speedOf(s)) / float(speedOf(t));
    const int table = ratio < 0.5f ? 192 : ratio < 0.9f ? 213 : ratio < 1.1f ? 233 : ratio < 1.3f ? 251 : ratio < 1.5f ? 269 : 290;
    const int base = int(c.hitRate * float(table + (t.player ? 0 : 13))) - effectSum(t, 82) + skillConstant(s, 81) - skillConstant(t, 82);
    const int chance = std::max(int(effectMultiplier(s, 48) * float(base)), 0);
    return int(random() * 256.0f) < chance;
}

std::vector<std::pair<int, int>> Cr3BattleRules::endTurn() {
    std::vector<std::pair<int, int>> out;
    if (reverseSpeed && reverseSpeedTurns > 0 && --reverseSpeedTurns == 0) reverseSpeed = false;
    for (size_t k = 0; k < fighters.size(); ++k) {
        Cr3Fighter& f = fighters[k];
        // (calculateStatusEffectHpChange @ 0x007e1b9c: (skill 45 / 100 + status 45 stack rate - status 46
        // stack rate) x max HP for living units; slip never kills)
        const float rate = float(skillConstant(f, 45)) / 100.0f + effectStackMultiplier(f, 45) - effectStackMultiplier(f, 46);
        const int change = f.dead() ? 0 : int(rate * float(f.maxHp));
        if (change != 0) {
            f.hp = std::clamp(f.hp + change, change < 0 ? 1 : 0, f.maxHp);
            out.emplace_back(int(k), change);
        }
        // (BtlStatusEffect::Update: a turn off each, gone at 0; -1 stays)
        for (auto& [id, turns] : f.effects)
            if (turns > 0 && --turns == 0) f.effectStacks.erase(id);
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

void Cr3BattleRules::rollCritAndDown(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, bool& crit, bool& down, float& rate) {
    const bool adv = advantage(s.player);
    // (calculateCriticalHit: +0xbc bit 4 lets it, bit 5 or status/skill kind 139 always; else a
    // random byte under the luck ratio's 8/13/18/24/29/34/39 + monster taken-crit +6 + skill kind 19 + status kind 44)
    crit = false;
    if (c.flags & 0x10) {
        bool forceCrit = (c.flags & 0x20) != 0 || hasKind(s, 139);
        if (!forceCrit && tables_) {
            MbeFile::Row sr;
            for (int id : s.skills)
                if (tables_->skill.find("Skill", id, kSkill, sr) && sr.i(8) == 0)
                    for (const auto& [k, v] : skillEffects(id, adv)) forceCrit |= k == 139;
        }
        if (forceCrit) crit = true;
        else {
            const float luck = float(luckOf(s)) / float(luckOf(t));
            const int chance = (luck < 0.5f ? 8 : luck < 0.7f ? 13 : luck < 0.9f ? 18 : luck < 1.1f ? 24 : luck < 1.3f ? 29 : luck < 1.5f ? 34 : 39) +
                               (t.player ? 0 : 6) + skillConstant(s, 44) + effectSum(s, 44);
            crit = int(random() * 256.0f) < chance;
        }
    }
    const int e = std::clamp(c.element, 0, 6);
    rate = 1;
    if (tables_) {
        MbeFile::Row er;
        if (tables_->battle.find("Element", e, "ifffffff", er) && t.element >= 0 && t.element < 7) rate = er.f(size_t(1 + t.element));
    }
    // (calculateDown: the target downs where the element's rate is above 1, on a critical, where the
    // command always downs - bit 6 - or where it is already down)
    down = crit || t.comboChance || rate > 1.0f || (c.flags & 0x40);
}

int Cr3BattleRules::hitDamage(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, bool crit, bool down, float rate) {
    const bool adv = advantage(s.player);
    const int e = std::clamp(c.element, 0, 6);
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
    // calculateProportionalToStatusEffectNumber (0x007e1c8c): extraType 18 adds +20 to base power
    // for each target status in c.removes that also appears in c.extraConstants.
    if (c.damage == 12 && c.extraType == 18) {
        for (int rem : c.removes) {
            const bool onTarget = std::any_of(t.effects.begin(), t.effects.end(), [rem](const auto& ef) { return ef.first == rem; });
            if (onTarget && (c.extraConstants.empty() || std::find(c.extraConstants.begin(), c.extraConstants.end(), rem) != c.extraConstants.end()))
                power += 20;
        }
    }
    float d = ratio * float(power) * 0.5f;
    // ("Status": x 1.5 with the gauge's advantage, + 0.1 a down, + 0.5 a critical)
    d *= (adv ? 1.5f : 1.0f) + (down ? 0.1f : 0.0f) + (crit ? 0.5f : 0.0f);
    // ("Element": battle.mbe Element rate + GetSameElementCorrection skill kind 103 @ 0x009b0648)
    const int same103 = skillConstant(s, 103);
    const float sameCorr = same103 != 0 ? (s.element == c.element ? float(same103) * 0.01f : -float(same103) * 0.01f) : 0.0f;
    d *= std::max(rate + sameCorr, 0.0f);
    // ("Resistance": the attacker's lowered element resistance - (the target's physical or magic
    // resistance + its element resistance) + the attacker's lowered physical or magic one; the
    // element's skill kinds BtlElement's: 3 + 4e a resistance, 4 + 4e a lowered one)
    const bool magic = c.damage == 2 || c.damage == 11 || c.damage == 22 || c.damage == 23;
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
    int out = std::max(int(d), 1);
    if (bootCharacter) out *= 10; // (MbCouplingInfo +0x44 bit 3: IsBootCharacter)
    // (calculatePhysicalDamage / calculateMagicDamage: x the attacker's kind 54 / 55 multipliers)
    out = int(float(out) * effectMultiplier(s, magic ? 55 : 54));
    if (c.spread > 0) out = int(((random() - 0.5f) * float(c.spread) * 2 + 100.0f) / 100.0f * float(out));
    // (calculateMagicChainBonus / calculatePhisicalChainBonus: x (1 + combo/chain x 0.001))
    if (magic) out = int((float(t.combo) * 0.001f + 1.0f) * float(out));
    else if (c.chain == 3) out = int((float(t.chain) * 0.001f + 1.0f) * float(out));
    return std::max(out, 1);
}

int Cr3BattleRules::damage(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, bool* critOut, bool* downOut) {
    bool crit = false, down = false;
    float rate = 1;
    rollCritAndDown(s, t, c, crit, down, rate);
    if (critOut) *critOut = crit;
    if (downOut) *downOut = down;
    return hitDamage(s, t, c, crit, down, rate);
}

int Cr3BattleRules::heal(const Cr3Fighter& s, const Cr3Command& c) {
    // (BtlCalculator::calculateHpHeal: clamp(defence / attack, 0.5, 2) x power x (gene level / 20 + 1)
    // x (1 + skill kind 78 / 100), x 10 on bootCharacter, then calculateHpFluctuation spread)
    const float ratio = std::clamp(float(defenceOf(s)) / float(attackOf(s)), 0.5f, 2.0f);
    const float corr = 1.0f + float(skillConstant(s, 78)) * 0.01f;
    int out = std::max(int(ratio * float(c.power) * (float(s.geneLevel) / 20.0f + 1.0f) * corr), 1);
    if (bootCharacter) out *= 10;
    if (c.spread > 0) out = int(((random() - 0.5f) * float(c.spread) * 2.0f + 100.0f) / 100.0f * float(out));
    return std::max(out, 1);
}

int Cr3BattleRules::chocolatBonus(const Cr3Fighter& target, int baseHeal) const {
    // (BtlCalculator::calculateChocolatBonus: x (1 + target skill kind 77 / 100))
    return std::max(int(float(baseHeal) * (1.0f + float(skillConstant(target, 77)) * 0.01f)), 1);
}

bool Cr3BattleRules::targetable(int i, const Cr3Command* by) const {
    if (i < 0 || size_t(i) >= fighters.size()) return false;
    const Cr3Fighter& f = fighters[size_t(i)];
    const bool allowsDead = by && (by->revives() || std::find(by->requiredEffects.begin(), by->requiredEffects.end(), 1) != by->requiredEffects.end());
    if (f.dead()) {
        if (!allowsDead) return false;
    } else if (hasKind(f, 32)) {
        return false;
    }
    if (by) {
        for (int req : by->requiredEffects) {
            const bool present = (req == 1 && f.dead()) || hasEffect(i, req);
            if (!present) return false;
        }
        for (int forb : by->forbiddenEffects) {
            const bool present = (forb == 1 && f.dead()) || hasEffect(i, forb);
            if (present) return false;
        }
    }
    MbeFile::Row r;
    if (tables_)
        for (const auto& [id, turns] : f.effects)
            if (tables_->skill.find("StatusEffect", id, kStatusEffect, r))
                for (size_t k = 0; k < 4; ++k)
                    if (r.i(14 + k * 2) == 72 && !(by && std::find(by->removes.begin(), by->removes.end(), id) != by->removes.end())) return false;
    return true;
}

int Cr3BattleRules::livingTarget(bool player, int preferred, const Cr3Command* by) {
    if (preferred >= 0 && size_t(preferred) < fighters.size() && targetable(preferred, by) && fighters[size_t(preferred)].player == player)
        return preferred;
    std::vector<int> alive;
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player == player && targetable(int(k), by)) alive.push_back(int(k));
    if (alive.empty() && !(by && !by->requiredEffects.empty())) // (none to take: a living one all the same, unless a status is required)
        for (size_t k = 0; k < fighters.size(); ++k)
            if (fighters[k].player == player && !fighters[k].dead()) alive.push_back(int(k));
    if (alive.empty()) return -1;
    return alive[size_t(random() * float(alive.size())) % alive.size()];
}

bool Cr3BattleRules::effectLands(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, int id) {
    // BtlStatus::HasInvalidStatusEffectToCalculation (0x009b6bd4) + HasStatusEffectInvalid (0x009afc28 / 0x009b1e3c):
    // check status/skill kind 68 matching `id` and monster resist > 256 BEFORE flag bit 3 (guaranteed hit);
    // otherwise base chance (+0x30 / +0x34 with advantage) + luck bonus + skill kind 83 - resist.
    bool immune = false;
    if (tables_) eachEffectKind(tables_->skill, t, 68, [&](int blocked) { if (blocked == id) immune = true; });
    for (int blocked : skillConstants(t, 68))
        if (blocked == id) immune = true;
    if (immune) return false;
    int resist = 0;
    for (size_t k = 0; k < resistIds_.size() && k < t.effectResist.size(); ++k)
        if (resistIds_[k] == id) resist = t.effectResist[k];
    if (resist > 256) return false;
    if (c.flags & 8) return true;
    MbeFile::Row r;
    if (!tables_ || !tables_->skill.find("StatusEffect", id, kStatusEffect, r)) return false;
    const float luck = float(luckOf(s)) / float(luckOf(t));
    const int bonus = luck < 0.5f ? -52 : luck < 0.7f ? -26 : luck < 0.9f ? -13 : luck < 1.1f ? 0 : luck < 1.3f ? 13 : luck < 1.5f ? 26 : 52;
    const int chance = (advantage(s.player) ? r.i(13) : r.i(12)) + bonus + skillConstant(s, 83) - resist;
    return int(random() * 256.0f) < std::max(chance, 0);
}

void Cr3BattleRules::changeMode(int fi, int mode) {
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size() || fighters[size_t(fi)].player) return;
    Cr3Fighter& f = fighters[size_t(fi)];
    const MbeFile::Table* mc = tables_->monster.table("MonsterCommand");
    for (size_t row = 0; mc && row < mc->rows.size(); ++row) {
        MbeFile::Row m;
        if (!tables_->monster.row(*mc, row, kMonsterCommand, m) || m.i(0) != f.id || m.i(1) != mode) continue;
        f.attackCommand = m.i(5), f.element = m.i(2);
        f.commands.clear(), f.skills.clear();
        for (size_t c = 0; c < 8; ++c)
            if (m.i(8 + c * 3) >= 0) f.commands.emplace_back(m.i(8 + c * 3), std::max(m.i(9 + c * 3), 0));
        for (size_t k = 32; k < 40; ++k)
            if (m.i(k) > 0) f.skills.push_back(m.i(k));
        return;
    }
}

std::vector<Cr3Action> Cr3BattleRules::turn(const std::vector<std::array<int, 3>>& choices) {
    for (const auto& [fi, mode] : modeRequests) changeMode(fi, mode);
    modeRequests.clear();
    std::vector<Cr3Action> out;
    // BtlCommandManager::AddCommandBySelected + ExecuteAIForEnemy + BtlCommandList::Sort:
    // buckets 0 (normal, sorted), 2 (priority -8, sorted), and 5 (priority -9, unsorted)
    std::vector<std::array<int, 4>> raw; // (fighter, command, target, 1: a skill's added command)
    for (const auto& c : choices)
        if (c[0] >= 0 && size_t(c[0]) < fighters.size() && fighters[size_t(c[0])].player) raw.push_back({c[0], c[1], c[2], 0});
    const bool wait = monstersWait;
    monstersWait = false;
    for (size_t k = 0; k < fighters.size() && !wait; ++k) {
        const Cr3Fighter& f = fighters[k];
        if (f.player || f.dead()) continue;
        if (const auto sc = scripted.find(int(k)); sc != scripted.end() && !sc->second.empty()) { // (its script's, in its own's place)
            for (int id : sc->second) raw.push_back({int(k), id, -1, 0});
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
        raw.push_back({int(k), cmd, -1, 0});
    }
    scripted.clear();
    auto cmdPriorityAndCost = [&](int fi, int cmdId) -> std::pair<int, int> {
        Cr3Command info;
        if (cmdId < 0) {
            if (fi >= 0 && size_t(fi) < fighters.size() && fighters[size_t(fi)].player)
                return command(51, info) ? std::make_pair(info.priority, info.speedCost) : std::make_pair(2, 0);
            return {0, 0};
        }
        return command(cmdId, info) ? std::make_pair(info.priority, info.speedCost) : std::make_pair(0, 0);
    };
    const bool revSpeed = reverseSpeed || std::any_of(fighters.begin(), fighters.end(), [&](const Cr3Fighter& f) { return !f.dead() && hasKind(f, 11); });
    auto sortBucket = [&](std::vector<std::array<int, 4>>& bucket) {
        for (size_t i = 1; i < bucket.size(); ++i) {
            const size_t j = size_t(random() * float(i + 1)) % (i + 1);
            std::swap(bucket[i], bucket[j]);
        }
        std::stable_sort(bucket.begin(), bucket.end(), [&](const std::array<int, 4>& a, const std::array<int, 4>& b) {
            const auto [pa, ca] = cmdPriorityAndCost(a[0], a[1]);
            const auto [pb, cb] = cmdPriorityAndCost(b[0], b[1]);
            if (pa != pb) return pa > pb;
            const Cr3Fighter& fa = fighters[size_t(a[0])];
            const Cr3Fighter& fb = fighters[size_t(b[0])];
            const int sa = speedOf(fa) - fa.weight + ca;
            const int sb = speedOf(fb) - fb.weight + cb;
            return revSpeed ? sa < sb : sa > sb;
        });
    };
    std::vector<std::array<int, 4>> bucket0, bucket2, bucket5, order;
    for (const auto& e : raw) {
        const int pri = cmdPriorityAndCost(e[0], e[1]).first;
        if (pri == -8) bucket2.push_back(e);
        else if (pri == -9) bucket5.push_back(e);
        else bucket0.push_back(e);
    }
    sortBucket(bucket0);
    sortBucket(bucket2);
    size_t bucket0End = bucket0.size();
    order.insert(order.end(), bucket0.begin(), bucket0.end());
    order.insert(order.end(), bucket2.begin(), bucket2.end());
    order.insert(order.end(), bucket5.begin(), bucket5.end());
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
            for (Cr3Fighter& f : fighters) f.comboChance = false, f.combo = 0, f.chain = 0;
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
        // HaveEnoughMp & IsDisableGeneCommand / IsDisableHealCommand (0x0097b0a0): abort mid-turn if MP/status blocks it.
        const int neededMp = c.damage == 22 ? 1 : mpCost(s, c);
        if (s.mp < neededMp || (!fromSkill && (((c.flags & 1) && hasKind(s, 47)) || (c.ally() && hasKind(s, 67))))) {
            a.command = -2, a.gaugeAfter = gauge;
            out.push_back(a);
            continue;
        }
        Cr3Command own; // (hits below 0: VGetWeaponHitNum + skill kind 104 @ 0x009b04e8)
        if (c.hitsOwn) c.hits = (s.player ? std::max(s.weaponHits, 1) : command(s.attackCommand, own) ? own.hits : 1) + skillConstant(s, 104);
        if (c.type == 2) c.repeat += skillConstant(s, 105); // (GetMagicHitNumCorrection: skill kind 105 @ 0x009b0598)
        if (c.action == 14) { // (BtlCalculator::calculateActionNumBySpeed: +3/+2/+1 extra repeats by speed)
            const float spd = float(speedOf(s));
            const float roll = random();
            if (roll < spd * (1.0f / 512.0f)) c.repeat += 3;
            else if (roll < spd * (1.0f / 256.0f)) c.repeat += 2;
            else if (roll < spd * (1.0f / 128.0f)) c.repeat += 1;
        }
        c.hits = std::max(c.hits, 1) * std::max(c.repeat, 1);
        // (calculateCooperation @ 0x007dc7f4: normal bucket0 only, +0xbc bit 7, not a skill's, free of
        // kind 47 for a gene's; random byte under 154 (with advantage) or 77 + skill kind 80)
        if (at < bucket0End && (c.flags & 0x80) && !fromSkill && !((c.flags & 1) && hasKind(s, 47))) {
            const int chance = (advantage(s.player) ? 154 : 77) + skillConstant(s, 80);
            if (int(random() * 256.0f) < chance)
                for (size_t k = at + 1; k < bucket0End && k < order.size();) {
                    const std::array<int, 4> o = order[k];
                    const Cr3Fighter& p = fighters[size_t(o[0])];
                    if (p.player == s.player && !p.dead() && !hasKind(p, 31) && !((c.flags & 1) && hasKind(p, 47)) &&
                        p.mp >= (c.damage == 22 ? 1 : mpCost(p, c)) && o[3] == 0 && o[1] == cmd && (c.target != 3 || o[2] == tgt)) {
                        a.partners.push_back(o[0]);
                        order.erase(order.begin() + long(k));
                        --bucket0End;
                    } else {
                        ++k;
                    }
                }
        }
        coopNow_ = coopScale_[std::min(a.partners.size() + 1, coopScale_.size() - 1)];
        const int actualHpCost = hpCost(s, c);
        int mpReleased = 0;
        if (c.damage == 22) { // (calculateMpRelease: spends all remaining MP)
            mpReleased = s.mp;
            a.mpCost = s.mp, s.mp = 0;
        } else {
            a.mpCost = std::min(mpCost(s, c), s.mp);
            s.mp -= a.mpCost;
        }
        s.hp = std::max(s.hp - actualHpCost, 1);
        if (s.player) partyMp += a.mpCost;
        for (int pi : a.partners) {
            Cr3Fighter& p = fighters[size_t(pi)];
            const int m = c.damage == 22 ? p.mp : std::min(mpCost(p, c), p.mp);
            p.mp -= m;
            p.hp = std::max(p.hp - hpCost(p, c), 1);
            if (p.player) partyMp += m;
        }
        const bool ally = c.ally();
        bool anyCrit = false, anyDown = false;
        std::vector<int> targets;
        // (the target kind's side - BtlStatusList::collectDamageTarget*, by MbTargetType: 0 self, 1 a
        // party member, 2 all of them, 3 an enemy, 4 all enemies, 5 everyone, 6 a random enemy, 8 / 9
        // either side's one / all - a heal its own -, 10 / 11 the player's, 12 / 13 the other party
        // members' one / all)
        const bool ownSide = c.target == 2 || c.target == 13 || (c.target == 9 && (ally || c.revives()));
        const bool playersSide = c.target == 10 || c.target == 11; // (the player's: whoever uses it - battle 7's 1844 by a monster)
        const bool ownOne = c.target == 1 || c.target == 12 || (c.target == 8 && (ally || c.revives()));
        // TargetSelectMenu's change-side button supplies a fighter on the selected side for
        // flexible target kinds. Unspecified targets retain the command's default behavior.
        const bool chosenSide = tgt >= 0 && size_t(tgt) < fighters.size() && (c.target == 5 || c.target == 7 || c.target == 8 || c.target == 9);
        if (c.all)
            for (size_t k = 0; k < fighters.size(); ++k) {
                if ((chosenSide ? fighters[k].player == fighters[size_t(tgt)].player :
                     c.target == 5 || fighters[k].player == (playersSide ? true : ownSide ? s.player : !s.player)) && targetable(int(k), &c) &&
                    (c.target != 13 || int(k) != src))
                    targets.push_back(int(k));
            }
        else if (c.target == 0) targets.push_back(src);
        else if (chosenSide && targetable(tgt, &c)) targets.push_back(tgt);
        else if (ally || ownOne) { // (the one chosen on its own side, else - a monster's - one at random; 12 never itself)
            if (tgt >= 0 && size_t(tgt) < fighters.size() && fighters[size_t(tgt)].player == s.player && targetable(tgt, &c) && (c.target != 12 || tgt != src))
                targets.push_back(tgt);
            else {
                std::vector<int> side;
                for (size_t k = 0; k < fighters.size(); ++k)
                    if (fighters[k].player == s.player && targetable(int(k), &c) && (c.target != 12 || int(k) != src)) side.push_back(int(k));
                if (!side.empty()) targets.push_back(side[size_t(random() * float(side.size())) % side.size()]);
            }
        }
        else if (int t = livingTarget(playersSide ? true : !s.player, tgt, &c); t >= 0) targets.push_back(t);
        if (targets.empty()) {
            if (outcome() != 0) break;
            continue;
        }
        std::vector<int> downed; // (its targets downed: their commands wait - delayCommandByDown)
        for (int ti : targets) {
            Cr3Fighter& t = fighters[size_t(ti)];
            Cr3Hit hit;
            hit.target = ti;
            bool landed = false;
            if (c.damage < 0) { // (CommandFomula -1: no damage - its status effects alone)
                landed = true;
            } else if (ally) {
                if (c.damage == 16 || c.damage == 17) { // (calculateConstantMpHeal / calculatePercentMpHeal: colour 4)
                    const int m = c.damage == 16 ? std::max(c.power, 0) : std::max(int(float(t.maxMp) * float(c.power) / 100.0f), 0);
                    hit.hits.push_back(0), hit.mpHits.push_back(-m), hit.flags.push_back(0), hit.colors.push_back(4);
                    t.mp = std::min(t.mp + m, t.maxMp);
                } else if (c.damage == 18) { // (calculatePercentHpMpHeal: Elixir / Continue - both HP and MP)
                    const int h = std::max(int(float(t.maxHp) * float(c.power) / 100.0f), 1);
                    const int m = std::max(int(float(t.maxMp) * float(c.power) / 100.0f), 0);
                    hit.hits.push_back(-h), hit.mpHits.push_back(-m), hit.flags.push_back(0), hit.colors.push_back(3);
                    t.hp = std::min(std::max(t.hp, 0) + h, t.maxHp);
                    t.mp = std::min(t.mp + m, t.maxMp);
                } else {
                    int h = 1;
                    if (c.damage == 5) h = heal(s, c);
                    else if (c.damage == 6) {
                        h = std::max(c.power, 1);
                        if (c.chain == 19) h = chocolatBonus(t, h);
                    } else if (c.damage == 7) {
                        h = std::max(int(float(t.maxHp) * float(c.power) / 100.0f), 1);
                    } else if (c.damage == 15) { // (calculateHealOfFriendNumber)
                        int friends = 0;
                        for (const Cr3Fighter& f : fighters)
                            if (f.player == s.player && !f.dead()) ++friends;
                        h = std::max(int(float(friends) * 0.1f * float(t.maxHp)), 1);
                    }
                    hit.hits.push_back(-h), hit.mpHits.push_back(0), hit.flags.push_back(0), hit.colors.push_back(3);
                    t.hp = std::min(std::max(t.hp, 0) + h, t.maxHp);
                }
                landed = true;
            } else {
                // BtlDamageTargetList::Calculate (0x0097f81c): a cooperation fires ONE combined attack
                // scaled by coopNow_ (not N separate loops); critical and down are rolled once per target.
                lastTargets.push_back(ti);
                const Cr3Fighter& u = fighters[size_t(src)];
                bool critUnit = false, downUnit = false, critShown = false;
                float rate = 1;
                rollCritAndDown(u, t, c, critUnit, downUnit, rate);
                const uint8_t elemColor = rate < 1.0f ? 2 : rate > 1.0f ? 1 : 0;
                for (int wh = 0; wh < c.hits; ++wh) {
                    if (!hits(u, t, c)) { // (a miss: no damage, no points)
                        hit.hits.push_back(0), hit.mpHits.push_back(0), hit.flags.push_back(4), hit.colors.push_back(0);
                        continue;
                    }
                    landed = true;
                    lastContinued |= t.comboChance;
                    if (c.damage == 20) { // (calculateMpDamage: reduces target MP, colour 4)
                        int m = c.power;
                        if (c.spread > 0) m = int(((random() - 0.5f) * float(c.spread) * 2.0f + 100.0f) / 100.0f * float(m));
                        m = partyUnhurt && t.player ? 0 : std::clamp(m, 0, t.mp);
                        hit.hits.push_back(0), hit.mpHits.push_back(m), hit.flags.push_back(0), hit.colors.push_back(4);
                        t.mp = std::max(t.mp - m, 0);
                        continue;
                    }
                    int rawDmg = 0;
                    if (c.damage == 8) { // (calculatePercentDamage: % of current HP less skill kind 35 @ 0x009af9cc)
                        const int pct = std::max(c.power - skillConstant(t, 35), 0);
                        rawDmg = std::max(int(float(t.hp) * float(pct) / 100.0f), 0);
                    } else if (c.damage == 24) { // (calculateConstantHp: sets target HP down to power)
                        rawDmg = std::max(t.hp - std::max(c.power, 1), 0);
                    } else {
                        rawDmg = hitDamage(u, t, c, critUnit, downUnit, rate);
                        if (c.damage == 12 && c.extraType != 18) rawDmg *= int(t.effects.size()) + 1;
                        else if (c.damage == 13) {
                            rawDmg = std::max(int(float(rawDmg) * (float(actualHpCost) / float(std::max(u.maxHp, 1)) * 4.0f + 1.0f)), 1);
                        } else if (c.damage == 22) {
                            rawDmg = std::max(int(float(rawDmg) * (1.0f + float(mpReleased) * 0.02f)), 1);
                        } else if (c.damage == 23) {
                            const float spdMult = std::clamp(float(speedOf(u)) / float(std::max(speedOf(t), 1)), 0.5f, 4.0f);
                            rawDmg = std::max(int(float(rawDmg) * spdMult), 1);
                        }
                    }
                    const int d = partyUnhurt && t.player ? 0 : rawDmg;
                    const bool markCrit = critUnit && !critShown && c.damage != 8 && c.damage != 24;
                    if (markCrit) critShown = true;
                    hit.hits.push_back(d);
                    hit.mpHits.push_back(0);
                    hit.flags.push_back(uint8_t((markCrit ? 1 : 0) | (downUnit ? 2 : 0)));
                    hit.colors.push_back(elemColor);
                    ++t.chain; // (BtlStatusList::AddChainNum: after CalculateChainBonus on landed hit)
                    // (BtlBasicStatus::TakeDamage: no lower than the least HP its effects allow -
                    // kind 53 a number, 121 a percentage of the max: an endure; a skill's added
                    // command past it - the spider's finisher)
                    const int least = c.damage == 24 ? std::max(c.power, 1)
                                      : fromSkill    ? 0
                                                     : std::max(effectMax(t, 53), int(float(effectMax(t, 121)) * 0.01f * float(t.maxHp)));
                    const int before = t.hp;
                    t.hp = std::max(t.hp - d, t.hp > 0 ? std::min(least, t.hp) : 0);
                    if (s.player && !t.player) partyDamage = std::min(partyDamage + d, 9999999L), killed += before > 0 && t.hp <= 0;
                    if (downUnit) t.comboChance = true, ++t.combo; // (BtlBasicStatus::TakeDamage, addComboNum)
                    anyCrit |= critUnit, anyDown |= downUnit;
                    if (downUnit && std::find(downed.begin(), downed.end(), ti) == downed.end()) downed.push_back(ti);
                }
            }
            // the status effects it takes away (BtlDamage's RemovesStatusEffect), where it landed
            if (landed)
                for (int e : c.removes) removeEffect(ti, e);
            // its status effects, where it landed and the target lives (IsStatusEffectHit + skill 84 continue boost)
            if (landed && !t.dead()) {
                statusTurnBoost_ = skillConstant(s, 84);
                for (int e : c.effects)
                    if (effectLands(s, t, c, e)) giveEffect(ti, e), hit.effectsGiven.push_back(e);
                statusTurnBoost_ = 0;
            }
            hit.hpAfter = t.hp, hit.mpAfter = t.mp, hit.killed = t.dead();
            a.targets.push_back(hit);
        }
        // BtlDamageChunk::ChangeElementToCalculation (0x0097b9b0): non-item commands with element > 0
        // shift source & cooperation partners' element unless Fix Element (status/skill kind 102).
        if (c.element > 0 && c.type != 5) {
            auto shiftElem = [&](Cr3Fighter& u) {
                if (!hasKind(u, 102) && !hasSkillKind(u, 102)) u.element = c.element;
            };
            shiftElem(s);
            for (int pi : a.partners) shiftElem(fighters[size_t(pi)]);
        }
        // Break points (calculateBreakPoint @ 0x007e1400, calculateForceBreakPoint @ 0x007e15a8, skill 70 reset @ 0x0097ac10):
        if (c.extraType == 13 && !c.extraConstants.empty()) {
            const int forced = c.extraConstants[size_t(random() * float(c.extraConstants.size())) % c.extraConstants.size()];
            gauge = std::clamp(forced, -1000, 1000), gaugeStarted = gauge != 0;
        } else if (!ally && c.breakPoint >= 0) {
            int base = c.breakPoint, bonus = effectSum(s, 58);
            if (c.flags & 2) {
                base = s.player ? s.weaponBreak : command(s.attackCommand, own) ? std::max(own.breakPoint, 0) : 0;
                bonus += c.breakPoint;
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
        if (hasSkillKind(s, 70)) gauge = 0, gaugeStarted = false; // (Break Reset passive skill 70 @ 0x0097ac10)
        coopNow_ = 1;
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
        // (BtlDamageChunk::TriggerAfterCommandToCalculation - trigger 10, after the unit's own
        // command: its skills whose condition holds - 19 that command the constant, or none -, the
        // effects of its side's advantage: kind 97 interrupts with its command, 126 too (with no
        // target: AddNoTargetInterruptCommand), 100 follows - the final battle's 473: 128, then 129)
        if (tables_ && !s.dead() && !fromSkill)
            for (int id : s.skills) {
                MbeFile::Row r;
                if (!tables_->skill.find("Skill", id, kSkill, r) || r.i(7) != 10) continue;
                if (r.i(8) != 0 && !(r.i(8) == 19 && r.i(9) == cmd)) continue;
                size_t next = at + 1;
                for (const auto& [kind, value] : skillEffects(id, advantage(s.player)))
                    if (kind == 97 || kind == 126 || kind == 100) order.insert(order.begin() + long(next++), {src, value, kind == 97 ? tgt : -1, 1});
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
