#include "battle/cr3battle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

bool readTable(const MvglArchive& a, const char* file, MbeFile& out, std::string* error) {
    std::vector<uint8_t> d;
    std::string err;
    if (!a.read(std::string("data\\") + file, d, &err) || !out.parse(d, &err)) {
        if (error) *error = std::string(file) + ": " + err;
        return false;
    }
    return true;
}

} // namespace

bool Cr3BattleTables::load(const MvglArchive& a, std::string* error) {
    return readTable(a, "model.mbe", model, error) && readTable(a, "mon_para.mbe", monster, error) &&
           readTable(a, "mon_cop.mbe", coupling, error) && readTable(a, "battle_field.mbe", field, error) &&
           readTable(a, "battle.mbe", battle, error) && readTable(a, "char_para.mbe", character, error) &&
           readTable(a, "command_para.mbe", command, error) && readTable(a, "direction.mbe", direction, error) &&
           readTable(a, "gene_para.mbe", gene, error) && readTable(a, "item_para.mbe", item, error) &&
           readTable(a, "skill_para.mbe", skill, error) && (readTable(a, "attachment_para.mbe", attachment, nullptr), true);
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
    int armour = 0; // (the party member being added's)
    auto addUnit = [&](int modelId, bool player) -> Cr3BattleUnit* {
        MbeFile::Row m;
        if (!t.model.find("Model", modelId, kModel, m)) return nullptr;
        Cr3BattleUnit u;
        u.model = modelId, u.name = m.s(2), u.player = player;
        u.width = m.f(4) * 0.1f, u.depth = m.f(5) * 0.1f, u.cameraSize = m.i(6), u.scale = m.f(7);
        // CrxModel::Build: a parts model is two figures, <name>b<costume> (FUN_009467ac's "%sb%02d";
        // b01 without a costume) and <name>h01
        if (m.i(10) & 1) u.figure = t.body(u.name, armour), u.head = u.name + "h01";
        else u.figure = u.name;
        units.push_back(u);
        return &units.back();
    };
    // createParty: the party's first three (BtlBootParam), its characters' models
    for (size_t k = 0; k < party.size() && k < 3; ++k) {
        if (party[k] <= 0) continue;
        MbeFile::Row c;
        if (!t.character.find("Char", party[k], kChar, c)) continue;
        const auto set = armours ? armours->find(party[k]) : std::map<int, int>::const_iterator();
        armour = armours && set != armours->end() ? set->second : t.defaultArmour(party[k]);
        if (Cr3BattleUnit* u = addUnit(c.i(1), true)) {
            u->character = party[k];
            if (!t.weaponModel(t.weaponOf(party[k], weapons), u->weapon, u->weaponVariant)) u->weapon.clear();
        }
        armour = 0;
    }
    // CreateMonster: each slot with an id (MbCouplingInfo::GetMonsterId: one of its list at random)
    MbeFile::Row cp;
    if (!t.coupling.find("Coupling", couplingId, kCoupling, cp)) {
        if (error) *error = "coupling " + std::to_string(couplingId) + ": not in mon_cop";
        return false;
    }
    couplingFlags = cp.i(10);
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
