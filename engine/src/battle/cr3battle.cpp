#include "battle/cr3battle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cr3 {

namespace {

// the rows' layouts (assets/mbe.hpp; the 64-bit structs: MbModelInfo, MbMonsterInfo, ...)
constexpr const char* kModel = "iippffiffii";      // id, -, name, sub, width, depth, camera size, scale, -, -, flags (bit 0: parts)
constexpr const char* kCamera = "ifpppppppppp";    // size, the get-set's end, ten camera names
constexpr const char* kMonster = "iip";            // id, model, texture variation
constexpr const char* kCoupling = "i_ip_ip_ipiiii"; // id, three slots (count, the ids), the dead direction, -, -, flags
constexpr const char* kField = "ipffff";           // id, figure, ...
constexpr const char* kBattle = "ff";              // the party's line (x 1/10), the monsters'
constexpr const char* kChar = "ii";                // id, model
// MbParamAnim and MbParamAnimDirection are keyed by both model and motion. The pointers
// move between the 32-bit Vita and the 64-bit releases, while the column layout stays the same.
constexpr const char* kAnimTiming = "iifffffff_ipf"; // cue frame (8), marker count (9), float markers (10), next motion frame (11)
constexpr const char* kAnimDirectionTiming = "iippiif"; // fallback event group count (5) and frame interval (6)
constexpr const char* kCommandTiming = "iiiifii_piiiiifffffffiifff"; // projectile and hit parameters through size spread (+0x68)
constexpr const char* kEffectTiming = "i_pppiffiff"; // effect hit groups (7), first hit frame (8), frame interval (9)
constexpr float kBattleFrame = 1.0f / 30.0f;

bool readTable(const MvglArchive& a, const char* file, MbeFile& out, std::string* error) {
    std::vector<uint8_t> d;
    std::string err;
    if (!a.read(std::string("data\\") + file, d, &err) || !out.parse(d, &err)) {
        if (error) *error = std::string(file) + ": " + err;
        return false;
    }
    return true;
}

bool animationRow(const MbeFile& data, const char* table, int modelId, int motion, const char* layout, MbeFile::Row& row) {
    const MbeFile::Table* source = data.table(table);
    if (!source) return false;
    MbeFile::Row key;
    for (size_t i = 0; i < source->rows.size(); ++i)
        if (data.row(*source, i, "ii", key) && key.i(0) == modelId && key.i(1) == motion)
            return data.row(*source, i, layout, row);
    return false;
}

// BtlEventTimer::AddEvents: distribute the remaining hits across the remaining time groups.
// If there are fewer hits than groups, each occupied group has one hit.
void appendEventGroups(std::vector<float>& times, size_t count, int groups, float first, float interval) {
    while (count > 0) {
        const size_t inGroup = std::max(groups > 0 ? count / size_t(groups) : 0, size_t(1));
        times.insert(times.end(), inGroup, first);
        count -= inGroup;
        --groups;
        first += interval;
    }
}

std::vector<float> animationEvents(const MbeFile::Row& anim, const MbeFile& model, size_t count, int groups, float intervalFrames) {
    std::vector<float> times;
    const std::string& markers = anim.bytes[10];
    const size_t authored = std::min(count, size_t(std::max(anim.i(9), 0)));
    if (authored > markers.size() / sizeof(float)) return {};
    times.reserve(count);
    for (size_t i = 0; i < authored; ++i) {
        float frame;
        std::memcpy(&frame, markers.data() + i * sizeof frame, sizeof frame);
        if (!std::isfinite(frame)) return {};
        times.push_back(std::max(frame, 0.0f) * kBattleFrame);
    }
    if (authored == count) return times;
    if (groups < 0) {
        MbeFile::Row fallback;
        if (!animationRow(model, "AnimDirection", anim.i(0), anim.i(1), kAnimDirectionTiming, fallback)) return {};
        groups = fallback.i(5);
        intervalFrames = fallback.f(6);
    }
    if (groups <= 0 || !std::isfinite(anim.f(8)) || !std::isfinite(intervalFrames)) return {};
    appendEventGroups(times, count - authored, groups, std::max(anim.f(8), 0.0f) * kBattleFrame,
                      std::max(intervalFrames, 0.0f) * kBattleFrame);
    // The original timer waits for the earlier entries before consuming a later one. Preserve
    // marker order even when a fallback cue's frame predates the last authored marker.
    for (size_t i = 1; i < times.size(); ++i) times[i] = std::max(times[i], times[i - 1]);
    return times;
}

} // namespace

bool Cr3BattleTables::load(const MvglArchive& a, std::string* error) {
    readTable(a, "battle_anim.mbe", battleAnim, nullptr);
    readTable(a, "effect.mbe", effect, nullptr);
    readTable(a, "locator.mbe", locator, nullptr);
    return readTable(a, "model.mbe", model, error) && readTable(a, "mon_para.mbe", monster, error) &&
           readTable(a, "mon_cop.mbe", coupling, error) && readTable(a, "battle_field.mbe", field, error) &&
           readTable(a, "battle.mbe", battle, error) && readTable(a, "char_para.mbe", character, error) &&
           readTable(a, "command_para.mbe", command, error) && readTable(a, "direction.mbe", direction, error) &&
           readTable(a, "gene_para.mbe", gene, error) && readTable(a, "item_para.mbe", item, error) &&
           readTable(a, "skill_para.mbe", skill, error) && (readTable(a, "attachment_para.mbe", attachment, nullptr), true);
}

float Cr3BattleTables::animationCueTime(int modelId, int motion) const {
    MbeFile::Row anim;
    if (!animationRow(battleAnim, "Anim", modelId, motion, kAnimTiming, anim) || !std::isfinite(anim.f(8)) || anim.f(8) < 0) return -1;
    return anim.f(8) * kBattleFrame;
}

bool Cr3BattleTables::motionTravel(int modelId, int motion, MotionTravel& out) const {
    MbeFile::Row anim;
    if (!animationRow(battleAnim, "Anim", modelId, motion, "iifffffff_ipfff", anim)) return false;
    const int turn = motion == 14 ? 12 : 6; // bo04 uses the return rotation window.
    out = {anim.f(4) * kBattleFrame, anim.f(5) * kBattleFrame, anim.f(turn) * kBattleFrame, anim.f(turn + 1) * kBattleFrame};
    return out.stepStart >= 0 && out.stepEnd >= out.stepStart && std::isfinite(out.stepEnd) &&
           out.turnStart >= 0 && out.turnEnd >= out.turnStart && std::isfinite(out.turnEnd);
}

int Cr3BattleTables::directionCommand(int commandId, int basicAttack) const {
    MbeFile::Row commandRow;
    // BtlDirectionManager::CreateDirection substitutes the attack direction for kind 4.
    return direction.find("CommandDirection", commandId, "ii", commandRow) && commandRow.i(1) == 4 ? basicAttack : commandId;
}

std::string Cr3BattleTables::victoryCamera(int modelId) const {
    MbeFile::Row anim;
    // BtlUnit::PlayVictoryCamera: MbModelInfo::GetAnimInfo(modelId, 2) -> camera name at +0x08.
    return animationRow(model, "AnimDirection", modelId, 2, "iip", anim) ? anim.s(2) : std::string();
}

std::string Cr3BattleTables::unitCamera(int modelId, int motion, float* whiteFrame) const {
    MbeFile::Row anim;
    if (whiteFrame) *whiteFrame = -1;
    if (!animationRow(model, "AnimDirection", modelId, motion, "iippiiff", anim)) return std::string();
    if (whiteFrame && std::isfinite(anim.f(7))) *whiteFrame = anim.f(7);
    return anim.s(2);
}

Cr3BattleTables::EffectPoint Cr3BattleTables::effectPoint(int modelId, int point) const {
    if (point < 0 || point >= 8) return {};
    std::string layout = "i_pfffpfff"; // the damage and camera points precede EP0
    for (int i = 0; i < point; ++i) layout += "pfffpfff";
    layout += "pfff";
    MbeFile::Row row;
    if (!locator.find("Locator", modelId, layout.c_str(), row)) return {};
    const size_t col = size_t(9 + point * 8);
    return {row.s(col), {row.f(col + 1) * 0.1f, row.f(col + 2) * 0.1f, row.f(col + 3) * 0.1f}};
}

Cr3BattleTables::ActionTiming Cr3BattleTables::actionTiming(int modelId, int commandId, size_t hitCount, int repeats, int element) const {
    if (hitCount == 0) return {};
    MbeFile::Row commandRow;
    if (!direction.find("CommandDirection", commandId, kCommandTiming, commandRow)) return {};
    const int kind = commandRow.i(1);
    const bool melee = kind == 1;
    const bool ranged = kind == 2 || kind == 3;
    const bool cast = (kind >= 5 && kind <= 8) || (kind >= 32 && kind <= 36);
    const bool groupedEffect = melee || kind == 3 || cast || kind == 10 || kind == 21 || kind == 38;
    const int motion = melee ? 11 : ranged ? 15 : commandRow.i(2);
    if (motion <= 0 || motion >= 32) return {};

    MbeFile::Row effectRow;
    const bool timedEffect = effect.find("Effect", commandRow.i(9), kEffectTiming, effectRow) &&
                             effectRow.i(7) > 0 && std::isfinite(effectRow.f(8)) && std::isfinite(effectRow.f(9));
    // Native effects with no table row notify damage immediately. The visual effect timer adds
    // its own delay after the animation timer dispatches it (BtlVisualEffect::initializeEventTimer).
    const float effectDelay = timedEffect ? std::max(effectRow.f(8), 0.0f) * kBattleFrame : 0.0f;
    const float effectInterval = timedEffect ? std::max(effectRow.f(9), 0.0f) * kBattleFrame : 0.0f;
    const int effectGroups = timedEffect ? effectRow.i(7) : 1;
    const size_t swings = melee || ranged ? std::min({size_t(std::max(repeats, 1)), hitCount, size_t(3)}) : 1;
    ActionTiming timing;
    timing.hitEffect = commandRow.i(9);
    timing.targetPoint = commandRow.i(11);
    timing.waitEffect = cast;
    const uint32_t flags = uint32_t(commandRow.i(20));
    timing.scaleToTarget = (flags & 4) != 0;
    if (flags & 0x80) timing.sizeSpread = commandRow.f(24);
    if (kind == 7 || kind == 34) timing.placement = ActionTiming::Placement::Group;
    if (kind == 8 || kind == 35) timing.placement = ActionTiming::Placement::Field;
    const bool shot = kind == 3 || kind == 6 || kind == 33;
    const int bulletCount = commandRow.i(6);
    if (shot && bulletCount > 0) {
        Projectile& p = timing.projectile;
        p.effect = commandRow.element(7, size_t(std::max(element, 0) % bulletCount));
        p.sourcePoint = commandRow.i(10);
        p.targetPoint = commandRow.i(11);
        p.speed = commandRow.f(13), p.acceleration = commandRow.f(14);
        p.spreadX = {commandRow.f(15), commandRow.f(16)};
        p.spreadY = {commandRow.f(17), commandRow.f(18)};
        p.waypoint = commandRow.f(19);
        // BtlBullet integrates normalized distance. A flight ends at distance 1.
        const float discriminant = p.speed * p.speed + 2 * p.acceleration;
        if (p.effect > 0 && p.speed >= 0 && discriminant >= 0)
            p.duration = 2.0f / (p.speed + std::sqrt(discriminant));
        if (p.duration > 0 && p.duration < 10) timing.targetPoint = commandRow.i(12);
        else p = Projectile();
    }
    const bool scheduleEffect = timing.projectile.duration > 0 || kind == 5 || kind == 7 || kind == 8 ||
                                kind == 32 || kind == 34 || kind == 35;
    if (kind >= 5 && kind <= 8) {
        timing.receiverCamera = commandRow.i(3);
        std::string layout = "iippiifi";
        for (int slot = 0; slot < 5; ++slot) layout += "ipffii";
        MbeFile::Row motionFx;
        if (animationRow(model, "AnimDirection", modelId, motion, layout.c_str(), motionFx)) {
            timing.castCamera = motionFx.s(2);
            timing.castSound = motionFx.s(3);
            for (size_t col = 8; col < 38; col += 6) {
                const float first = motionFx.f(col + 2), last = motionFx.f(col + 3);
                const int id = motionFx.element(col + 1, 0);
                if (motionFx.i(col) > 0 && id > 0 && std::isfinite(first) && std::isfinite(last) && first >= 0)
                    timing.castEffects.push_back({id, motionFx.i(col + 4), first * kBattleFrame,
                                                  last * kBattleFrame, (motionFx.i(col + 5) & 1) != 0});
            }
            std::stable_sort(timing.castEffects.begin(), timing.castEffects.end(),
                             [](const auto& a, const auto& b) { return a.start < b.start; });
        }
    }
    size_t hitsLeft = hitCount;
    float start = 0;
    for (size_t swing = 0; swing < swings; ++swing) {
        const int motionType = motion + int(swing);
        MbeFile::Row anim;
        if (!animationRow(battleAnim, "Anim", modelId, motionType, kAnimTiming, anim)) return {};
        if (kind >= 5 && kind <= 8) {
            const float change = anim.f(11);
            timing.spellHandoff = std::isfinite(change) ? std::max(change, 0.0f) * kBattleFrame : 0;
        }
        const size_t damageCount = hitsLeft / (swings - swing);
        const size_t dispatches = groupedEffect ? (melee || ranged ? 1 : std::min(size_t(std::max(repeats, 1)), damageCount)) : damageCount;
        const auto cues = animationEvents(anim, model, dispatches, groupedEffect ? int(dispatches) : -1, commandRow.f(4));
        if (cues.empty()) return {};
        timing.motions.push_back({motionType, start});
        if (groupedEffect) {
            size_t damageLeft = damageCount;
            for (size_t i = 0; i < cues.size(); ++i) {
                const size_t perEffect = damageLeft / (cues.size() - i);
                const float release = start + cues[i];
                const float arrival = release + timing.projectile.duration;
                if (timing.projectile.duration > 0) timing.effects.push_back({release, true});
                if (scheduleEffect && timing.hitEffect > 0) timing.effects.push_back({arrival, false});
                appendEventGroups(timing.hits, perEffect, effectGroups, arrival + effectDelay, effectInterval);
                damageLeft -= perEffect;
            }
        } else {
            for (float cue : cues) timing.hits.push_back(start + cue + effectDelay);
        }
        hitsLeft -= damageCount;
        // The melee and ranged directions chain on the previous clip's change notification.
        // Do not drop a pending impact when a clip provides an earlier change cue.
        if (swing + 1 < swings && !std::isfinite(anim.f(11))) return {};
        start += std::max(cues.back(), anim.f(11) * kBattleFrame);
    }
    for (size_t i = 1; i < timing.hits.size(); ++i) timing.hits[i] = std::max(timing.hits[i], timing.hits[i - 1]);
    std::stable_sort(timing.effects.begin(), timing.effects.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return timing;
}

bool Cr3BattleTables::attachmentOf(int item, Attachment& out) const {
    MbeFile::Row r;
    if (item <= 0 || !attachment.find("Attachment", item, "iiiiiii", r)) return false;
    out = Attachment{r.i(0), r.i(1), r.i(2), r.i(3), r.i(4), r.i(5), r.i(6)};
    return true;
}

int Cr3BattleTables::defaultArmour(int ch) const {
    MbeFile::Row r;
    if (!character.find("Char", ch, "iiiiii", r)) return 0;
    if (!(r.i(3) & 1) && ch == 1) return 83; // (PartyEquip(1): j001's - the row's armour 1, b01, is the Black Jacket)
    return r.i(5);
}

std::string Cr3BattleTables::body(const std::string& model, int armour) const {
    MbeFile::Row r;
    int number = 1;
    if (armour > 0 && item.find("Armor", armour, "iiiiiiiiiiiii", r) && r.i(12) > 0) number = r.i(12);
    char name[40];
    std::snprintf(name, sizeof name, "%sb%02d", model.c_str(), number);
    return name;
}

int Cr3BattleTables::weaponOf(int ch, const std::map<int, int>* worn) const {
    MbeFile::Row r;
    if (!character.find("Char", ch, "iiiiii", r)) return 0;
    const auto it = worn ? worn->find(ch) : std::map<int, int>::const_iterator();
    return !(r.i(3) & 1) && worn && it != worn->end() ? it->second : r.i(4);
}

bool Cr3BattleTables::weaponModel(int weapon, std::string& figure, std::string& variant) const {
    MbeFile::Row w, m;
    if (weapon <= 0 || !item.find("Weapon", weapon, "iiiiiiiiiiiii", w) || w.i(12) <= 0 || !model.find("AttachmentModel", w.i(12), "ipp", m))
        return false;
    figure = m.s(1), variant = m.s(2);
    return !figure.empty();
}

bool Cr3Battle::setup(const Cr3BattleTables& t, int couplingId, int fieldNo, const std::vector<int>& party, uint32_t seed, std::string* error,
                      const std::map<int, int>* armours, const std::map<int, int>* weapons) {
    *this = Cr3Battle();
    coupling = couplingId, fieldId = fieldNo;
    MbeFile::Row r;
    if (t.field.find("BattleField", fieldNo, kField, r)) {
        field = r.s(1);
        for (int k = 0; k < 4; ++k) fieldColour[size_t(k)] = r.f(size_t(2 + k));
    }
    float partyLine = 2, monsterLine = -2; // (MbBattleInfo +0, +4: 20, -20)
    if (t.battle.table("Battle") && t.battle.row(*t.battle.table("Battle"), 0, kBattle, r)) partyLine = r.f(0) * 0.1f, monsterLine = r.f(1) * 0.1f;
    fieldSpan = partyLine - monsterLine;
    int armour = 0; // (the party member being added's)
    auto addUnit = [&](int modelId, bool player) -> Cr3BattleUnit* {
        MbeFile::Row m;
        if (!t.model.find("Model", modelId, kModel, m)) return nullptr;
        Cr3BattleUnit u;
        u.model = modelId, u.name = m.s(2), u.player = player;
        u.width = m.f(4) * 0.1f, u.depth = m.f(5) * 0.1f, u.cameraSize = m.i(6), u.scale = m.f(7);
        u.effectScale = m.f(8), u.modelFlags = m.i(10);
        // CrxModel::Build: a parts model is two figures, <name>b<costume> (FUN_009467ac's "%sb%02d";
        // b01 without a costume) and <name>h01
        if (u.modelFlags & 1) u.figure = t.body(u.name, armour), u.head = u.name + "h01";
        else u.figure = u.name;
        units.push_back(u);
        return &units.back();
    };
    MbeFile::Row cp;
    if (!t.coupling.find("Coupling", couplingId, kCoupling, cp)) {
        if (error) *error = "coupling " + std::to_string(couplingId) + ": not in mon_cop";
        return false;
    }
    couplingFlags = cp.i(10);
    // createParty: the party's first three (BtlBootParam), or solo Char 7 when IsBootCharacter (+0x44 bit 3)
    const std::vector<int> bootParty{7};
    const std::vector<int>& useParty = (couplingFlags & 8) ? bootParty : party;
    for (size_t k = 0; k < useParty.size() && k < 3; ++k) {
        if (useParty[k] <= 0) continue;
        MbeFile::Row c;
        if (!t.character.find("Char", useParty[k], kChar, c)) continue;
        const auto set = armours ? armours->find(useParty[k]) : std::map<int, int>::const_iterator();
        armour = armours && set != armours->end() ? set->second : t.defaultArmour(useParty[k]);
        if (Cr3BattleUnit* u = addUnit(c.i(1), true)) {
            u->character = useParty[k];
            if (!t.weaponModel(t.weaponOf(useParty[k], weapons), u->weapon, u->weaponVariant)) u->weapon.clear();
        }
        armour = 0;
    }
    // CreateMonster: each slot with an id (MbCouplingInfo::GetMonsterId: one of its list at random)
    uint32_t rng = seed ? seed : 1;
    for (size_t s = 0; s < 3; ++s) {
        const int count = cp.i(1 + s * 2);
        if (count <= 0 || cp.element(2 + s * 2, 0) == -1) continue;
        rng = rng * 1103515245u + 12345u;
        const int id = cp.element(2 + s * 2, size_t((rng >> 16) % uint32_t(count)));
        MbeFile::Row mon;
        if (!t.monster.find("Monster", id, kMonster, mon)) continue;
        if (Cr3BattleUnit* u = addUnit(mon.i(1), false)) u->monster = id, u->variant = mon.s(2);
    }
    // BtlUnitList::Align: each side side by side along x, centred; BtlUnit::initializeHome: its line
    for (const bool side : {true, false}) {
        float x = 0, total = 0;
        const Cr3BattleUnit* prev = nullptr;
        for (Cr3BattleUnit& u : units) {
            if (u.player != side) continue;
            x = (prev ? prev->home[0] + prev->width * prev->scale * 0.5f : 0.0f) + u.width * u.scale * 0.5f;
            u.home[0] = x;
            total += u.width * u.scale;
            prev = &u;
        }
        for (Cr3BattleUnit& u : units) {
            if (u.player != side) continue;
            u.home[0] -= total * 0.5f;
            u.home[2] = side ? partyLine + u.depth * 0.5f : monsterLine - u.depth * 0.5f;
            u.yaw = side ? 3.14159265f : 0.0f;
        }
    }
    // BtlUnitList::ChangeLagerCameraSize: the largest unit's camera size, its cameras' names
    for (const Cr3BattleUnit& u : units) cameraSize = std::max(cameraSize, u.cameraSize);
    MbeFile::Row cam;
    if (t.model.find("Camera", cameraSize, kCamera, cam)) {
        getSetEnd = cam.f(1);
        for (size_t k = 0; k < 10; ++k) cameras[k] = cam.s(2 + k);
    }
    if (units.empty()) {
        if (error) *error = "coupling " + std::to_string(couplingId) + ": no units";
        return false;
    }
    return true;
}

std::string Cr3Battle::script() const {
    char name[32];
    std::snprintf(name, sizeof name, "battle_%04d", (couplingFlags & 1) ? coupling : 0);
    return name;
}

bool Cr3BattleCamera::load(const MvglArchive& a, const std::string& figure) {
    *this = Cr3BattleCamera();
    std::string base = figure;
    for (char& c : base) c = char(std::tolower(static_cast<unsigned char>(c))); // (the archive's names are lower case)
    std::vector<uint8_t> d;
    if (!a.read(base + ".geom", d) || !parseGeom(d, geom_) || !a.read(base + ".skel", d) || !parseSkeleton(d, skel_)) return false;
    if (a.read(base + ".name", d)) names_ = parseJointNames(d);
    animated_ = a.read(base + ".anim", d) && anim_.parse(d) && anim_.joints() == int(skel_.basePose.size());
    loaded_ = !geom_.cameras.empty();
    return loaded_;
}

bool Cr3BattleCamera::has(const std::string& joint) const {
    return loaded_ && std::find(names_.begin(), names_.end(), joint) != names_.end();
}

bool Cr3BattleCamera::at(const std::string& joint, float t, Mat34& world, float& fovY, float& zn, float& zf) const {
    if (!loaded_) return false;
    const auto it = std::find(names_.begin(), names_.end(), joint);
    if (it == names_.end()) return false;
    const size_t j = size_t(it - names_.begin());
    // (Figure::GetContaindCamera: the camera the figure holds on that joint)
    const GeomCamera* cam = nullptr;
    for (const GeomCamera& c : geom_.cameras)
        if (j < skel_.nameHashes.size() && c.hash == skel_.nameHashes[j]) cam = &c;
    std::vector<JointTransform> pose = skel_.basePose;
    std::vector<float> user;
    if (animated_) {
        const float frames = float(std::max(anim_.frames() - 1, 1));
        pose = anim_.poseAbsolute(skel_.basePose, std::clamp(t * anim_.sampleRate(), 0.0f, frames), &user);
    }
    const std::vector<Mat34> w = skel_.world(pose);
    if (j >= w.size()) return false;
    world = w[j];
    fovY = cam ? cam->fovY : 45;
    if (cam) // (BtlCameraMotion::GetFieldOfView: its animated value where it has one)
        for (size_t k = 0; k < skel_.userChannels.size() && k < user.size(); ++k)
            if (skel_.userChannels[k].material == cam->hash && user[k] > 0.5f && user[k] < 179) fovY = user[k];
    zn = cam ? std::max(cam->near, 0.05f) : 0.1f, zf = cam ? std::min(cam->far * 5, 5000.0f) : 1000.0f;
    zf = std::max(zf, zn * 2);
    return true;
}

} // namespace cr3
