#include "battle/cr3rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <tuple>

namespace cr3 {

namespace {

// the rows' layouts (assets/mbe.hpp)
constexpr const char* kCharStats = "iiiiiiiii_ip_ipiiiiiiii"; // 7 the guest gene, 8 the attack command; 13 HP, 14 MP, 15 atk | def, 16 spd | luck, 17 .. 20 their growth
constexpr const char* kMonsterStats = "iipiiiiiiiiiiiiiiiiiii";          // 4 level (+0x14), 5 element, 6 HP, 7 MP, 8 atk | def, 9 spd | luck, 10 .. 13 their growth
constexpr const char* kMonsterCommand = "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"; // id, mode, element, card, actions/turn, attack, last attack, finest arts, (command, cost, uses) x 8 (+0x20), skills x 8 (+0x80)
constexpr const char* kWeapon = "iiiiiiiiiiiiiiiiiiiiiiiiiii";   // 14 the constant (+0x38), 15 hits, 17 consumption %, 18 / 19 lowered physical / magic, 20.. lowered element 0 ..
constexpr const char* kArmor = "iiiiiiiiiiiii_piiiiiiiiiii_p";  // 15 physical (+0x44), 16 magic, 17 .. 23 element 0 .. 6
constexpr const char* kAccessory = "iiiiiiiiiiiiiiiiii";         // 12 HP (+0x30), 13 MP, 14 attack, 15 defence, 16 speed, 17 luck
constexpr const char* kGene = "iiiiiiiiiiiiiiiiiiiiiii";         // 12 element (+0x30), 13 HP (+0x34), 14 MP, 15 atk | def, 16 spd | luck, 17 .. 20 their growth
constexpr const char* kGuestGene = "iiiiiiiiiiiiiiiiiii";        // id, gene, level, then (type, id) x 8
constexpr const char* kStatusEffect = "iiiiiiiiiiiiiiiiiiiiii"; // id, group, 2 icon (+0x08: btStIcon_fr00, -1 none), priority, -, 5 / 6 turns, 7 maxAccumulation, .., 12 / 13 chance, 14 .. 21 four (kind, constant)
constexpr const char* kStatusEffectFull = "iiiiiiiiiiiiiiiiiiiiiii_ipiiiiiiii"; // + col 32 (+0x8c) flags (bit 0x10 CanCancel)
constexpr const char* kBattleBreak = "ffii_ip_ip_ip_ipiiiiiiii_ipip"; // 12 .. 19 the break bonuses (+0x50: with the advantage base, critical, down, kill; +0x60 without), 20 / 21 the status effects monsters resist (+0x70 / +0x78), 22 / 23 the cooperation scales (+0x80 / +0x88, floats)
constexpr const char* kSkill = "iiiiiiiiiiiiiiiiiiiii";          // 8 its condition (+0x20), 12 / 13 and 14 / 15 its effects (kind, constant)
constexpr const char* kCommand = "iiiiiiffiii_ip_ip_ip_ip_ip_ip_ipiiiiiiiiiiiiiii_pi"; // 5 power, 6 multiplier, 8 packed (attacks, hits, break), 27 spread (+0xa8), 32 flags, 33 formula (+0xc0)
constexpr const char* kFormula = "iiii";  // id, action, damage, chain
constexpr const char* kDirection = "iii"; // command, kind, motion

int16_t low16(int32_t v) { return int16_t(uint32_t(v) & 0xffff); }
int16_t high16(int32_t v) { return int16_t(uint32_t(v) >> 16); }
int grown(int base, int growth, int level) { return base + (level - 1) * growth / 100; }
// (BtlSkill::IsEnable 0x9c6558: a skill of no trigger and no condition - BtlStatus::Initialize's trigger 0 enables it, its
// uses all -1, its end timing 0 never called - or one its trigger enabled, until its end timing)
bool enabledRow(const Cr3Fighter& f, int id, const MbeFile::Row& r) { return (r.i(7) == 0 && r.i(8) == 0) || f.skillsOn.count(id) > 0; }

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
                f.geneOrder = gi.i(21); // (+0x54)
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
                f.geneOrder = gi.i(21); // (+0x54)
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
    noAssist = (b.couplingFlags & 0x40) != 0; // (MbCouplingInfo +0x44 bit 6: calculateAssist none)
    reverseSpeed = false, reverseSpeedTurns = 0;
    banked.clear();
    usedSkills_.clear();
    turnEndQueue_.clear(), turnEndTriggered_ = false;
    // the enemy AI (BtlOrderPoint: the points 0 to begin with; the coupling's order type, tolerance
    // and element skip; battle.mbe OrderType / ThinkPointAddition)
    enemyPoints = {}, turnNo = 0, scripted.clear();
    orderType = b.orderType, orderTolerance = b.orderTolerance, elementSkip = b.elementSkip;
    orderTypes_.clear();
    if (const MbeFile::Table* ot = t.battle.table("OrderType"))
        for (size_t row = 0; row < ot->rows.size(); ++row) {
            MbeFile::Row o;
            if (!t.battle.row(*ot, row, "iiiiiiiii", o) || o.i(0) < 0 || o.i(0) > 64) continue;
            if (size_t(o.i(0)) >= orderTypes_.size()) orderTypes_.resize(size_t(o.i(0)) + 1);
            for (size_t k = 0; k < 8; ++k) orderTypes_[size_t(o.i(0))][k] = o.i(1 + k); // (+0x04 + 4 x type)
        }
    orderChances_.clear();
    if (const MbeFile::Table* ot = t.battle.table("OrderType"))
        for (size_t row = 0; row < ot->rows.size(); ++row) {
            MbeFile::Row o;
            if (!t.battle.row(*ot, row, "iiiiiiiiiiii", o) || o.i(0) < 0 || o.i(0) > 64) continue;
            if (size_t(o.i(0)) >= orderChances_.size()) orderChances_.resize(size_t(o.i(0)) + 1);
            orderChances_[size_t(o.i(0))] = {o.i(9), o.i(10), o.i(11)};
        }
    if (MbeFile::Row tp; t.battle.find("ThinkPointAddition", 2, "iiiiiiiii", tp))
        for (size_t k = 0; k < 8; ++k) thinkEvery3_[k] = tp.i(1 + k);
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
        if (u.player) {
            f.player = true;
            if (!playerStats(t, u.character, f)) continue;
        } else if (!monsterFighter(u.monster, std::max(monsterLevel, 1), f)) {
            continue;
        }
        f.unit = int(k);
        f.hp = f.maxHp, f.mp = f.maxMp;
        if (const auto left = partyMpLeft.find(f.id); f.player && left != partyMpLeft.end()) f.mp = std::clamp(left->second, 0, f.maxMp);
        fighters.push_back(f);
    }
    if (fighters.empty()) {
        if (error) *error = "no fighters";
        return false;
    }
    // (BtlStatus::Initialize 0x9b32fc: TriggerSkillToCalculation with trigger 0 once - the skills of no trigger enabled,
    // their kind 60 status effects given and kind 61 taken away from the start: 451's 77, 454's 79, 491's 139)
    for (size_t k = 0; k < fighters.size(); ++k) (void)triggerSkills(int(k), 0);
    return true;
}

bool Cr3BattleRules::monsterFighter(int monsterId, int levelIfNone, Cr3Fighter& f) {
    MbeFile::Row r;
    if (!tables_ || !tables_->monster.find("Monster", monsterId, kMonsterStats, r)) return false;
    f = Cr3Fighter();
    // (BtlMonsterStatus::vInitialize: its own level, +0x14, where it has one - Gunner's and
    // Axer's 50, the target list's - else the reservation's, or a reinforced slot's)
    f.id = monsterId, f.level = r.i(4) > 0 ? r.i(4) : std::max(levelIfNone, 1), f.element = r.i(5);
    f.geneLevel = f.level; // (BtlMonsterStatus::VGetGeneLevel: its level)
    // (MbMonsterInfo::GetStatusEffectResistance: an s16 each at +0x48, in battle.mbe's
    // list's order - 13, 14, 15, 16, 1)
    for (size_t k = 0; k < 6; ++k)
        f.effectResist[k] = (k & 1) ? high16(r.i(17 + k / 2)) : low16(r.i(17 + k / 2));
    f.maxHp = grown(r.i(6), r.i(10), f.level), f.maxMp = std::max(grown(r.i(7), r.i(11), f.level), 0);
    f.unlimitedMp = r.i(7) < 0;
    f.attack = grown(low16(r.i(8)), low16(r.i(12)), f.level), f.defence = grown(high16(r.i(8)), high16(r.i(12)), f.level);
    f.speed = grown(low16(r.i(9)), low16(r.i(13)), f.level), f.luck = grown(high16(r.i(9)), high16(r.i(13)), f.level);
    // (MbMonsterInfo's mode 0: its MonsterCommand row - the element stays the Monster row's)
    if (const MbeFile::Table* mc = tables_->monster.table("MonsterCommand"))
        for (size_t row = 0; row < mc->rows.size(); ++row) {
            MbeFile::Row m;
            if (!tables_->monster.row(*mc, row, kMonsterCommand, m) || m.i(0) != monsterId || m.i(1) != 0) continue;
            loadMode(f, m);
            break;
        }
    f.hp = f.maxHp, f.mp = f.maxMp;
    return true;
}

bool Cr3BattleRules::command(int id, Cr3Command& c) const {
    c = Cr3Command();
    MbeFile::Row r;
    if (!tables_ || !tables_->command.find("Command", id, kCommand, r)) return false;
    c.id = id;
    c.icon = r.i(2);
    c.power = r.i(5);
    c.multiplier = r.f(6);
    const int8_t hits = int8_t((uint32_t(r.i(8)) >> 8) & 0xff); // (+0x21)
    c.hits = hits > 0 ? hits : 1;
    c.hitsOwn = hits < 0;
    c.repeat = std::max(int(int8_t(uint32_t(r.i(8)) & 0xff)), 1); // (+0x20: its attacks - Rakshasa Lotus's 3; the magic number)
    c.breakPoint = int16_t(uint32_t(r.i(8)) >> 16);                 // (+0x22)
    c.flags = uint32_t(r.i(32));                                      // (+0xbc)
    c.noAuto = (c.flags >> 16) & 1;                                   // (+0xbe bit 0)
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
    c.changeElement = r.i(10), c.elementC4 = r.i(34);
    c.selectType = r.i(35), c.selectPriority = r.i(36), c.targetOrder = r.i(37);
    for (int k = 0; k < r.i(39) && k < 16; ++k) c.extraConstants.push_back(r.element(40, size_t(k)));
    if (r.i(11) > 0) c.effectChance = r.element(12, 0); // (+0x30 / +0x38)
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
        if (tables_->skill.find("Skill", id, kSkill, r) && enabledRow(f, id, r)) { // (the enabled ones alone)
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
            if (tables_->skill.find("Skill", id, kSkill, r) && enabledRow(f, id, r)) {
                if (r.i(base) == kind) out.push_back(r.i(base + 1));
                if (r.i(base + 2) == kind) out.push_back(r.i(base + 3));
            }
    return out;
}

bool Cr3BattleRules::skillOn(const Cr3Fighter& f, int skill) const {
    MbeFile::Row r;
    return tables_ && std::find(f.skills.begin(), f.skills.end(), skill) != f.skills.end() && tables_->skill.find("Skill", skill, kSkill, r) &&
           enabledRow(f, skill, r);
}

int Cr3BattleRules::skillMost(const Cr3Fighter& f, int kind) const {
    int most = 0;
    for (int c : skillConstants(f, kind)) most = std::max(most, c);
    return most;
}

std::array<int, 3>& Cr3BattleRules::skillUsesOf(Cr3Fighter& f, int skill, const MbeFile::Row& r) {
    const auto at = f.skillUses.find(skill);
    if (at != f.skillUses.end()) return at->second;
    return f.skillUses[skill] = {r.i(4), r.i(5), r.i(6)}; // (BtlSkill::Initialize 0x9c604c)
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
        if (tables_ && tables_->skill.find("Skill", id, kSkill, r) && enabledRow(f, id, r))
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

namespace {
constexpr const char* kCharFinishers = "iiiiiiiii_ip_ip"; // 2 the save slot (+0x08), 9 / 10 the LastAttack ids (+0x28 / +0x30), 11 / 12 the FinestArts ids
} // namespace

// (the worn gene's MbGeneInfo +8 - gene_para Gene, 0 .. 2 -; no gene -1, one without its row 0)
static int finisherGeneValue(const Cr3BattleTables& t, const Cr3Fighter& f) {
    if (f.geneMaster < 0) return -1;
    MbeFile::Row g;
    return t.gene.find("Gene", f.geneMaster, "iii", g) ? g.i(2) : 0;
}

int Cr3BattleRules::lastAttackCommand(int fi) {
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size()) return -1;
    const Cr3Fighter& f = fighters[size_t(fi)];
    // (BtlStatus::GetLastAttackCommandId 0x009b722c: the transformations - effect kinds, BtlStatusEffectUtility::
    // HasTransformation 0x009b1ef0 -: 63 none, 64 - the pig form - command 18, Black Impact)
    if (hasKind(f, 63)) return -1;
    if (hasKind(f, 64)) return 18;
    if (!f.player) return f.lastAttack;
    MbeFile::Row ch;
    if (!tables_->character.find("Char", f.id, kCharFinishers, ch)) return -1;
    const auto lv = lastAttackLevel.find(ch.i(2));
    const int level = lv != lastAttackLevel.end() ? lv->second : 0, gene = finisherGeneValue(*tables_, f);
    std::vector<std::pair<int, int>> rows; // (command, weight so far)
    int total = 0;
    for (int k = 0; k < ch.i(9); ++k) {
        MbeFile::Row r; // (LastAttack: id, command, level, gene value, weight)
        if (!tables_->command.find("LastAttack", ch.element(10, size_t(k)), "iiiii", r) || r.i(2) > level || r.i(3) > gene) continue;
        total += r.i(4), rows.emplace_back(r.i(1), total);
    }
    if (rows.empty() || total <= 0) return -1;
    const int pick = std::min(int(random() * float(total)), total - 1); // (CrxRandom::GenerateUInt32 % the weights')
    for (const auto& [cmd, upTo] : rows)
        if (pick < upTo) return cmd;
    return rows.back().first;
}

int Cr3BattleRules::attackCommandOf(int fi) const {
    if (fi < 0 || size_t(fi) >= fighters.size()) return -1;
    const Cr3Fighter& f = fighters[size_t(fi)];
    return hasKind(f, 63) ? 8 : hasKind(f, 64) ? 9 : f.attackCommand;
}

int Cr3BattleRules::finestArtsCommand(int fi) {
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size()) return -1;
    const Cr3Fighter& f = fighters[size_t(fi)];
    if (hasKind(f, 63) || hasKind(f, 64)) return -1; // (BtlCharacterStatus 0x009b29b0: none under a transformation's kind)
    if (!f.player) return f.finestArts;
    MbeFile::Row ch;
    if (!tables_->character.find("Char", f.id, kCharFinishers, ch)) return -1;
    const auto lv = finestArtsLevel.find(ch.i(2));
    const int level = lv != finestArtsLevel.end() ? lv->second : 0, gene = finisherGeneValue(*tables_, f);
    std::vector<std::pair<int, int>> rows; // (command, its level)
    for (int k = 0; k < ch.i(11); ++k) {
        MbeFile::Row r; // (FinestArts: id, command, level, gene value)
        if (tables_->command.find("FinestArts", ch.element(12, size_t(k)), "iiii", r) && r.i(2) <= level && r.i(3) <= gene) rows.emplace_back(r.i(1), r.i(2));
    }
    if (rows.empty()) return -1;
    // (shuffled, sorted by level - comparator 0x00932050 -, the last: the highest level's, at random among equals)
    int top = rows.front().second;
    for (const auto& r : rows) top = std::max(top, r.second);
    std::vector<int> best;
    for (const auto& [cmd, l] : rows)
        if (l == top) best.push_back(cmd);
    return best[std::min(size_t(random() * float(best.size())), best.size() - 1)];
}

bool Cr3BattleRules::hasEffect(int fi, int id) const {
    if (fi < 0 || size_t(fi) >= fighters.size()) return false;
    for (const auto& [e, turns] : fighters[size_t(fi)].effects)
        if (e == id) return true;
    return false;
}

void Cr3BattleRules::giveEffect(int fi, int id, bool lasting, bool stackOnly) {
    // BtlStatusEffectList::Add + CanCancel (0x009b874c): group slot (col 1), priority (col 3),
    // turns (cols 5..6 + skill 84 continue boost), maxAccumulation (col 7), CanCancel (col 32 bit 0x10).
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size()) return;
    MbeFile::Row r;
    const bool full = tables_->skill.find("StatusEffect", id, kStatusEffectFull, r);
    if (!full && !tables_->skill.find("StatusEffect", id, kStatusEffect, r)) return;
    const int group = r.i(1), priority = r.i(3), maxAcc = std::max(r.i(7), 1), flags = full ? r.i(32) : 0;
    if (stackOnly && hasEffect(fi, id)) { // (AddAccumulation alone: no turns drawn, none refreshed)
        int& stacks = fighters[size_t(fi)].effectStacks[id];
        stacks = std::min(std::max(stacks, 1) + 1, maxAcc);
        return;
    }
    int turns = r.i(5);
    if (!lasting && turns >= 0 && r.i(6) > turns) turns += int(random() * float(r.i(6) - turns + 1)); // (a field's: no roll - -1 below)
    if (!lasting && turns >= 0) turns += statusTurnBoost_;
    if (lasting) turns = -1; // (TakeStatusEffectToCalculation(id, -1): the field's, for good)
    Cr3Fighter& f = fighters[size_t(fi)];
    // (BtlStatusEffect's ctor 0x9c792c, from StatusEffectList::Add 0x9b75c8: its uses StatusEffect +0x10; one
    // with a trigger - +0x20 not 0 - dormant until BtlStatusEffect::Trigger enables it, the rest on at once)
    const auto arm = [&](int as) {
        f.effectUses[as] = r.i(4);
        if (r.i(8) != 0) f.effectsDormant.insert(as);
        else f.effectsDormant.erase(as);
    };
    for (size_t i = 0; i < f.effects.size(); ++i) {
        const int curId = f.effects[i].first;
        if (curId == id) {
            f.effects[i].second = turns;
            f.effectStacks[id] = std::min(std::max(f.effectStacks[id], 1) + 1, maxAcc);
            arm(id);
            return;
        }
        if (group < 0) continue;
        MbeFile::Row cr;
        if (!tables_->skill.find("StatusEffect", curId, kStatusEffect, cr) || cr.i(1) != group) continue;
        const int curPri = cr.i(3);
        if (curPri == priority && (flags & 0x10)) { // mutual cancellation (e.g. ATK Up vs ATK Down)
            f.effectStacks.erase(curId), f.effectUses.erase(curId), f.effectsDormant.erase(curId);
            f.effects.erase(f.effects.begin() + long(i));
            return;
        }
        if (priority >= curPri) {
            f.effectStacks.erase(curId), f.effectUses.erase(curId), f.effectsDormant.erase(curId);
            f.effects[i] = {id, turns};
            f.effectStacks[id] = 1;
            arm(id);
        }
        return;
    }
    f.effects.emplace_back(id, turns);
    f.effectStacks[id] = 1;
    arm(id);
}

// a status effect's icon on the party panel (MbStatusEffectInfo +0x08 - StatusEffect col 2: btStIcon_fr00's
// frame, BtlStatusUI::AddStatusEffectIcon 0x009a36d8; -1 none)
int Cr3BattleRules::effectIcon(int id) const {
    MbeFile::Row r;
    return tables_ && tables_->skill.find("StatusEffect", id, kStatusEffect, r) ? r.i(2) : -1;
}

// what each fighter has once the action is calculated - its status effects, its element - for the
// direction to show as it lands (BtlStatus::UpdateInterface / BtlStatusEffectList::Add's icons)
void Cr3BattleRules::snapshotAfter(Cr3Action& a) const {
    a.effectsAfter.assign(fighters.size(), {}), a.elementsAfter.assign(fighters.size(), 0);
    for (size_t k = 0; k < fighters.size(); ++k) {
        for (const auto& e : fighters[k].effects) a.effectsAfter[k].push_back(e.first);
        a.elementsAfter[k] = fighters[k].element;
    }
}

void Cr3BattleRules::removeEffect(int fi, int id) {
    if (fi < 0 || size_t(fi) >= fighters.size()) return;
    fighters[size_t(fi)].effectStacks.erase(id);
    fighters[size_t(fi)].effectUses.erase(id), fighters[size_t(fi)].effectsDormant.erase(id);
    auto& v = fighters[size_t(fi)].effects;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const std::pair<int, int>& e) { return e.first == id; }), v.end());
}

// (BtlStatusEffectVisitor::Visit 0x9b9e3c: each enabled effect - IsEnable; a triggered one only once
// BtlStatusEffect::Trigger has enabled it - its four (kind, constant) of the kind)
template <typename F> static void eachEffectKind(const MbeFile& table, const Cr3Fighter& f, int kind, F&& fn) {
    MbeFile::Row r;
    for (const auto& [id, turns] : f.effects)
        if (!f.effectsDormant.count(id) && table.find("StatusEffect", id, kStatusEffect, r))
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
        if (tables_->skill.find("Skill", id, kSkill, r) && enabledRow(f, id, r))
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
        if (f.effectsDormant.count(id) || !tables_->skill.find("StatusEffect", id, kStatusEffect, r)) continue; // (enabled ones alone)
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
    // (calculateHit @ 0x007e0f3c: requiredEffects must hold; target kind 123 always evades - enabled:
    // Moonlight Shadow's for the one physical command that triggered it -; +0xbc bit 2 or attacker kind
    // 122 - Lock On's, while enabled - always hits; else speed table + monster +13, + attacker
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
    std::vector<std::pair<int, int>> out; // (the field effects' step: turn()'s end, BtlCommandManager::Calculate's)
    // (BtlCommandManager::TriggerTurnEnd 0x7dbdec - trigger 12, poison and regeneration enabled for their HP change - ran at
    // turn()'s end, its commands turnEndActions'; here only where no turn() came before - its commands dropped)
    if (!turnEndTriggered_)
        for (size_t k = 0; k < fighters.size(); ++k) (void)triggerSkills(int(k), 12), (void)triggerEffects(int(k), 12);
    turnEndTriggered_ = false, turnEndQueue_.clear();
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
        endEffects(int(k), 11);
        // (BtlStatus::UpdateStatusEffectToCalculation, the turn's end: BtlSkill::ResetCountPerTurn 0x9c6078 - each skill's
        // uses a turn its row's again)
        MbeFile::Row sr;
        for (auto& [id, uses] : f.skillUses)
            if (tables_ && tables_->skill.find("Skill", id, kSkill, sr)) uses[1] = sr.i(5);
        // (BtlStatusEffect::Update 0x9c7cb4: a turn off each, gone at 0; -1 stays)
        for (auto& [id, turns] : f.effects)
            if (turns > 0 && --turns == 0) f.effectStacks.erase(id), f.effectUses.erase(id), f.effectsDormant.erase(id);
        f.effects.erase(std::remove_if(f.effects.begin(), f.effects.end(), [](const std::pair<int, int>& e) { return e.second == 0; }),
                        f.effects.end());
    }
    return out;
}

bool Cr3BattleRules::addBreak(int points) {
    // (BtlBreakPoint::Add: the battle's first points +-500 whatever they are; from above 0 to 0 or
    // below another -500 first - a break, +0x31 -, from below to 0 or above +500; held within +-1000)
    if (points == 0) return false;
    if (!gaugeStarted) points = points > 0 ? 500 : -500;
    gaugeStarted = true;
    bool broke = false;
    if (gauge > 0 && gauge + points < 1) gauge = std::clamp(gauge - 500, -1000, 1000), broke = true;
    else if (gauge < 0 && gauge + points >= 0) gauge = std::clamp(gauge + 500, -1000, 1000), broke = true;
    // (the step added, the observers notified, then the points: BtlCommandManager::VUpdate(BtlBreakPoint const&) 0x7db17c -
    // +0x31 set - AllTriggerSkillToCalculation with trigger 6, attack kind 0, command -1 on every unit, then
    // AllEndEffectToCalculation(5); the advantage already the new side's - Emergency Guard 126 guards the side that lost
    // it, its advantage pair empty. The fallen left out: whether the status list's walk takes them unsettled)
    if (broke) {
        for (size_t k = 0; k < fighters.size(); ++k)
            if (!fighters[k].dead()) (void)triggerSkills(int(k), 6);
        for (size_t k = 0; k < fighters.size(); ++k) endEffects(int(k), 5);
    }
    gauge = std::clamp(gauge + points, -1000, 1000);
    return broke;
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
                if (tables_->skill.find("Skill", id, kSkill, sr) && enabledRow(s, id, sr)) // (Critical First Attack 2's: enabled at trigger 3)
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
    // (calculateMpDamage 0x7e2010 - damage 20 - is calculateMagicDamage's own, its result the MP's: magic too)
    const bool magic = c.damage == 2 || c.damage == 11 || c.damage == 20 || c.damage == 22 || c.damage == 23;
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
    // (calculateBasicDamage 0x7e3950's last step, a critical's - +0xa1: x BtlSkillUtility::GetCriticalDamageCorrection 0x9af3b4,
    // the attacker's enabled skills' kind 69 (0x45) constants / 100 multiplied - Critical x2 1's 200)
    if (crit)
        for (int k69 : skillConstants(s, 69)) d *= float(k69) / 100.0f;
    int out = std::max(int(d), 1);
    if (bootCharacter) out *= 10; // (MbCouplingInfo +0x44 bit 3: IsBootCharacter)
    // (calculatePhysicalDamage / calculateMagicDamage: x the attacker's kind 54 / 55 multipliers)
    out = int(float(out) * effectMultiplier(s, magic ? 55 : 54));
    if (c.spread > 0) out = int(((random() - 0.5f) * float(c.spread) * 2 + 100.0f) / 100.0f * float(out));
    // (calculateMagicChainBonus / calculatePhisicalChainBonus: x (1 + combo/chain x 0.001))
    if (magic && c.damage != 20) out = int((float(t.combo) * 0.001f + 1.0f) * float(out)); // (formula 19's chain -1: none)
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
        // (BtlStatusEffectList::HasProhibitedTarget 0x009b88cc: one of the required is enough -
        // battle 7's release 1844 asks for 32 or 43)
        bool required = by->requiredEffects.empty();
        for (int req : by->requiredEffects) required |= (req == 1 && f.dead()) || hasEffect(i, req);
        if (!required) return false;
        for (int forb : by->forbiddenEffects) {
            const bool present = (forb == 1 && f.dead()) || hasEffect(i, forb);
            if (present) return false;
        }
    }
    MbeFile::Row r;
    if (tables_)
        for (const auto& [id, turns] : f.effects)
            if (!f.effectsDormant.count(id) && tables_->skill.find("StatusEffect", id, kStatusEffect, r))
                for (size_t k = 0; k < 4; ++k)
                    if (r.i(14 + k * 2) == 72 && !(by && (std::find(by->removes.begin(), by->removes.end(), id) != by->removes.end() ||
                                                          // (MbCommandInfo::DisableNonTarget 0x009124d0: one it requires -
                                                          // battle 7's 1755, the squeeze, on the one held - effect 32)
                                                          std::find(by->requiredEffects.begin(), by->requiredEffects.end(), id) != by->requiredEffects.end())))
                        return false;
    return true;
}

int Cr3BattleRules::livingTarget(bool player, int preferred, const Cr3Command* by) {
    if (preferred >= 0 && size_t(preferred) < fighters.size() && targetable(preferred, by) && fighters[size_t(preferred)].player == player)
        return preferred;
    std::vector<int> alive;
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player == player && targetable(int(k), by)) alive.push_back(int(k));
    // (none to take: a living one all the same, unless a status is required - never one defeated, the escaped)
    if (alive.empty() && !(by && !by->requiredEffects.empty()))
        for (size_t k = 0; k < fighters.size(); ++k)
            if (fighters[k].player == player && !defeated(fighters[k])) alive.push_back(int(k));
    if (alive.empty()) return -1;
    return alive[size_t(random() * float(alive.size())) % alive.size()];
}

bool Cr3BattleRules::effectLands(const Cr3Fighter& s, const Cr3Fighter& t, const Cr3Command& c, int id) {
    // BtlStatus::HasInvalidStatusEffectToCalculation (0x009b6bd4) + HasStatusEffectInvalid (0x009afc28 / 0x009b1e3c):
    // check status/skill kind 68 matching `id` and monster resist > 256 BEFORE flag bit 3 (guaranteed hit);
    // otherwise base chance (+0x30 / +0x34 with advantage) + luck bonus + skill kind 83 - resist.
    if (effectInvalid(t, id)) return false;
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
        // (VChangeMode 0x009c6898: +0x1fc, the commands - their uses full again - and skills, the
        // element - +0x08 - where not -1: BtlBasicStatus::ChangeElement 0x009ba600 keeps the one it has;
        // the action count from the next turn's ResetRatio)
        loadMode(f, m);
        f.mode = mode;
        if (m.i(2) != -1) f.element = m.i(2);
        return;
    }
}

void Cr3BattleRules::loadMode(Cr3Fighter& f, const MbeFile::Row& m) {
    f.card = m.i(3);           // (+0x0c)
    f.actionsPerTurn = m.i(4); // (+0x10: MbMonsterInfo::GetRatio 0x00913b5c)
    f.attackCommand = m.i(5);
    f.lastAttack = m.i(6), f.finestArts = m.i(7); // (+0x18 / +0x1c: the finishers)
    f.commands.clear(), f.skills.clear();
    f.skillsOn.clear(), f.skillUses.clear(); // (initializeCommandSkill: a new BtlSkillList - none enabled, the uses the rows')
    for (size_t c = 0; c < 8; ++c)
        if (m.i(8 + c * 3) >= 0) f.commands.push_back({m.i(8 + c * 3), m.i(9 + c * 3), m.i(10 + c * 3)});
    for (size_t k = 32; k < 40; ++k)
        if (m.i(k) > 0) f.skills.push_back(m.i(k)); // (its skills: Gunner's and Axer's 304 - element 4 resistance -50)
}

void Cr3BattleRules::addEnemyOrderPoint(int type, int points) {
    if (type >= 0 && type < 8) enemyPoints[size_t(type)] = std::clamp(enemyPoints[size_t(type)] + points, 0, 200);
}

bool Cr3BattleRules::isSelected(int fi, int ti, const Cr3Command& c, int type) {
    const Cr3Fighter& s = fighters[size_t(fi)];
    const Cr3Fighter& t = fighters[size_t(ti)];
    // (MP short - the command's cost with the status effects' percentage, where the MP is not below
    // 0 - does not rule the command out, it is only not preferred; nor is a target already of the
    // element it changes to, or a gene command under kind 47)
    if ((!s.unlimitedMp && s.mp >= 0 && s.mp < mpCost(s, c)) || t.element == c.changeElement || ((c.flags & 1) && hasKind(s, 47))) return false;
    const auto has = [&](int e) { return (e == 1 && t.dead()) || hasEffect(ti, e); };
    switch (type) {
    case 1:
    case 2: {
        // (unless a random number falls under Coupling +0x48: a target that resists the element is
        // not preferred - MbGetElementInfo(the target's element).NotStrong(the command's), the table's
        // row the target's element, as the original reads it; an element-less command with +0xc4 1
        // the actor's own under skill kind 75 with the gauge's advantage -, magic not one under kind 49)
        if (random() < elementSkip) return true;
        if (type == 2 && hasKind(t, 49)) return false;
        int e = c.element;
        if (e == 0 && c.elementC4 == 1 && advantage(s.player) && hasSkillKind(s, 75)) e = s.element;
        MbeFile::Row er; // (MbElementInfo::NotStrong 0x009129c0: the rate 1 or more; no row - true)
        if (!tables_ || !tables_->battle.find("Element", t.element, "ifffffff", er)) return true;
        return e >= 0 && e < 7 && er.f(size_t(1 + e)) >= 1.0f;
    }
    case 3: return float(t.hp) <= float(t.maxHp) * 0.9f; // (a heal: BtlBasicStatus::IsLowHp 0x009ba720 - 90 % or less)
    case 4: // (a cure: the target has one it removes - the original's loop counts the effects, +0x40, reading the removes, +0x58)
        for (size_t k = 0; k < c.effects.size() && k < c.removes.size(); ++k)
            if (has(c.removes[k])) return true;
        return false;
    case 5:
    case 6: // (a buff / debuff: the target lacks one of its effects; none - never)
        for (int e : c.effects)
            if (!has(e)) return true;
        return false;
    default: return true;
    }
}

std::vector<int> Cr3BattleRules::collectTargets(int fi, const Cr3Command& c) const {
    // (BtlTargetList::Collect 0x009ca648 by the target kind: 0 itself, 1 / 2 its side, 3 / 4 / 6
    // the other side - collectEnemy 0x009c9efc: provoked - kind 52 -, its provoker alone where it can
    // be targeted -, 5 / 7 everyone, 8 / 9 either - its own for a heal or a revive -, 10 / 11 the
    // player's, 12 / 13 its side's others; IsNonTarget / HasProhibitedTarget left out - targetable)
    std::vector<int> list;
    if (fi < 0 || size_t(fi) >= fighters.size()) return list;
    const Cr3Fighter& s = fighters[size_t(fi)];
    const bool ownSide = c.target == 1 || c.target == 2 || c.target == 12 || c.target == 13 ||
                         ((c.target == 8 || c.target == 9) && (c.ally() || c.revives()));
    if (c.target == 0) list.push_back(fi);
    else if (!ownSide && (c.target == 3 || c.target == 4 || c.target == 6 || c.target == 8 || c.target == 9) && hasKind(s, 52) &&
             s.forcedBy >= 0 && targetable(s.forcedBy, &c) && fighters[size_t(s.forcedBy)].player != s.player)
        list.push_back(s.forcedBy);
    else
        for (size_t k = 0; k < fighters.size(); ++k) {
            const bool side = c.target == 5 || c.target == 7 ? true
                            : c.target == 10 || c.target == 11 ? fighters[k].player
                            : ownSide ? fighters[k].player == s.player : fighters[k].player != s.player;
            if (side && targetable(int(k), &c) && !((c.target == 7 || c.target == 12 || c.target == 13) && int(k) == fi)) list.push_back(int(k));
        }
    return list;
}

int Cr3BattleRules::chooseTarget(int fi, int cmd, int type, bool* preferred) {
    if (preferred) *preferred = false;
    Cr3Command c;
    if (fi < 0 || size_t(fi) >= fighters.size() || !command(cmd, c)) return -1;
    const Cr3Fighter& s = fighters[size_t(fi)];
    // (BtlStatusEffectUtility::CanUseSelectType 0x009b2190: kind 138 - no heals)
    if ((type == 3 || type == 9) && hasKind(s, 138)) return -1;
    std::vector<int> list = collectTargets(fi, c);
    // (the order, +0xd0: shuffled, then sorted - taken from the back: 1 the highest HP first, 2 the
    // lowest, 3 / 4 the HP rate's, 5 / 6 the level's; 7 as collected)
    if (c.targetOrder != 7)
        for (size_t i = 1; i < list.size(); ++i) std::swap(list[i], list[size_t(random() * float(i + 1)) % (i + 1)]);
    const auto key = [&](int k) -> float {
        const Cr3Fighter& f = fighters[size_t(k)];
        switch (c.targetOrder) {
        case 1: case 2: return float(f.hp);
        case 3: case 4: return f.maxHp > 0 ? float(f.hp) / float(f.maxHp) : 0.0f;
        case 5: case 6: return float(f.level);
        default: return 0.0f;
        }
    };
    if (c.targetOrder >= 1 && c.targetOrder <= 6) {
        const bool higher = c.targetOrder % 2 == 1;
        std::stable_sort(list.begin(), list.end(), [&](int a, int b) { return higher ? key(a) < key(b) : key(a) > key(b); });
    }
    for (size_t k = list.size(); k-- > 0;)
        if (isSelected(fi, list[k], c, type)) {
            if (preferred) *preferred = true;
            return list[k];
        }
    return list.empty() ? -1 : list.front();
}

void Cr3BattleRules::addCommandByAI(int fi, std::vector<std::array<int, 4>>& raw, std::vector<std::array<int, 3>>& queued) {
    Cr3Fighter& f = fighters[size_t(fi)];
    struct Pick {
        int target = -1, command = -1, type = -1, cost = 1, entry = 0;
        bool notFound = true;
    } pick;
    // (BtlSelectTypeList::Sort 0x0096e884: the 8 types by their points, ascending; those within the
    // coupling's tolerance of the most taken as the most, ties in random order; tried from the most)
    std::vector<std::pair<int, int>> types; // (type, points)
    for (int k = 0; k < 8; ++k) types.emplace_back(k, enemyPoints[size_t(k)]);
    std::stable_sort(types.begin(), types.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    const int most = types.back().second;
    for (auto& [type, pts] : types)
        if (most - pts <= orderTolerance) pts = most;
    for (size_t i = 1; i < types.size(); ++i) std::swap(types[i], types[size_t(random() * float(i + 1)) % (i + 1)]);
    std::stable_sort(types.begin(), types.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    for (; !types.empty(); types.pop_back()) {
        const int type = types.back().first;
        // (BtlStatusList::Select 0x009bf514: its entries of the type it can pay for and has uses of -
        // BtlPriorityCommandIdList::Collect -, each scored random x its +0xcc / their sum and tried from
        // the highest - sortBySelectPriority 0x009c8244 -; the first with a target the fallback, the
        // first with a preferred one not already queued - BtlCommandList::Has 0x007e5cc8 - the pick)
        struct Kept {
            int entry, command;
            float score;
        };
        std::vector<Kept> kept;
        int total = 0;
        for (size_t i = 0; i < f.commands.size(); ++i) {
            const Cr3MonsterCommand& e = f.commands[i];
            Cr3Command c;
            if (e.cost <= f.actionsLeft && (e.uses == -1 || e.uses > 0) && command(e.command, c) && c.selectType == type)
                kept.push_back({int(i), e.command, float(c.selectPriority)}), total += c.selectPriority;
        }
        for (Kept& k : kept) k.score = total > 0 ? random() * (k.score / float(total)) : 0.0f; // (CrxRandom::GenerateReal)
        for (size_t i = 1; i < kept.size(); ++i) std::swap(kept[i], kept[size_t(random() * float(i + 1)) % (i + 1)]);
        std::stable_sort(kept.begin(), kept.end(), [](const Kept& a, const Kept& b) { return a.score > b.score; });
        bool first = true;
        for (const Kept& k : kept) {
            bool preferred = false;
            const int t = chooseTarget(fi, k.command, type, &preferred);
            if (t < 0) continue;
            const Pick record{t, k.command, type, f.commands[size_t(k.entry)].cost, k.entry, true};
            if (first) pick = record;
            const bool repeat = std::any_of(queued.begin(), queued.end(), [&](const std::array<int, 3>& q) { return q[0] == fi && q[1] == t && q[2] == k.command; });
            if (repeat) pick.notFound = true;
            else if (preferred) {
                pick = record, pick.notFound = false;
                break;
            }
            first = false;
        }
        if (!pick.notFound) break;
    }
    // (BtlOrderPoint::Consume 0x0096eee4: the type used - clamped to 50 .. 250 - loses 50)
    if (pick.type >= 0) enemyPoints[size_t(pick.type)] = std::clamp(enemyPoints[size_t(pick.type)], 50, 250) - 50;
    // (nothing valid: command -1 - what the command manager makes of it is unknown; nothing queued)
    if (pick.command >= 0) {
        Cr3Command c;
        // (an everyone command's target only picked its preference - the side it hits is every side)
        raw.push_back({fi, pick.command, command(pick.command, c) && c.target == 5 ? -1 : pick.target, 0});
        queued.push_back({fi, pick.target, pick.command});
    }
    f.actionsLeft = std::max(f.actionsLeft - std::max(pick.cost, 1), 0); // (a cost below 1 - none in the tables - taken as 1)
    // (VDecreaseCount: the entry's uses, where limited - entry 0's with nothing picked, as the original)
    if (size_t(pick.entry) < f.commands.size() && f.commands[size_t(pick.entry)].uses > 0) --f.commands[size_t(pick.entry)].uses;
}

// (a command's bucket - +0xa0 - and its speed weight - +0xa4; a guard - -1 - a player's command 51's)
std::pair<int, int> Cr3BattleRules::priorityAndCost(int fi, int cmdId) const {
    Cr3Command info;
    if (cmdId < 0) {
        if (fi >= 0 && size_t(fi) < fighters.size() && fighters[size_t(fi)].player)
            return command(51, info) ? std::make_pair(info.priority, info.speedCost) : std::make_pair(2, 0);
        return {0, 0};
    }
    return command(cmdId, info) ? std::make_pair(info.priority, info.speedCost) : std::make_pair(0, 0);
}

// (BtlCommandList::Sort: shuffled, then by the bucket, then by speed - less the weight, plus the command's cost)
void Cr3BattleRules::sortBucket(std::vector<std::array<int, 4>>& bucket) {
    for (size_t i = 1; i < bucket.size(); ++i) {
        const size_t j = size_t(random() * float(i + 1)) % (i + 1);
        std::swap(bucket[i], bucket[j]);
    }
    std::stable_sort(bucket.begin(), bucket.end(), [&](const std::array<int, 4>& a, const std::array<int, 4>& b) {
        const auto [pa, ca] = priorityAndCost(a[0], a[1]);
        const auto [pb, cb] = priorityAndCost(b[0], b[1]);
        if (pa != pb) return pa > pb;
        const Cr3Fighter& fa = fighters[size_t(a[0])];
        const Cr3Fighter& fb = fighters[size_t(b[0])];
        const int sa = speedOf(fa) - fa.weight + ca;
        const int sb = speedOf(fb) - fb.weight + cb;
        return reverseSpeed ? sa < sb : sa > sb; // (bit 3: FUN_007e7120's slowest first, else FUN_007e84c0's fastest)
    });
}

std::vector<Cr3Action> Cr3BattleRules::turn(const std::vector<std::array<int, 3>>& choices) {
    // BtlSysStateSelect::phaseStart 0x009d35d4: each unit's actions this turn (ResetRatio - a
    // monster's mode's +0x10, a character's 1; the party's first strike none for the monsters -
    // ClearRatioToEnemy), the turn's number, the think points (StepOrderPoint: OrderType's row of
    // the coupling's order type, every third turn ThinkPointAddition's row 2 - +50 to type 7)
    const bool wait = monstersWait;
    monstersWait = false;
    for (Cr3Fighter& f : fighters) f.actionsLeft = f.player ? 1 : wait ? 0 : std::max(f.actionsPerTurn, 0);
    ++turnNo;
    if (orderType >= 0 && size_t(orderType) < orderTypes_.size())
        for (size_t k = 0; k < 8; ++k) addEnemyOrderPoint(int(k), orderTypes_[size_t(orderType)][k]);
    if (turnNo % 3 == 0)
        for (size_t k = 0; k < 8; ++k) addEnemyOrderPoint(int(k), thinkEvery3_[k]);
    // (phaseCalculate 0x009d490c: the queued mode changes - this turn's picks the new mode's)
    for (const auto& [fi, mode] : modeRequests) changeMode(fi, mode);
    modeRequests.clear();
    giveFieldEffects(); // (phaseStart: GiveFieldStatusEffect, every turn)
    turnEndQueue_.clear(); // (the last turn end's list: played or dropped)
    // BtlCommandManager::AddCommandBySelected + ExecuteAIForEnemy + BtlCommandList::Sort:
    // buckets 0 (normal, sorted), 2 (priority -8, sorted), and 5 (priority -9, unsorted)
    std::vector<std::array<int, 4>> raw; // (fighter, command, target, 1: a skill's added command)
    std::vector<std::array<int, 3>> queued; // (source, target, command: BtlCommandList::Has's)
    for (const auto& c : choices)
        if (c[0] >= 0 && size_t(c[0]) < fighters.size() && fighters[size_t(c[0])].player)
            raw.push_back({c[0], c[1], c[2], 0}), queued.push_back({c[0], c[2], c[1]});
    if (!wait) {
        // (the scripts' BattleCommand.Add: its actions off the turn's, its target getCommandTarget's
        // with no select type)
        for (const auto& [k, list] : scripted) {
            if (k < 0 || size_t(k) >= fighters.size() || fighters[size_t(k)].player || fighters[size_t(k)].dead()) continue;
            for (const auto& [n, id] : list) {
                Cr3Fighter& f = fighters[size_t(k)];
                f.actionsLeft = std::max(f.actionsLeft - n, 0);
                Cr3Command c;
                const int t = chooseTarget(k, id, -1);
                raw.push_back({k, id, command(id, c) && c.target == 5 ? -1 : t, 0});
                queued.push_back({k, t, id});
            }
        }
        // (BtlStatusList::CollectAIForEnemy 0x009c0bcc - BtlTargetList::CollectNonPlayer: the
        // monsters by speed, the fastest first, ties at random - then ExecuteAIForEnemy 0x009c0de8 each:
        // none under kind 31 - IsNoAction - or with no actions left; else a pick while actions remain)
        std::vector<int> ai;
        for (size_t k = 0; k < fighters.size(); ++k)
            if (!fighters[k].player && !fighters[k].dead()) ai.push_back(int(k));
        for (size_t i = 1; i < ai.size(); ++i) std::swap(ai[i], ai[size_t(random() * float(i + 1)) % (i + 1)]);
        std::stable_sort(ai.begin(), ai.end(), [&](int a, int b) { return speedOf(fighters[size_t(a)]) > speedOf(fighters[size_t(b)]); });
        for (int k : ai) {
            if (hasKind(fighters[size_t(k)], 31)) continue;
            while (fighters[size_t(k)].actionsLeft > 0) addCommandByAI(k, raw, queued);
        }
    }
    scripted.clear();
    std::vector<std::array<int, 4>> bucket0, bucket2, bucket5, order;
    for (const auto& e : raw) {
        const int pri = priorityAndCost(e[0], e[1]).first;
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
    std::vector<Cr3Action> out = resolveOrder(std::move(order), bucket0End);
    // (BtlCommandManager::Calculate 0x7db674: the turn's commands worked out, the field effects stepped -
    // BtlFieldEffectList::Step, a turn off each, gone at 0 -; Reverse Gravity's over, the lists' bit 3 cleared and a
    // chunk with no source for command 58 - BtlDamageParam {-1, -1, -1, 0x3a}: its BtlGravityInversionDirection)
    if (reverseSpeedTurns > 0 && --reverseSpeedTurns == 0) {
        reverseSpeed = false;
        if (outcome() == 0) {
            Cr3Action back;
            back.source = -1, back.command = 58, back.gaugeAfter = gauge;
            snapshotAfter(back), out.push_back(back);
        }
    }
    // (then the assist - +0x182, set at every phaseStart - appended: calculateAssist, after them)
    if (Cr3Action assist; assistAfterTurn(assist)) out.push_back(assist);
    // (phaseCalculate 0x9d490c, the commands worked out: BtlCommandManager::TriggerTurnEnd 0x7dbdec - trigger 12 on every
    // unit's skills and status effects, the commands their kinds 100 / 101 add into the turn-end list - +0x48 -, made unique;
    // played after the turn end's script - turnEndActions -, the poison and the regeneration then as endTurn's numbers)
    for (size_t k = 0; k < fighters.size(); ++k) {
        queueTurnEnd(int(k), triggerSkills(int(k), 12));
        queueTurnEnd(int(k), triggerEffects(int(k), 12));
    }
    turnEndTriggered_ = true;
    return out;
}

// (AddEffectCommand into the turn-end list: kind 100 {the bearer, the command}, 101 {no source, the command, the bearer};
// BtlCommandList::Unique - its key unsettled, here the whole entry: two bearers' revives both kept)
void Cr3BattleRules::queueTurnEnd(int bearer, const std::vector<std::pair<int, int>>& added) {
    for (const auto& [kind, id] : added) {
        std::array<int, 3> e{};
        if (kind == 100) e = {bearer, id, -1};
        else if (kind == 101) e = {-1, id, bearer};
        else continue;
        if (std::find(turnEndQueue_.begin(), turnEndQueue_.end(), e) == turnEndQueue_.end()) turnEndQueue_.push_back(e);
    }
}

// phaseStatusEffect 0x9d0098 - the turn-end list not empty: CalculateTurnEnd 0x7dbeac (DeleteInvalid, calculateCommands,
// Clear). A kind 100 one its bearer's own - no trigger 3, no command change -, a kind 101 one with no source; command 55 -
// the poison's and the regeneration's HP change - left to endTurn's numbers
std::vector<Cr3Action> Cr3BattleRules::turnEndActions() {
    std::vector<std::array<int, 4>> order;
    for (const auto& [src, id, tgt] : turnEndQueue_)
        if (id != 55) order.push_back({src, id, tgt, 4});
    turnEndQueue_.clear();
    std::vector<Cr3Action> out = order.empty() ? std::vector<Cr3Action>{} : resolveOrder(std::move(order), 0);
    for (Cr3Action& a : out) markAssist(a);
    return out;
}

// BtlSysStateAction::VUpdate 0x9d261c case 0x12 - the turn end's script queued commands (BattleCommand.SetId /
// SetIdAndTarget): AllEndEffectToCalculation(11), then each {no source, its target or -1, the command} worked out
// (AddCommandBySelected, Calculate) and played with its own direction
std::vector<Cr3Action> Cr3BattleRules::scriptCommands(const std::vector<std::pair<int, int>>& cmds) {
    if (cmds.empty()) return {};
    for (size_t k = 0; k < fighters.size(); ++k) endEffects(int(k), 11);
    std::vector<std::array<int, 4>> order;
    for (const auto& [id, target] : cmds) order.push_back({-1, id, target, 4});
    std::vector<Cr3Action> out = resolveOrder(std::move(order), 0);
    for (Cr3Action& a : out) markAssist(a);
    return out;
}

// (CreateDirection 0x990d98: CommandDirection kind 13 - BtlAssistDirection - or 40 - BtlAssistWithScriptDirection)
void Cr3BattleRules::markAssist(Cr3Action& a) const {
    if (Cr3Command c; a.source < 0 && command(a.command, c) && (c.direction == 13 || c.direction == 40)) a.assist = true;
}

// the commands in order (BtlCommandManager::calculateCommands 0x7db93c): each worked out and applied - its finishers,
// cooperations, triggers and the commands they add on the way; a turn's (bucket0End: where list 0 - the cooperations' -
// ends), a turn end's (Cr3BattleRules::scriptCommands / turnEndActions: none of it list 0)
std::vector<Cr3Action> Cr3BattleRules::resolveOrder(std::vector<std::array<int, 4>> order, size_t bucket0End) {
    std::vector<Cr3Action> out;
    int lastSide = -1; // (BtlCommandManager::Clear: none yet this turn)
    bool lastHadChance = false, lastContinued = false;
    std::vector<int> lastTargets;
    // The finishers (BtlCommandManager::calculateCommands 0x007db93c: addLastAttack 0x007dbfc8 before
    // each next command). The last action not itself inserted - this +0x188 - and the combo's
    // participants - +0xb8: the actor added after a comboing action, alone after any other
    // (calculateCommand 0x007dc424)
    struct FinisherChunk {
        bool set = false;
        int attacker = -1, firstTarget = -1, command = -1;
        bool combo = false;
        std::vector<int> hit;
    } chunk;
    std::vector<int> comboActors;
    // (BtlStatusList::AddEffectCommand: the commands a status effect's enabled kinds add - 97 an interrupt,
    // next - AddInterruptCommand -, 98 the same with no actor, 99 a counter - AddCounterCommand 0x9b0f64: only
    // against an attacker of the other side, aimed at it -, 100 / 101 after the turn's - AddCommand /
    // AddNoSourceCommand -; marked 4: a status effect's)
    size_t nextAt = 0;
    const auto insertAt = [&](size_t pos, const std::array<int, 4>& e) {
        order.insert(order.begin() + long(std::min(pos, order.size())), e);
        if (pos < bucket0End) ++bucket0End;
    };
    const auto queueEffectCommands = [&](int bearer, const std::vector<std::pair<int, int>>& added, int attacker) {
        for (const auto& [kind, id] : added) {
            if (order.size() > 256) break; // (a guard: a turn's commands bounded)
            if (kind == 97) insertAt(nextAt++, {bearer, id, -1, 4});
            else if (kind == 98) insertAt(nextAt++, {-1, id, bearer, 4});
            else if (kind == 99 && attacker >= 0 && fighters[size_t(attacker)].player != fighters[size_t(bearer)].player) insertAt(nextAt++, {bearer, id, attacker, 4});
            else if (kind == 100) order.push_back({bearer, id, -1, 4});
            else if (kind == 101) order.push_back({-1, id, bearer, 4});
        }
    };
    // (after a command: TriggerAfterCommandToCalculation 0x97dc38 - trigger 11 on every unit - and playCommand
    // 0x7dc940's EndSourceEffectToCalculation(9) - the source's: a Rally used up, Afterthought Guard 127 ended straight after
    // its trigger 10 - and end timing 10 on every unit: Regenerate's revive, a used Mental Wall / Moonlight Shadow gone)
    const auto afterAnyCommand = [&](int attackKind, int source) {
        for (size_t k = 0; k < fighters.size(); ++k) (void)triggerSkills(int(k), 11, attackKind);
        for (size_t k = 0; k < fighters.size(); ++k) queueEffectCommands(int(k), triggerEffects(int(k), 11, attackKind), -1);
        if (source >= 0) endEffects(source, 9);
        for (size_t k = 0; k < fighters.size(); ++k) endEffects(int(k), 10);
    };
    // (a no-actor command next - Regenerate's - is still played once the party has fallen)
    const auto decided = [&](size_t at) { return outcome() != 0 && !(outcome() == 2 && at + 1 < order.size() && order[at + 1][0] < 0); };
    size_t recorded = 0;
    int resolvingFrom = 0; // (the order entry in course: its inserted mark - BtlCommand +0x1a)
    // (BtlStatusList::CollectDamageTarget: those the next command would hit - its chosen one, or
    // its side's where it hits them all or picks one at random)
    auto wouldHit = [&](int src, int cmd, int tgt) {
        std::vector<int> out;
        if (cmd < 0) return std::vector<int>{src};
        Cr3Command c;
        if (!command(cmd, c)) c.target = 3;
        const Cr3Fighter& s = fighters[size_t(src)];
        const bool own = c.ally() || c.target == 1 || c.target == 2 || c.target == 12 || c.target == 13;
        if (c.target == 0) return std::vector<int>{src};
        if (c.target == 6) { // (collectDamageTargetRandomEnemy 0x009bd320: one living opponent at random - no target filter)
            for (size_t k = 0; k < fighters.size(); ++k)
                if (!fighters[k].dead() && fighters[k].player != s.player) out.push_back(int(k));
            if (out.size() > 1) out = {out[std::min(size_t(random() * float(out.size())), out.size() - 1)]};
            return out;
        }
        if (!c.all && tgt >= 0 && size_t(tgt) < fighters.size() && !fighters[size_t(tgt)].dead()) return std::vector<int>{tgt};
        for (size_t k = 0; k < fighters.size(); ++k)
            if (!fighters[k].dead() && (c.target == 5 || fighters[k].player == (c.target == 10 || c.target == 11 ? true : own ? s.player : !s.player)))
                out.push_back(int(k));
        return out;
    };
    for (size_t at = 0; at < order.size(); ++at) {
        if (out.size() > recorded) { // (the action just resolved: the chunk and the combo's actors)
            const Cr3Action& done = out.back();
            if (done.command != -2 && done.source >= 0) {
                if (!done.combo) comboActors.clear();
                comboActors.push_back(done.source);
                if (resolvingFrom == 0) {
                    chunk.set = true, chunk.attacker = done.source, chunk.firstTarget = done.target;
                    chunk.command = done.command < 0 ? 51 : done.command, chunk.combo = done.combo, chunk.hit.clear();
                    for (const Cr3Hit& h : done.targets) chunk.hit.push_back(h.target);
                }
            }
            recorded = out.size();
        }
        // (addLastAttack: two linked actions at least; the last one's command able to lead into one -
        // MbCommandInfo +0xbd bit 1 -, its combo; the next not inserted and breaking the chain -
        // BtlDamageChunk::HaveChanceReset 0x0097e56c: the other side, its actor under status effect
        // kind 31 - no action -, or none of the same units hit -; the first target standing)
        if (chunk.set && comboActors.size() >= 2 && chunk.combo && order[at][3] == 0 && order[at][0] >= 0) {
            Cr3Command lc;
            const auto& [nSrc, nCmd, nTgt, nFrom] = order[at];
            bool reset = command(chunk.command, lc) && (lc.flags & 0x200);
            if (reset) {
                const bool otherSide = fighters[size_t(chunk.attacker)].player != fighters[size_t(nSrc)].player;
                Cr3Command nc;
                bool shared = false;
                if (!otherSide && !hasKind(fighters[size_t(nSrc)], 31) && (nCmd < 0 || command(nCmd, nc))) // (effect kind 31, IsNoAction)
                    for (int t : wouldHit(nSrc, nCmd, nTgt))
                        shared |= std::find(chunk.hit.begin(), chunk.hit.end(), t) != chunk.hit.end();
                reset = !shared;
            }
            const int first = chunk.firstTarget;
            if (reset && first >= 0 && size_t(first) < fighters.size() && !fighters[size_t(first)].dead()) {
                // (the side's gauge pinned at its full advantage - BtlBreakGaugeUI::HaveAdvantageOver
                // 0x0099d41c: +-1000 -: a participant's Finest Arts, at random; else the attacker's Last Attack)
                int actor = -1, finisher = -1;
                bool isArts = false; // (BtlCommand +0x24: addLastAttack's pick a Finest Arts)
                if (gauge == (fighters[size_t(chunk.attacker)].player ? 1000 : -1000)) {
                    std::vector<std::pair<int, int>> arts;
                    for (int p : comboActors)
                        if (const int fa = finestArtsCommand(p); fa >= 0) arts.emplace_back(p, fa);
                    if (!arts.empty()) std::tie(actor, finisher) = arts[std::min(size_t(random() * float(arts.size())), arts.size() - 1)], isArts = true;
                }
                if (finisher < 0) actor = chunk.attacker, finisher = lastAttackCommand(actor), isArts = false;
                if (finisher >= 0 && actor >= 0 && !fighters[size_t(actor)].dead()) {
                    // (BtlCommandList::VInsertTop 0x007e4fd8: before the next, marked inserted - 2 here,
                    // a finisher: it leads into no other)
                    order.insert(order.begin() + long(at), {actor, finisher, first, isArts ? 3 : 2}); // (3: a Finest Arts)
                    if (at < bucket0End) ++bucket0End;
                    chunk.set = false;
                }
            }
        }
        resolvingFrom = order[at][3];
        nextAt = at + 1;
        const auto [src, cmd0, tgt0, fromSkill] = order[at];
        int cmd = cmd0, tgt = tgt0;
        if (src < 0) { // (a command with no actor: a status effect's kind 98 / 101 - Regenerate's 306 on its bearer)
            Cr3Action na = resolveSourceless(cmd, tgt);
            if (!na.targets.empty()) {
                afterAnyCommand(0, -1);
                snapshotAfter(na), out.push_back(na);
            }
            if (decided(at)) break;
            continue;
        }
        Cr3Fighter& s = fighters[size_t(src)];
        if (s.dead()) continue;
        // (BtlStatus::ResetCountPerCommandToCalculation, a command's start: BtlSkill::ResetCountPerCommand 0x9c608c - every unit's
        // skills' uses a command their rows' again: Narrow Escape / Blue-Moon Brave once a command)
        for (Cr3Fighter& u : fighters)
            for (auto& [id, uses] : u.skillUses)
                if (MbeFile::Row sr; tables_ && tables_->skill.find("Skill", id, kSkill, sr)) uses[2] = sr.i(6);
        // (BtlDamageChunk::Calculate 0x97ac10, the command's start: trigger 3 on the actor - its skills', its status effects' -
        // as the command is worked out (BtlCalculator::Calculate 0x971e94: Rally / Concentrate / Lock On enabled for a physical
        // / magic one, by its +0xc4); not for a command a skill or a status effect put in - an unlimited one's own interrupt
        // would trigger itself for ever. Then - its own chosen command alone: not a finisher's, chunk +0x18 / +0x1a (unsettled
        // which those are) - the first enabled kind 120 - GetCommandChange: Dance's 1512, the pig form's 9 - is the command, its
        // target chosen again (+0x20 -1); then end timing 3 - the kind 120 ones' - and the no-action, MP and ban tests below,
        // all on the new command)
        const bool ownCommand = fromSkill != 1 && fromSkill != 4;
        if (ownCommand) {
            Cr3Command pre;
            if (!command(cmd < 0 ? 51 : cmd, pre)) pre = Cr3Command();
            (void)triggerSkills(src, 3, pre.elementC4, false, cmd < 0 ? 51 : cmd);
            queueEffectCommands(src, triggerEffects(src, 3, pre.elementC4), -1);
            if (const int to = fromSkill == 0 ? commandChange(s) : -1; to >= 0) cmd = to, tgt = -1;
            endEffects(src, 3);
        }
        Cr3Action a;
        a.source = src, a.command = cmd;
        a.finisher = fromSkill == 2 || fromSkill == 3; // (a Last Attack - 2 - or Finest Arts - 3 - addLastAttack put in)
        // (MbStatusEffectInfo::IsNoAction: a kind 31 effect - stunned, asleep - and it does nothing;
        // a skill's command plays all the same)
        if (hasKind(s, 31) && (fromSkill == 0 || fromSkill == 4)) {
            a.command = -2, a.gaugeAfter = gauge;
            snapshotAfter(a), out.push_back(a);
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
            endEffects(src, 9); // (its end: what its trigger 3 enabled - In the Pink 6 at full HP - ended)
            a.gaugeAfter = gauge;
            snapshotAfter(a), out.push_back(a);
            continue;
        }
        Cr3Command c;
        if (!command(cmd, c)) c.power = s.attack, c.hits = 1, c.damage = 1;
        // HaveEnoughMp & IsDisableGeneCommand / IsDisableHealCommand (0x0097b0a0): abort mid-turn if MP/status blocks it.
        const int neededMp = c.damage == 22 ? 1 : mpCost(s, c);
        if ((!s.unlimitedMp && s.mp < neededMp) || (!fromSkill && (((c.flags & 1) && hasKind(s, 47)) || (c.ally() && hasKind(s, 67))))) {
            a.command = -2, a.gaugeAfter = gauge;
            snapshotAfter(a), out.push_back(a);
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
        const int perAction = std::max(c.hits, 1); // (each action's hits: BtlCalculator +0x48 actions)
        c.hits = perAction * std::max(c.repeat, 1);
        // (calculateCooperation @ 0x007dc7f4: normal bucket0 only, +0xbc bit 7, not a skill's, free of
        // kind 47 for a gene's; random byte under 154 (with advantage) or 77 + skill kind 80)
        if (at < bucket0End && (c.flags & 0x80) && !fromSkill && !((c.flags & 1) && hasKind(s, 47))) {
            const int chance = (advantage(s.player) ? 154 : 77) + skillConstant(s, 80);
            if (int(random() * 256.0f) < chance)
                for (size_t k = at + 1; k < bucket0End && k < order.size();) {
                    const std::array<int, 4> o = order[k];
                    if (o[0] < 0) {
                        ++k;
                        continue;
                    }
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
        if (s.unlimitedMp) {
            a.mpCost = 0;
        } else if (c.damage == 22) { // (calculateMpRelease: spends all remaining MP)
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
        std::map<int, int> actionsOn; // (target kind 6: the actions each pick took)
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
        else if (c.target == 6) {
            // (6 a random enemy - BtlDamageChunk::Calculate 0x0097ac10 calls CollectDamageTarget once per action,
            // collectDamageTargetRandomEnemy 0x009bd320: one opponent living before the command, at random, no
            // target filter; picks may repeat, each action its own BtlDamageTargetList of its hits. Here the
            // actions grouped by the unit they picked, in the order first picked)
            std::vector<int> living;
            for (size_t k = 0; k < fighters.size(); ++k)
                if (!fighters[k].dead() && fighters[k].player != s.player) living.push_back(int(k));
            for (int n = 0; n < std::max(c.repeat, 1) && !living.empty(); ++n) {
                const int t = living.size() == 1 ? living[0] : living[std::min(size_t(random() * float(living.size())), living.size() - 1)];
                if (actionsOn[t]++ == 0) targets.push_back(t);
            }
        }
        else if (int t = livingTarget(playersSide ? true : !s.player, tgt, &c); t >= 0) targets.push_back(t);
        if (targets.empty()) {
            if (outcome() != 0) break;
            continue;
        }
        std::vector<int> downed; // (its targets downed: their commands wait - delayCommandByDown)
        for (int chosen : targets) {
            // (BtlDamageTarget::Calculate 0x983a44: +0x110 bit 5 - the target first hit in the command, BtlDamageChunk::Calculate's
            // chunk-wide list, any command; targets here hold each unit once - first triggerBeforeCalculateTakeDamage 0x984034,
            // trigger 4 - its skills', its status effects' - and end timing 4; then, a damaging one, an enabled kind 49 - Mental
            // Wall - turns it back: the calculation on the caster - BtlCalculator::ChangeTarget, its source kept, +0xad -,
            // +0x110 bit 1)
            const bool hostile = c.damage >= 0 && !ally;
            // (before it, Cover - BtlSkillUtility::HasCover 0x9b0cac: the first unit in the list's order, either side, not
            // under an enabled kind 31 - a fallen one neither -, whose skills - its gauge's advantage's - have kind 118 of this
            // command takes it, +0x14; +0x18 the original: battle_0034's Brave Pigtapir - skill 435 - taking 1685 Meat Juice)
            int coveredFrom = -1;
            for (size_t k = 0; k < fighters.size(); ++k) {
                if (int(k) == chosen || fighters[k].dead() || hasKind(fighters[k], 31)) continue;
                const std::vector<int> covers = skillConstants(fighters[k], 118);
                if (std::find(covers.begin(), covers.end(), cmd) != covers.end()) {
                    coveredFrom = chosen, chosen = int(k);
                    break;
                }
            }
            int ti = chosen, reflectedFrom = -1;
            (void)triggerSkills(chosen, 4, c.elementC4, false, cmd, 0, src);
            queueEffectCommands(chosen, triggerEffects(chosen, 4, c.elementC4), src);
            if (hostile && chosen != src && hasKind(fighters[size_t(chosen)], 49)) reflectedFrom = chosen, ti = src;
            endEffects(chosen, 4);
            Cr3Fighter& t = fighters[size_t(ti)];
            Cr3Hit hit;
            hit.target = ti, hit.reflectedFrom = reflectedFrom, hit.coveredFrom = coveredFrom;
            bool landed = false;
            const int hpBefore = t.hp;
            int dealt = 0;          // (the hits' HP damage, summed: BtlDamageTarget's GetTotalDamage)
            bool damaging = false;  // (the hits worked out on the HP as they come, then put back for trigger 5)
            bool downedHere = false;
            // (BtlBasicStatus::TakeDamage 0x9b41c4: no lower than the least HP - the status effects' kind 53 a number, 121 a
            // percentage of the max - an endure -, and BtlSkillUtility::GetLeastHp 0x9b0368: the enabled skills' highest kind 53,
            // Narrow Escape's / Blue-Moon Brave's 1; a skill's added command past it - the spider's finisher)
            const auto leastHp = [&]() {
                return c.damage == 24 ? std::max(c.power, 1)
                       : fromSkill == 1 ? 0
                                        : std::max({effectMax(t, 53), int(float(effectMax(t, 121)) * 0.01f * float(t.maxHp)), skillMost(t, 53)});
            };
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
                damaging = true;
                const Cr3Fighter& u = fighters[size_t(src)];
                bool critUnit = false, downUnit = false, critShown = false;
                float rate = 1;
                rollCritAndDown(u, t, c, critUnit, downUnit, rate);
                const uint8_t elemColor = rate < 1.0f ? 2 : rate > 1.0f ? 1 : 0;
                const int hitsOn = c.target == 6 ? perAction * actionsOn[coveredFrom >= 0 ? coveredFrom : chosen] : c.hits;
                for (int wh = 0; wh < hitsOn; ++wh) {
                    if (!hits(u, t, c)) { // (a miss: no damage, no points)
                        hit.hits.push_back(0), hit.mpHits.push_back(0), hit.flags.push_back(4), hit.colors.push_back(0);
                        continue;
                    }
                    landed = true;
                    lastContinued |= t.comboChance;
                    a.combo |= t.comboChance; // (BtlDamageTarget::notifyCombo: HaveComboChanceById - event 2)
                    if (c.damage == 20) { // (calculateMpDamage 0x7e2010: reduces target MP, colour 4)
                        // (calculateMagicDamage's body - calculateBasicDamage, x GetMagicDamageMultipliers - into the MP's
                        // +0x58, the HP's +0x50 0; calculateMpFluctuation 0x7e38a4: the spread, at most the target's MP)
                        int m = hitDamage(u, t, c, critUnit, downUnit, rate);
                        m = partyUnhurt && t.player ? 0 : std::clamp(m, 0, t.mp);
                        hit.hits.push_back(0), hit.mpHits.push_back(m), hit.flags.push_back(0), hit.colors.push_back(4);
                        t.mp = std::max(t.mp - m, 0);
                        continue;
                    }
                    if (c.damage == 21) { // (calculateStealGold: the physical damage - CommandFomula 20, chain -1 -
                        // the stolen, min(it, the party's money), no HP lost; the thief carries it - AddGold)
                        const int raw = partyUnhurt && t.player ? 0 : hitDamage(u, t, c, critUnit, downUnit, rate);
                        if (raw <= 0) { // (blocked to 0: nothing stolen, no announcement)
                            hit.hits.push_back(0), hit.mpHits.push_back(0), hit.flags.push_back(0), hit.colors.push_back(elemColor);
                            continue;
                        }
                        const int st = std::clamp(raw, 0, std::max(gold, 0));
                        gold -= st, fighters[size_t(src)].stolenGold += st;
                        hit.stolen = std::max(hit.stolen, 0) + st;
                        hit.hits.push_back(0), hit.mpHits.push_back(0), hit.flags.push_back(uint8_t(downUnit ? 2 : 0)), hit.colors.push_back(elemColor);
                        if (downUnit) t.comboChance = true, ++t.combo;
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
                    // (calculateHpFluctuation 0x7e35e0: the target's enabled skill kind 91 - the Eggons' 479, trigger
                    // 4 - makes the HP change its constant, GetFixedDamage's highest: -1 one damage a hit)
                    if (int fixed = 0; rawDmg > 0 && skillEnabled(t, 91, &fixed)) rawDmg = std::max(-fixed, 0);
                    // (calculateFluctuation 0x7e122c: blocked - no HP lost - by the target's enabled status kind 62,
                    // or kind 3 against a physical command, or CanBlockBySkill 0x7e2924: unless the attacker's skill
                    // kind 89 pierces, the target's skill of the element's block kind - BtlElement +0x0c, 4e + 5 -,
                    // kind 3 against a physical command, 6 against magic - enabled ones: 121 / 122's by trigger 4, their chance
                    // rolled there once a command -; never on a hit on oneself not reflected)
                    bool blocked = false;
                    if (rawDmg > 0 && (reflectedFrom >= 0 || ti != src)) {
                        blocked = hasKind(t, 62) || (c.elementC4 == 1 && hasKind(t, 3));
                        if (!blocked && !skillEnabled(u, 89))
                            blocked = (c.element >= 1 && c.element <= 6 && skillEnabled(t, 4 * c.element + 5)) ||
                                      (c.elementC4 == 1 && skillEnabled(t, 3)) || (c.elementC4 == 2 && skillEnabled(t, 6));
                    }
                    if (blocked) rawDmg = 0;
                    const bool downHit = downUnit && !blocked; // (calculateDown 0x7e0ddc: CanBlockBySkill - no down)
                    int d = partyUnhurt && t.player ? 0 : rawDmg;
                    // (calculateHpFluctuation 0x7e35e0: an HP steal - +0xbd bit 5 - skips the element's absorb test and
                    // takes no more than the target has)
                    if (c.flags & 0x2000) d = std::min(d, std::max(t.hp, 0));
                    const bool markCrit = critUnit && !critShown && c.damage != 8 && c.damage != 24 && !blocked;
                    if (markCrit) critShown = true;
                    hit.hits.push_back(d);
                    hit.mpHits.push_back(0);
                    hit.flags.push_back(uint8_t((markCrit ? 1 : 0) | (downHit ? 2 : 0) | (blocked ? 8 : 0)));
                    hit.colors.push_back(elemColor);
                    ++t.chain; // (BtlStatusList::AddChainNum: after CalculateChainBonus on landed hit)
                    const int least = leastHp();
                    t.hp = std::max(t.hp - d, t.hp > 0 ? std::min(least, t.hp) : 0);
                    dealt += d;
                    if (s.player && !t.player) partyDamage = std::min(partyDamage + d, 9999999L);
                    downedHere |= downHit;
                    if (downHit) t.comboChance = true, ++t.combo; // (BtlBasicStatus::TakeDamage, addComboNum)
                    anyCrit |= critUnit && !blocked, anyDown |= downHit;
                    if (downHit && std::find(downed.begin(), downed.end(), ti) == downed.end()) downed.push_back(ti);
                }
            }
            // (playCommand 0x7dc940's first step - BtlDamageChunk::TriggerDamageToCalculation 0x97d7b0, before the HP change:
            // trigger 5 once a target - its skills' with param +0x1c the target's total HP change, below 0 damage, +0x20 a
            // down -, then its status effects' and AddEffectCommand; Narrow Escape 134 / Blue-Moon Brave 132 enabled - kind 53,
            // the least HP 1 - and their status effect 109 given. The original fires it for all of a command's targets before
            // any HP changes: here a target at a time, each hit's damage worked out first on the HP, put back, the trigger, then
            // the sum taken under the least HP as it then stands - the same as hit by hit under one least HP; a heal's after)
            int total = 0;
            for (size_t h = 0; h < hit.hits.size(); ++h)
                if (!(hit.flags[h] & 4)) total -= hit.hits[h];
            if (damaging) t.hp = hpBefore, total = -dealt;
            (void)triggerSkills(ti, 5, c.elementC4, downedHere, cmd, total, src);
            queueEffectCommands(ti, triggerEffects(ti, 5, c.elementC4, downedHere), src);
            if (damaging) {
                t.hp = std::max(hpBefore - dealt, hpBefore > 0 ? std::min(leastHp(), hpBefore) : 0);
                if (s.player && !t.player) killed += hpBefore > 0 && t.hp <= 0;
            }
            // the status effects it takes away (BtlDamage's RemovesStatusEffect), where it landed
            if (landed)
                for (int e : c.removes) removeEffect(ti, e);
            // its status effects, where it landed and the target lives (IsStatusEffectHit + skill 84 continue boost)
            if (landed && !t.dead()) {
                statusTurnBoost_ = skillConstant(s, 84);
                for (int e : c.effects)
                    if (effectLands(s, t, c, e)) {
                        giveEffect(ti, e), hit.effectsGiven.push_back(e);
                        // (a kind 52 effect - a provoke: BtlStatusEffect::GetForceTarget - its giver)
                        MbeFile::Row er;
                        if (tables_ && tables_->skill.find("StatusEffect", e, kStatusEffect, er))
                            for (size_t q = 0; q < 4; ++q)
                                if (er.i(14 + q * 2) == 52) t.forcedBy = src;
                    }
                statusTurnBoost_ = 0;
            }
            // (a call for help - extraType 0x11, Rainforcement: BtlCalculator::CalculateStatusEffect 0x9844a8 - +0x110 bit 4
            // from a byte under the command's chance, +0x38[0] of 256 -, then GiveStatusEffectToCalculation 0x985740 ->
            // BtlMonsterStatus::VRainforceToCalculation 0x9c6ef0 on the fallen target: its EXP, money and drop banked
            // first, the slot made the source's own species - or the command's first constant -, vInitialize - its own
            // level or the slot's, HP and MP full, mode 0 -, its status effects cleared; the skills' opening trigger not in)
            if (c.extraType == 17 && t.dead() && !t.player) {
                a.reinforced.fighter = ti;
                a.reinforced.success = int(random() * 256.0f) < c.effectChance;
                const int species = c.extraConstants.empty() ? s.id : c.extraConstants[0];
                if (Cr3Fighter nf; a.reinforced.success && monsterFighter(species, t.level, nf)) {
                    banked.push_back(t);
                    nf.unit = t.unit;
                    t = nf;
                    a.reinforced.monster = species;
                } else {
                    a.reinforced.success = false;
                }
            }
            // (BtlDamageTarget::TriggerAfterDamageToCalculation 0x988498, once a target: its first hit - a miss, +0x5c, ends it
            // with no trigger at all -: trigger 7 - its skills', its status effects'; condition 20 that hit's +0x69, it downed the
            // unit - and end timing 6 (the hit counters, Daunted); that hit not blocked - +0x6e - and the landed hits' HP change
            // summed no heal - calculateHpFluctuation's sign: damage below 0 -: trigger 8 and end timing 7 (Counter / Magic
            // Counter, Braveheart's 1525); no hit at all - a status command's -: trigger 9 and end timing 8)
            if (hit.hits.empty()) {
                (void)triggerSkills(ti, 9, c.elementC4, false, cmd, 0, src);
                queueEffectCommands(ti, triggerEffects(ti, 9, c.elementC4), src);
                endEffects(ti, 8);
            } else if (!(hit.flags[0] & 4)) {
                const bool firstDown = (hit.flags[0] & 2) != 0, firstBlocked = (hit.flags[0] & 8) != 0;
                (void)triggerSkills(ti, 7, c.elementC4, firstDown, cmd, 0, src);
                queueEffectCommands(ti, triggerEffects(ti, 7, c.elementC4, firstDown), src);
                endEffects(ti, 6);
                int change = 0;
                for (size_t h = 0; h < hit.hits.size(); ++h)
                    if (!(hit.flags[h] & 4)) change += hit.hits[h];
                if (!firstBlocked && change >= 0) {
                    (void)triggerSkills(ti, 8, c.elementC4, false, cmd, 0, src);
                    queueEffectCommands(ti, triggerEffects(ti, 8, c.elementC4), src);
                    endEffects(ti, 7);
                }
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
        if (!ally && c.breakPoint >= 0) {
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
                        a.broke |= addBreak(points * (s.player ? 1 : -1)); // (BtlDamageTarget::AddBreakPointToCalculation: event 3)
                    }
                    th.gauge.push_back(gauge);
                }
            }
        }
        if (hasSkillKind(s, 70)) gauge = 0, gaugeStarted = false; // (Break Reset passive skill 70 @ 0x0097ac10)
        // (BtlDamageChunk::CalculateForceBreakPoint 0x0097b77c -> BtlCalculator::calculateForceBreakPoint 0x007e15a8, the
        // chunk's +0x98: a Finest Arts - BtlCalculator +0xa5 - +500 for the party, -500 for the monsters; else extra
        // type 13 - command 398 - one of its constants, shuffled; ConsumeBreakPointToCalculation 0x0097b920 then sets the
        // gauge to it - BtlPoint::SetValue, held within its bounds - after the action's own points, where it isn't 0)
        int forced = 0;
        if (fromSkill == 3) forced = s.player ? 500 : -500;
        else if (c.extraType == 13 && !c.extraConstants.empty())
            forced = c.extraConstants[std::min(size_t(random() * float(c.extraConstants.size())), c.extraConstants.size() - 1)];
        if (forced != 0) gauge = std::clamp(forced, -1000, 1000), gaugeStarted = true;
        coopNow_ = 1;
        for (Cr3Hit& th : a.targets)
            while (th.gauge.size() < th.hits.size()) th.gauge.push_back(gauge);
        // (an HP / MP steal - +0xbd & 0x60: BtlDamageTargetList::Calculate 0x97f81c's BtlDamageTarget for the source on
        // itself, appended last; BtlDamageTarget::SetStealDamage 0x988eb4 for each hit k of the first target's - its HP
        // change minus the targets' hit-k HP damage summed, its MP the same, the landed hits alone -, a recovery where not
        // 0; TakeDamage's clamp at the max)
        if ((c.flags & 0x6000) && !s.dead()) {
            Cr3Hit back;
            back.target = src, back.stealBack = true;
            int hpSum = 0, mpSum = 0;
            for (size_t k = 0; k < a.targets.front().hits.size(); ++k) {
                int hp = 0, mp = 0;
                for (const Cr3Hit& th : a.targets)
                    if (k < th.hits.size() && !(th.flags[k] & 4)) hp += th.hits[k], mp += k < th.mpHits.size() ? th.mpHits[k] : 0;
                back.hits.push_back(-hp), back.mpHits.push_back(-mp), back.flags.push_back(0);
                back.colors.push_back(hp == 0 && mp != 0 ? 4 : 3), back.gauge.push_back(gauge);
                hpSum += hp, mpSum += mp;
            }
            s.hp = std::min(s.hp + std::max(hpSum, 0), s.maxHp), s.mp = std::min(s.mp + std::max(mpSum, 0), s.maxMp);
            back.hpAfter = s.hp, back.mpAfter = s.mp;
            a.targets.push_back(back);
        }
        a.gaugeAfter = gauge;
        a.target = a.targets[0].target, a.hits = a.targets[0].hits, a.hpAfter = a.targets[0].hpAfter, a.killed = a.targets[0].killed;
        // (the command's end - playCommand's order: its trigger 10, every unit's trigger 11, the actor's end timing 9, every
        // unit's end timing 10; the break points - AddBreakPointToCalculation - after them there, before them here)
        if (ownCommand) {
            (void)triggerSkills(src, 10, c.elementC4, false, cmd); // (its commands: the trigger 10 skills' below)
            queueEffectCommands(src, triggerEffects(src, 10, c.elementC4), -1);
        }
        afterAnyCommand(c.elementC4, src);
        // (calculateCommands 0x7db93c, after a command of extraType 0xb - Reverse Gravity 387: BtlFieldEffectList::Add
        // 0x7deaac - type 0, 5 turns: a toggle, cleared where it is on -, then lists 0 and 2's bit 3 to match; their
        // BtlCommandList::Sort before every next command - the rest of this turn re-sorted here, the inserted left be)
        if (c.extraType == 11) {
            if (reverseSpeedTurns > 0) reverseSpeedTurns = 0, reverseSpeed = false;
            else reverseSpeedTurns = 5, reverseSpeed = true;
            a.fieldToggle = reverseSpeed ? 1 : -1;
            for (const bool late : {false, true}) {
                std::vector<size_t> slots; // (the list's own entries still to come: bucket 0, then bucket 2 - -8)
                std::vector<std::array<int, 4>> rest;
                for (size_t k = at + 1; k < order.size(); ++k) {
                    if (order[k][3] != 0 || order[k][0] < 0) continue;
                    const int pri = priorityAndCost(order[k][0], order[k][1]).first;
                    if (pri != -9 && (pri == -8) == late) slots.push_back(k), rest.push_back(order[k]);
                }
                sortBucket(rest);
                for (size_t k = 0; k < slots.size(); ++k) order[slots[k]] = rest[k];
            }
        }
        snapshotAfter(a), out.push_back(a);
        if (decided(at)) break;
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
                if (order[k][0] == d && order[k][3] == 0) { // (a skill's or a status effect's - an interrupt - stays)
                    const auto moved = order[k];
                    order.erase(order.begin() + long(k));
                    order.push_back(moved);
                    break;
                }
    }
    return out;
}

int Cr3BattleRules::orderChance(int row, int which) const {
    return row >= 0 && size_t(row) < orderChances_.size() && which >= 0 && which < 3 ? orderChances_[size_t(row)][size_t(which)] : 0;
}

int Cr3BattleRules::randomTarget(int fi, int cmd) {
    Cr3Command c;
    if (!command(cmd, c)) return -1;
    const std::vector<int> list = collectTargets(fi, c);
    return list.empty() ? -1 : list[std::min(size_t(random() * float(list.size())), list.size() - 1)];
}

std::vector<std::array<int, 3>> Cr3BattleRules::massAttackChoices() {
    std::vector<int> members; // (InitializeMenu: the party's living, none under kind 31)
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player && !fighters[k].dead() && !hasKind(fighters[k], 31)) members.push_back(int(k));
    std::vector<std::array<int, 3>> out;
    if (members.empty()) return out;
    int target = -1;
    if (Cr3Command c; command(attackCommandOf(members[0]), c))
        for (int t : collectTargets(members[0], c)) // (GetLowestHpTarget: +0x20, the first of the lowest)
            if (target < 0 || fighters[size_t(t)].hp < fighters[size_t(target)].hp) target = t;
    for (int f : members) {
        const int cmd = attackCommandOf(f);
        out.push_back({f, cmd, hasKind(fighters[size_t(f)], 51) ? randomTarget(f, cmd) : target});
    }
    return out;
}

namespace {

// MbStatusEffectInfo::GetAutomaticChance 0x90bb1c: a cure's weight for taking the effect away (the
// table at 0xf43ae4, by the effect's id 13 .. 20; 19 / 20 the transformations'); 0 the rest
int automaticChance(int effect) {
    static constexpr int kChance[8] = {51, 153, 25, 0, 0, 0, 256, 256};
    return effect >= 13 && effect <= 20 ? kChance[effect - 13] : 0;
}

// a candidate (BtlAutomaticCommand): the actor, the command, its target and what it would do to
// each it hits - the HP and MP change (damage below 0), the effects taken away and given
struct AutoEffect {
    int target = -1, hp = 0, mp = 0;
    std::vector<int> erased, added;
};
struct AutoCandidate {
    int actor = -1, command = -1, target = -1, type = 0, mp = 0, speed = 0;
    bool forced = false; // (a guard for want of an attack: BtlAutomaticCommand +0x30)
    std::vector<AutoEffect> on;
};

} // namespace

std::vector<std::array<int, 3>> Cr3BattleRules::autoChoices(bool player) {
    std::vector<std::array<int, 3>> out;
    std::vector<int> actors, direct; // (BtlAutomaticActorList::Initialize 0x971774: one action each - a character's 1)
    for (size_t k = 0; k < fighters.size(); ++k) {
        const Cr3Fighter& f = fighters[k];
        if (f.player != player || f.dead() || hasKind(f, 31)) continue;
        (hasKind(f, 51) ? direct : actors).push_back(int(k)); // (kind 51 - IsAutoAttack: its attack at random, added as it is)
    }
    // the simulated statuses (the plan's picks reflected) and the predictor - a copy of the rules, so
    // the battle's own RNG and numbers are left as they are
    std::vector<int> hp, mp;
    std::vector<std::set<int>> gone; // (the effects the plan takes away)
    for (const Cr3Fighter& f : fighters) hp.push_back(std::max(f.hp, 0)), mp.push_back(f.mp), gone.emplace_back();
    Cr3BattleRules sim = *this;
    const auto has = [&](int t, int e) { return !gone[size_t(t)].count(e) && ((e == 1 && hp[size_t(t)] <= 0) || hasEffect(t, e)); };
    const auto predict = [&](int src, const Cr3Command& c, int tgt) {
        std::vector<AutoEffect> on;
        Cr3Fighter& s = sim.fighters[size_t(src)];
        const std::vector<int> hit = c.all ? collectTargets(src, c) : std::vector<int>{tgt};
        Cr3Command own; // (the hits, as turn(): below 0 the unit's own, + skill kind 104; magic + kind 105)
        const int each = c.hitsOwn ? (s.player ? std::max(s.weaponHits, 1) : command(s.attackCommand, own) ? own.hits : 1) + skillConstant(s, 104) : c.hits;
        const int hits = std::max(each, 1) * std::max(c.repeat + (c.type == 2 ? skillConstant(s, 105) : 0), 1);
        for (int ti : hit) {
            if (ti < 0 || size_t(ti) >= fighters.size()) continue;
            Cr3Fighter& t = sim.fighters[size_t(ti)];
            AutoEffect e;
            e.target = ti;
            if (c.damage < 0 || c.damage == 21) {
            } else if (c.damage == 16 || c.damage == 17) {
                e.mp = c.damage == 16 ? std::max(c.power, 0) : std::max(int(float(t.maxMp) * float(c.power) / 100.0f), 0);
            } else if (c.ally()) {
                int h = 1;
                if (c.damage == 5) h = sim.heal(s, c);
                else if (c.damage == 6) h = c.chain == 19 ? chocolatBonus(t, std::max(c.power, 1)) : std::max(c.power, 1);
                else if (c.damage == 7 || c.damage == 18) h = std::max(int(float(t.maxHp) * float(c.power) / 100.0f), 1);
                else if (c.damage == 15) {
                    int friends = 0;
                    for (size_t k = 0; k < fighters.size(); ++k) friends += fighters[k].player == s.player && hp[k] > 0;
                    h = std::max(int(float(friends) * 0.1f * float(t.maxHp)), 1);
                }
                e.hp = h;
                if (c.damage == 18) e.mp = std::max(int(float(t.maxMp) * float(c.power) / 100.0f), 0);
            } else if (c.damage == 20) {
                e.mp = -std::clamp(c.power, 0, std::max(t.mp, 0));
            } else {
                int d = 0;
                for (int n = 0; n < hits; ++n) {
                    int one = c.damage == 8    ? std::max(int(float(t.hp - d) * float(std::max(c.power - skillConstant(t, 35), 0)) / 100.0f), 0)
                              : c.damage == 24 ? std::max(t.hp - d - std::max(c.power, 1), 0)
                                               : sim.damage(s, t, c);
                    if (c.damage == 12 && c.extraType != 18) one *= int(t.effects.size()) + 1;
                    d += one;
                }
                e.hp = -d;
            }
            for (int r : c.removes)
                if (has(ti, r)) e.erased.push_back(r);
            for (int g : c.effects)
                if (!has(ti, g)) e.added.push_back(g);
            on.push_back(e);
        }
        return on;
    };
    // EnumerateCommand 0x970edc: each actor's commands (its attack - transformation-aware -, its gene's;
    // a monster's mode's) of the select types, one candidate a target
    const auto enumerate = [&](std::initializer_list<int> types) {
        std::vector<AutoCandidate> list;
        for (int a : actors) {
            const Cr3Fighter& f = fighters[size_t(a)];
            std::vector<int> ids{attackCommandOf(a)};
            if (player) ids.insert(ids.end(), f.geneCommands.begin(), f.geneCommands.end());
            else
                for (const Cr3MonsterCommand& m : f.commands)
                    if (m.uses != 0) ids.push_back(m.command);
            for (int id : ids) {
                Cr3Command c;
                if (!command(id, c) || !c.usable() || std::find(types.begin(), types.end(), c.selectType) == types.end()) continue;
                if ((c.selectType == 3 || c.selectType == 9) && hasKind(f, 138)) continue; // (CanUseSelectType)
                if (((c.flags & 1) && hasKind(f, 47)) || c.noAuto) continue;              // (a gene's sealed; never on auto)
                const int mpNeed = c.damage == 22 ? 1 : mpCost(f, c);
                if (hpCost(f, c) > hp[size_t(a)] || (mp[size_t(a)] >= 0 && mpNeed > mp[size_t(a)])) continue;
                if (c.extraType == 10 && !c.extraConstants.empty() && c.extraConstants[0] > hp[size_t(a)]) continue;
                std::vector<int> targets = collectTargets(a, c);
                if (c.all && !targets.empty()) targets.resize(1); // (an all-target command: one candidate, every target in it)
                for (int t : targets) {
                    AutoCandidate k;
                    k.actor = a, k.command = id, k.target = t, k.type = c.selectType, k.mp = mpNeed, k.speed = speedOf(f) + c.speedCost;
                    k.on = predict(a, c, t);
                    list.push_back(std::move(k));
                }
            }
        }
        return list;
    };
    std::map<int, AutoCandidate> picks; // (by actor: a later pass's in place of an earlier's)
    // Select 0x972e14: while candidates are left and the gate holds - the rejected dropped, the best
    // taken (every other of its actor's dropped), its outcome reflected for the next
    const auto select = [&](std::vector<AutoCandidate> list, auto reject, auto better, auto gate) {
        while (!list.empty() && gate()) {
            list.erase(std::remove_if(list.begin(), list.end(), reject), list.end());
            if (list.empty()) break;
            const AutoCandidate best = *std::min_element(list.begin(), list.end(), better);
            list.erase(std::remove_if(list.begin(), list.end(), [&](const AutoCandidate& k) { return k.actor == best.actor; }), list.end());
            for (const AutoEffect& e : best.on) {
                const size_t t = size_t(e.target);
                for (int r : e.erased) gone[t].insert(r);
                if (hp[t] > 0 || std::find(e.erased.begin(), e.erased.end(), 1) != e.erased.end())
                    hp[t] = std::clamp(hp[t] + e.hp, 0, fighters[t].maxHp);
                if (mp[t] >= 0) mp[t] = std::clamp(mp[t] + e.mp, 0, fighters[t].maxMp);
            }
            picks[best.actor] = best;
        }
    };
    const auto always = [] { return true; };
    const auto kills = [&](const AutoCandidate& k) {
        int n = 0;
        for (const AutoEffect& e : k.on) n += e.hp < 0 && hp[size_t(e.target)] > 0 && hp[size_t(e.target)] + e.hp <= 0;
        return n;
    };
    const auto rate = [&](const AutoCandidate& k) { // (the HP rate taken - to what is left -, x 100000 as an int)
        float r = 0;
        for (const AutoEffect& e : k.on)
            if (e.hp < 0 && hp[size_t(e.target)] > 0) r += float(std::min(-e.hp, hp[size_t(e.target)])) / float(std::max(fighters[size_t(e.target)].maxHp, 1));
        return int(r * 100000.0f);
    };
    const auto total = [](const AutoCandidate& k) {
        int d = 0;
        for (const AutoEffect& e : k.on) d += e.hp;
        return d;
    };
    const auto targetHp = [&](const AutoCandidate& k) { return k.target >= 0 ? hp[size_t(k.target)] : 0; };
    // SelectAttack 0x9702a4 (types 0 attack, 1 physical skill, 2 magic): no damage turned down; the
    // most kills, the most HP rate, the least MP, the fastest, the least damage (no overkill), the
    // lowest target HP
    select(
        enumerate({0, 1, 2}), [&](const AutoCandidate& k) { return total(k) >= 0; },
        [&](const AutoCandidate& a, const AutoCandidate& b) {
            return std::make_tuple(-kills(a), -rate(a), a.mp, -a.speed, -total(a), targetHp(a)) <
                   std::make_tuple(-kills(b), -rate(b), b.mp, -b.speed, -total(b), targetHp(b));
        },
        always);
    // IsEasyPrey 0x975bdc: no foe left standing with under half its HP taken - the attacks alone
    bool easy = true;
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player != player && !fighters[k].dead() && hp[k] != 0 &&
            std::abs(float(std::max(fighters[k].hp, 0) - hp[k]) / float(std::max(fighters[k].maxHp, 1))) < 0.5f)
            easy = false;
    if (!easy) {
        // SelectGuard 0x97058c (EnumerateGuardCommands 0x971a18: an actor is offered one where the side has
        // a heal to give, or where it has no attack - that one forced, never turned down); otherwise kept
        // only at 20 % HP or less, a random byte under its order type's guard chance, and not alone
        int alive = 0;
        for (const Cr3Fighter& f : fighters) alive += f.player == player && !f.dead();
        const bool heals = !enumerate({3}).empty();
        std::vector<AutoCandidate> guards;
        for (int a : actors) {
            const bool forced = !picks.count(a);
            if (!forced && !heals) continue;
            const Cr3Fighter& f = fighters[size_t(a)];
            const int chance = orderChance(player ? f.geneOrder : orderType, 2);
            if (!forced && !(float(hp[size_t(a)]) <= float(f.maxHp) * 0.2f && int(random() * 256.0f) < chance && alive != 1)) continue;
            AutoCandidate g;
            g.actor = a, g.command = -1, g.target = -1, g.type = 7, g.forced = forced;
            guards.push_back(g);
        }
        select(guards, [](const AutoCandidate&) { return false; }, [](const AutoCandidate&, const AutoCandidate&) { return false; }, always);
        // SelectDebuffer 0x97071c / SelectBuffer 0x970898: 1.1.3's reject turns every one down (an order
        // type's row found - MbGetOrderTypeInfo, rows 0 .. 5 - is taken as a reason to), so none.
        // SelectCure 0x970a14 (type 4): the erased effects' weight - none turned down -, then the least MP,
        // the fastest, the target's gene level, its HP
        const auto weight = [&](const AutoCandidate& k) {
            int w = 0;
            for (const AutoEffect& e : k.on)
                for (int r : e.erased) w += gone[size_t(e.target)].count(r) ? 0 : automaticChance(r);
            return w;
        };
        const auto level = [&](const AutoCandidate& k) { return k.target >= 0 ? fighters[size_t(k.target)].geneLevel : 0; };
        select(
            enumerate({4}), [&](const AutoCandidate& k) { return weight(k) == 0; },
            [&](const AutoCandidate& a, const AutoCandidate& b) {
                return std::make_tuple(-weight(a), a.mp, -a.speed, -level(a), targetHp(a)) < std::make_tuple(-weight(b), b.mp, -b.speed, -level(b), targetHp(b));
            },
            always);
        // SelectHeal 0x970bb0 (types 3 and 9): first the revives where one of the side is down (the most
        // brought back, the fastest, the least MP), then the heals while the lowest living HP rate's gate
        // holds - 50 % or less half the time, 35 % three quarters, 20 % always -: the most HP put back
        // (to the max), the least MP, the fastest
        const auto revived = [&](const AutoCandidate& k) {
            int n = 0;
            for (const AutoEffect& e : k.on) n += hp[size_t(e.target)] <= 0 && std::find(e.erased.begin(), e.erased.end(), 1) != e.erased.end();
            return n;
        };
        bool down = false;
        for (size_t k = 0; k < fighters.size(); ++k) down |= fighters[k].player == player && hp[k] <= 0;
        std::set<int> reviving;
        if (down) {
            select(
                enumerate({3, 9}), [&](const AutoCandidate& k) { return revived(k) == 0; },
                [&](const AutoCandidate& a, const AutoCandidate& b) { return std::make_tuple(-revived(a), -a.speed, a.mp) < std::make_tuple(-revived(b), -b.speed, b.mp); },
                always);
            for (const auto& [a, p] : picks)
                if (p.type == 3 || p.type == 9) reviving.insert(a);
        }
        const auto healed = [&](const AutoCandidate& k) {
            int n = 0;
            for (const AutoEffect& e : k.on)
                if (e.hp > 0 && hp[size_t(e.target)] > 0) n += std::min(e.hp, fighters[size_t(e.target)].maxHp - hp[size_t(e.target)]);
            return n;
        };
        const auto gate = [&] {
            float lowest = 2;
            for (size_t k = 0; k < fighters.size(); ++k)
                if (fighters[k].player == player && hp[k] > 0) lowest = std::min(lowest, float(hp[k]) / float(std::max(fighters[k].maxHp, 1)));
            if (lowest <= 0.2f) return true;
            if (lowest > 0.5f) return false;
            const int roll = int(random() * 256.0f);
            return roll < (lowest <= 0.35f ? 192 : 128);
        };
        std::vector<AutoCandidate> heal = enumerate({3, 9});
        heal.erase(std::remove_if(heal.begin(), heal.end(), [&](const AutoCandidate& k) { return reviving.count(k.actor) != 0; }), heal.end());
        select(
            heal, [&](const AutoCandidate& k) { return healed(k) == 0; },
            [&](const AutoCandidate& a, const AutoCandidate& b) { return std::make_tuple(-healed(a), a.mp, -a.speed) < std::make_tuple(-healed(b), b.mp, -b.speed); },
            gate);
    }
    // (BtlCommandManager::Add each pick; an actor left without one guards)
    for (int a : actors) {
        const auto it = picks.find(a);
        out.push_back(it == picks.end() ? std::array<int, 3>{a, -1, -1} : std::array<int, 3>{a, it->second.command, it->second.target});
    }
    for (int a : direct) {
        const int cmd = attackCommandOf(a);
        out.push_back({a, cmd, randomTarget(a, cmd)});
    }
    std::sort(out.begin(), out.end(), [](const std::array<int, 3>& x, const std::array<int, 3>& y) { return x[0] < y[0]; });
    return out;
}

bool Cr3BattleRules::effectInvalid(const Cr3Fighter& t, int id) const {
    bool immune = false;
    if (tables_) eachEffectKind(tables_->skill, t, 68, [&](int blocked) { if (blocked == id) immune = true; });
    for (int blocked : skillConstants(t, 68))
        if (blocked == id) immune = true;
    return immune;
}

std::vector<std::pair<int, int>> Cr3BattleRules::triggerEffects(int fi, int trigger, int attackKind, bool downed) {
    std::vector<std::pair<int, int>> added;
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size()) return added;
    Cr3Fighter& f = fighters[size_t(fi)];
    MbeFile::Row r;
    for (const auto& [id, turns] : f.effects) {
        if (!tables_->skill.find("StatusEffect", id, kStatusEffect, r) || r.i(8) != trigger || trigger == 0) continue;
        const auto u = f.effectUses.find(id);
        const int uses = u != f.effectUses.end() ? u->second : -1;
        if (uses == 0) continue;
        if (!conditionHolds(f, r.i(9), r.i(10), attackKind, downed, turns, -1)) continue;
        f.effectsDormant.erase(id);
        if (uses > 0) f.effectUses[id] = uses - 1;
        // (BtlStatusList::AddEffectCommand: the commands of the kinds it enables - 97 interrupting next, 98 the
        // same with no actor, 99 a counter, 100 after, 101 after with no actor - BtlStatusEffectUtility)
        for (size_t k = 0; k < 4; ++k)
            if (const int kind = r.i(14 + k * 2); kind >= 97 && kind <= 101) added.emplace_back(kind, r.i(15 + k * 2));
    }
    return added;
}

// (the conditions: IsLowHp - HP / max HP at or under the constant's % -, the command's +0xc4 - 1 physical, 2 magic -, the
// turns left, the HP against the constant, the hit's +0x69 - it downed the unit -; a skill's (BtlSkill::Trigger's switch) 2 HP
// at or over the constant's %, 4 the command the unit's own attack (param +0x48 - VGetAttackCommandId, vtable +0xb0 - against
// +8, neither -1), 5 not downed and physical, 15 physical or magic, 19 the command the constant, 21 holding status effect
// the constant, 23 the other unit's HP at or under it, 24 / 25 the device's hour in 6 .. 17 / outside, 28 every foe's HP at
// or under it, 29 HP + the trigger's HP change at 0 or under at the constant's % or over (Narrow Escape 134); the rest - 9 ..
// 14, 16, 26, 27 - not traced: never)
bool Cr3BattleRules::conditionHolds(const Cr3Fighter& f, int cond, int c, int attackKind, bool downed, int turns, int cmd, int total,
                                    int other) const {
    const auto hpAtLeast = [&](int pct) { return f.maxHp > 0 && int64_t(f.hp) * 100 >= int64_t(pct) * f.maxHp; };
    switch (cond) {
    case 0: return true;
    case 2: return hpAtLeast(c);
    case 3: return f.maxHp > 0 && float(f.hp) / float(f.maxHp) <= float(c) / 100.0f;
    case 4: return cmd >= 0 && &f >= fighters.data() && &f < fighters.data() + fighters.size() && cmd == attackCommandOf(int(&f - fighters.data()));
    case 5: return !downed && attackKind == 1;
    case 6: return attackKind == 1;
    case 7: return attackKind == 2;
    case 15: return attackKind == 1 || attackKind == 2;
    case 17: return turns >= 0 && turns <= c;
    case 18: return f.hp >= c;
    case 19: return cmd >= 0 && cmd == c;
    case 20: return downed;
    case 21: return std::any_of(f.effects.begin(), f.effects.end(), [c](const std::pair<int, int>& e) { return e.first == c; });
    case 22: return f.hp <= c;
    case 23: return other >= 0 && size_t(other) < fighters.size() && fighters[size_t(other)].hp <= c;
    case 24:
    case 25: {
        int hour = localHour;
        if (hour < 0) {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            hour = local.tm_hour;
        }
        return (hour >= 6 && hour < 18) == (cond == 24);
    }
    case 28: {
        for (const Cr3Fighter& o : fighters)
            if (o.player != f.player && o.hp > c) return false;
        return true;
    }
    case 29: return f.hp + total <= 0 && hpAtLeast(c);
    default: return false;
    }
}

std::vector<std::pair<int, int>> Cr3BattleRules::triggerSkills(int fi, int trigger, int attackKind, bool downed, int cmd, int total, int other) {
    std::vector<std::pair<int, int>> added;
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size() || trigger < 0) return added;
    const std::vector<int> skills = fighters[size_t(fi)].skills; // (a copy: a given effect may not touch it, but be safe)
    MbeFile::Row r;
    // (BtlSkillList::Trigger 0x9bb224 -> BtlSkill::Trigger 0x9c60a0: the trigger's skills - +0x1c - with their three uses not
    // 0, the condition holding and the chance - the trigger's chances visitor, +0x2c / +0x40 of 256 - rolled (here after the
    // other tests, as CR3-U always drew it), each use above 0 one less, enabled. Trigger 0's skills - no condition, chance
    // 256, uses -1 - count as enabled from the start: enabledRow)
    if (trigger > 0)
        for (int id : skills) {
            if (!tables_->skill.find("Skill", id, kSkill, r) || r.i(7) != trigger) continue;
            Cr3Fighter& f = fighters[size_t(fi)];
            std::array<int, 3>& uses = skillUsesOf(f, id, r);
            if (uses[0] == 0 || uses[1] == 0 || uses[2] == 0) continue;
            if (!conditionHolds(f, r.i(8), r.i(9), attackKind, downed, -1, cmd, total, other)) continue;
            const bool adv = advantage(f.player);
            const int chance = r.i(adv ? 16 : 11);
            if (chance < 256 && !(int(random() * 256.0f) < chance)) continue;
            for (int& u : uses)
                if (u > 0) --u;
            f.skillsOn.insert(id);
            for (const auto& [kind, value] : skillEffects(id, adv))
                if ((kind >= 97 && kind <= 101) || kind == 126) added.emplace_back(kind, value);
        }
    // (then BtlSkillVisitor 0x9b8cc4 over the enabled skills, the gauge's advantage's pair: every kind 60 ->
    // TakeStatusEffectToCalculation(id, GetTurn) - a held one an accumulation more, BtlStatusEffectList::Add 0x9b75c8 -, then
    // every kind 61 -> Remove; each trigger again, the no-trigger skills' always: a lost one comes back)
    for (const int kindNow : {60, 61})
        for (int id : skills) {
            const Cr3Fighter& f = fighters[size_t(fi)];
            if (!tables_->skill.find("Skill", id, kSkill, r) || !enabledRow(f, id, r)) continue;
            for (const auto& [kind, value] : skillEffects(id, advantage(f.player))) {
                if (kind != kindNow) continue;
                if (kind == 61) removeEffect(fi, value);
                else if (!effectInvalid(fighters[size_t(fi)], value)) giveEffect(fi, value, false, true);
            }
        }
    return added;
}

int Cr3BattleRules::commandChange(const Cr3Fighter& f) const {
    if (!tables_) return -1;
    std::vector<std::pair<int, int>> slots; // (group, id: StatusEffectList's fixed array, by StatusEffect +0x04)
    MbeFile::Row r;
    for (const auto& [id, turns] : f.effects)
        if (!f.effectsDormant.count(id) && tables_->skill.find("StatusEffect", id, kStatusEffect, r)) slots.emplace_back(r.i(1), id);
    std::stable_sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [group, id] : slots)
        if (tables_->skill.find("StatusEffect", id, kStatusEffect, r))
            for (size_t k = 0; k < 4; ++k)
                if (r.i(14 + k * 2) == 120 && r.i(15 + k * 2) >= 0) return r.i(15 + k * 2);
    return -1;
}

std::vector<std::pair<int, int>> Cr3BattleRules::opening() {
    std::vector<std::pair<int, int>> out;
    for (const int trigger : {1, 2}) {
        for (size_t k = 0; k < fighters.size(); ++k) {
            if (fighters[k].dead()) continue;
            for (const auto& [kind, id] : triggerSkills(int(k), trigger))
                if (kind == 100 && !(trigger == 2 && std::any_of(out.begin(), out.end(), [&](const auto& e) { return e.second == id; })))
                    out.emplace_back(int(k), id);
        }
        for (size_t k = 0; k < fighters.size(); ++k) endEffects(int(k), trigger); // (AllEndEffectToCalculation)
    }
    return out;
}

void Cr3BattleRules::endEffects(int fi, int timing) {
    if (!tables_ || fi < 0 || size_t(fi) >= fighters.size() || timing == 0) return;
    Cr3Fighter& f = fighters[size_t(fi)];
    MbeFile::Row r;
    // (BtlStatus::EndEffectToCalculation 0x9b5190 ends the skill list too: BtlSkill::End 0x9c6504 - enabled cleared where the
    // row's +0x28 is the timing)
    for (auto it = f.skillsOn.begin(); it != f.skillsOn.end();)
        it = tables_->skill.find("Skill", *it, kSkill, r) && r.i(10) == timing ? f.skillsOn.erase(it) : std::next(it);
    std::vector<int> gone;
    for (const auto& [id, turns] : f.effects) {
        if (!tables_->skill.find("StatusEffect", id, kStatusEffectFull, r) || r.i(11) != timing) continue;
        if (!(r.i(32) & 8)) f.effectsDormant.insert(id); // (+0x8c bit 3: kept enabled)
        const auto u = f.effectUses.find(id);
        if ((u != f.effectUses.end() && u->second == 0) || turns == 0) gone.push_back(id);
    }
    for (int id : gone) removeEffect(fi, id);
}

void Cr3BattleRules::giveFieldEffects() {
    for (size_t k = 0; k < fighters.size(); ++k) {
        const Cr3Fighter& f = fighters[k];
        if (!f.player || f.dead()) continue;
        for (int id : fieldEffects) {
            if (effectInvalid(f, id)) continue;
            // (a skill of kind 0x6a - with the gauge's advantage as it stands -: RemoveStatusEffectToCalculation)
            if (!skillConstants(f, 0x6a).empty()) {
                removeEffect(int(k), id);
                continue;
            }
            if (!hasEffect(int(k), id)) giveEffect(int(k), id, true); // (TakeStatusEffectToCalculation(id, -1): not stacked again)
        }
    }
}

float Cr3BattleRules::assistChance(int p) {
    return p < 10 ? 0.0f : p < 25 ? 0.1f : p < 50 ? 0.2f : p < 80 ? 0.3f : p < 100 ? 0.4f : 0.5f;
}

int Cr3BattleRules::lowHpPlayer(bool mp) const {
    // (the living players at 20 % HP / 10 % MP or under, shuffled and sorted by the rate - FUN_009c52ec with
    // FUN_009c40e8 -, the first; the shuffle - uniform_int, not the battle's random numbers - left out: the
    // lowest rate's first, ties the first collected)
    int best = -1;
    float bestRate = 2;
    for (size_t k = 0; k < fighters.size(); ++k) {
        const Cr3Fighter& f = fighters[k];
        if (!f.player || f.dead()) continue;
        const float rate = mp ? (f.maxMp > 0 ? float(f.mp) / float(f.maxMp) : 2.0f) : float(f.hp) / float(std::max(f.maxHp, 1));
        if (rate <= (mp ? 0.1f : 0.2f) && rate < bestRate) best = int(k), bestRate = rate;
    }
    return best;
}

bool Cr3BattleRules::lowAllPlayers(bool mp) const {
    for (const Cr3Fighter& f : fighters) {
        if (!f.player) continue;
        // (BtlBasicStatus::IsLowHp 0x9ba720 / IsLowMp 0x9ba740: the rate at or under 0.2 / 0.1 - the fallen at 0)
        const float rate = mp ? (f.maxMp > 0 ? float(f.mp) / float(f.maxMp) : 1.0f) : float(f.hp) / float(std::max(f.maxHp, 1));
        if (rate > (mp ? 0.1f : 0.2f)) return false;
    }
    return true;
}

int Cr3BattleRules::pickAssist(int* target, int* costOut) const {
    const int p = cheer;
    bool halved = false; // (HasEnableEffect(79) on a player's BtlStatusEffectList)
    for (const Cr3Fighter& f : fighters) halved |= f.player && hasKind(f, 79);
    const auto cost = [&](int c) { return halved ? c / 2 : c; };
    int dead = 0, firstDead = -1;
    for (size_t k = 0; k < fighters.size(); ++k)
        if (fighters[k].player && fighters[k].dead()) ++dead, firstDead = firstDead < 0 ? int(k) : firstDead;
    // (calculateRemoveStatusEffect: a player with one of the command's removes; calculateStatusEffect 0x7dcc40: a
    // player lacking one of its effects - both the halved cost or more points)
    const auto removable = [&](int id) {
        Cr3Command c;
        if (!command(id, c)) return false;
        for (size_t k = 0; k < fighters.size(); ++k)
            if (fighters[k].player && !fighters[k].dead())
                for (int e : c.removes)
                    if (hasEffect(int(k), e)) return true;
        return false;
    };
    const auto lacking = [&](int id) {
        Cr3Command c;
        if (!command(id, c)) return false;
        for (size_t k = 0; k < fighters.size(); ++k)
            if (fighters[k].player && !fighters[k].dead())
                for (int e : c.effects)
                    if (!hasEffect(int(k), e)) return true;
        return false;
    };
    struct Pick {
        int command = -1, target = -1, cost = 0;
    } pick;
    const int lowHp = lowHpPlayer(false), lowMp = lowHpPlayer(true);
    if (p > 49 && dead >= 2) pick = {61, -1, cost(50)};                                   // (AllResurrection 0x7da5d0)
    else if (p > 24 && firstDead >= 0) pick = {62, firstDead, cost(25)};                   // (Resurrection 0x7da650: GetDeadPlayer)
    else if (p > 9 && lowAllPlayers(false)) pick = {63, -1, cost(10)};                     // (AllHealHp 0x7da6d0)
    else if (p > 19 && lowAllPlayers(true)) pick = {64, -1, cost(20)};                     // (AllHealMp 0x7da74c)
    else if (p > 4 && lowHp >= 0) pick = {65, lowHp, cost(5)};                             // (HealHp 0x7da7c8)
    else if (p > 9 && lowMp >= 0) pick = {66, lowMp, cost(10)};                            // (HealMp 0x7da848)
    else if (cost(5) <= p && removable(0x43)) pick = {0x43, -1, cost(5)};                  // (RemovePoison 0x7da8c8)
    else if (cost(5) <= p && removable(0x45)) pick = {0x45, -1, cost(5)};                  // (RemoveDisableGene 0x7da8d4)
    else if (cost(5) <= p && removable(0x46)) pick = {0x46, -1, cost(5)};                  // (RemoveBrain 0x7da8e0)
    else if (cost(20) <= p && lacking(0x47)) pick = {0x47, -1, cost(20)};                  // (Enhance 0x7da8ec)
    if (target) *target = pick.target;
    if (costOut) *costOut = pick.cost;
    return pick.command;
}

bool Cr3BattleRules::assistAfterTurn(Cr3Action& out) {
    if (noAssist || outcome() != 0) return false; // (HasVictory / HasDefeat, MbCouplingInfo +0x44 bit 6)
    const float chance = assistChance(cheer);
    if (chance <= 0 || random() > chance) return false; // (CrxRandom::GenerateReal() <= the chance)
    int target = -1, cost = 0;
    const int cmd = pickAssist(&target, &cost);
    if (cmd < 0) return false;
    cheer = std::max(cheer - cost, 0); // (the points spent; CrxGameActivities::UseAssist: a log alone)
    out = resolveSourceless(cmd, target);
    out.assist = true;
    return !out.targets.empty();
}

Cr3Action Cr3BattleRules::resolveSourceless(int id, int target) {
    Cr3Action a;
    a.source = -1, a.command = id;
    Cr3Command c;
    if (!command(id, c)) return a;
    std::vector<int> targets;
    if (target >= 0 && size_t(target) < fighters.size()) {
        if (targetable(target, &c)) targets.push_back(target);
    } else if (!c.all) { // (one - battle 52's 1694 .., target kind 10 with none set: a living player's, as getCommandTarget picks)
        if (const int t = livingTarget(true, -1, &c); t >= 0) targets.push_back(t);
    } else
        for (size_t k = 0; k < fighters.size(); ++k)
            if (fighters[k].player && targetable(int(k), &c)) targets.push_back(int(k)); // (10 / 11: the player's side)
    for (int ti : targets) {
        Cr3Fighter& t = fighters[size_t(ti)];
        Cr3Hit hit;
        hit.target = ti;
        if (c.damage == 16 || c.damage == 17) {
            const int m = c.damage == 16 ? std::max(c.power, 0) : std::max(int(float(t.maxMp) * float(c.power) / 100.0f), 0);
            hit.hits.push_back(0), hit.mpHits.push_back(-m), hit.flags.push_back(0), hit.colors.push_back(4);
            t.mp = std::min(t.mp + m, t.maxMp);
        } else if (c.damage == 18) { // (calculatePercentHpMpHeal: 72 Special Support's - HP and MP, a revive's too)
            const int h = std::max(int(float(t.maxHp) * float(c.power) / 100.0f), 1);
            const int m = std::max(int(float(t.maxMp) * float(c.power) / 100.0f), 0);
            hit.hits.push_back(-h), hit.mpHits.push_back(-m), hit.flags.push_back(0), hit.colors.push_back(3);
            t.hp = std::min(std::max(t.hp, 0) + h, t.maxHp);
            t.mp = std::min(t.mp + m, t.maxMp);
        } else if (c.damage == 5 || c.damage == 6 || c.damage == 7) {
            const int h = c.damage == 7 ? std::max(int(float(t.maxHp) * float(c.power) / 100.0f), 1) : std::max(c.power, 1);
            hit.hits.push_back(-h), hit.mpHits.push_back(0), hit.flags.push_back(0), hit.colors.push_back(3);
            t.hp = std::min(std::max(t.hp, 0) + h, t.maxHp);
        }
        for (int e : c.removes) removeEffect(ti, e);
        if (!t.dead())
            for (int e : c.effects)
                if (effectLands(t, t, c, e)) giveEffect(ti, e), hit.effectsGiven.push_back(e);
        hit.hpAfter = t.hp, hit.mpAfter = t.mp, hit.killed = t.dead();
        a.targets.push_back(hit);
    }
    a.gaugeAfter = gauge;
    if (!a.targets.empty()) a.target = a.targets[0].target, a.hits = a.targets[0].hits, a.hpAfter = a.targets[0].hpAfter, a.killed = a.targets[0].killed;
    snapshotAfter(a);
    return a;
}

bool Cr3BattleRules::skillEnabled(const Cr3Fighter& f, int kind, int* most) const {
    bool on = false;
    for (int value : skillConstants(f, kind)) {
        if (most) *most = on ? std::max(*most, value) : value;
        on = true;
    }
    return on;
}

bool Cr3BattleRules::defeated(const Cr3Fighter& f) const { return f.dead() || (hasKind(f, 33) && !hasKind(f, 128)); }

// (BtlStatusList::HasVictory 0x9c2ae0 / HasDefeat: every unit of a side IsDefeat - an escaped Mimic, status effect 51's
// kinds 72 + 33 but not 76, is out: the battle won)
int Cr3BattleRules::outcome() const {
    bool party = false, monsters = false;
    for (const Cr3Fighter& f : fighters)
        if (!defeated(f)) (f.player ? party : monsters) = true;
    return !monsters ? 1 : !party ? 2 : 0;
}

} // namespace cr3
