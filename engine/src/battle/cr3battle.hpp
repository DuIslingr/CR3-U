#pragma once
// CR3's battle (Btl*: docs/FORMATS.md "Battles (CR3)"): who stands where and the cameras, from
// the parameter tables (assets/mbe.hpp). BtlSystem::Init: the party (createParty: the party's
// characters, GameMain's unit models), the coupling's monsters (CreateMonster: mon_cop's three
// slots, each an id picked from its list), then BtlUnitList::Align - each side along x in the
// order made, side by side by their widths (model.mbe's, x the scale), centred on 0; the party
// at z = Battle +0 / 10 + its half depth turned to face -z, the monsters at Battle +4 / 10 - theirs
// facing +z (BtlUnit::initializeHome). The field: battle_field.mbe's figure, its animation looped
// (BtlField::Load).

#include <array>
#include <string>
#include <vector>

#include "assets/mbe.hpp"
#include "assets/mvgl.hpp"
#include "assets/geom.hpp"
#include "assets/skeleton.hpp"

namespace cr3 {

// the battle's tables (data\*.mbe), read once
struct Cr3BattleTables {
    MbeFile model, monster, coupling, field, battle, character, command, direction;
    bool load(const MvglArchive& archive, std::string* error = nullptr);
};

struct Cr3BattleUnit {
    int model = 0;          // model.mbe's id
    std::string name;       // its name (m234, j006)
    std::string figure;     // the figure drawn: the name, a parts model's body <name>b<costume>
    std::string head;       // (a parts model's head, <name>h01; empty: none)
    std::string variant;    // the texture variation (the monster's: "a" the figure's own)
    bool player = false;    // (the party's side: BtlUnit +0xe4)
    int character = -1, monster = -1;
    float width = 0, depth = 0, scale = 1; // (model.mbe +0x18, +0x1c / 10; +0x24 x the unit's)
    int cameraSize = 0;     // (model.mbe +0x20: MbCameraSize)
    std::array<float, 3> home{};
    float yaw = 0;          // (0: facing +z - the monsters -; pi the party)
};

struct Cr3Battle {
    int coupling = -1, fieldId = -1;
    int couplingFlags = 0;             // (mon_cop +0x44: bit 0 - a script of its own)
    // BtlScriptSystem::Initialize: battle_<coupling> where the coupling has its own, else battle_0000
    std::string script() const;
    std::string field;                 // battle_field.mbe's figure (d1251b)
    std::array<float, 4> fieldColour{}; // (its +0x10 .. +0x1c)
    std::vector<Cr3BattleUnit> units;   // the party, then the monsters
    int cameraSize = -1;               // the largest unit's (BtlCameraSystem::ChangeLargerCameraSize)
    std::array<std::string, 10> cameras; // model.mbe Camera's names for that size: 0 the get-set, 1 command, ...
    float getSetEnd = 4.16667f;        // (its +0x04: PlayBattleIn's end time)
    // party: the characters in the party's slots (Cr3System.SetParty: char_para ids, 0 none)
    bool setup(const Cr3BattleTables& t, int coupling, int fieldId, const std::vector<int>& party, uint32_t seed,
               std::string* error = nullptr);
};

// BtlCameraMotion: a camera figure (Cam_bt: every battle camera a joint of it, all animated by
// its one animation from 0) - the joint named, its position x the scale and its rotation, the
// field of view of the camera the figure holds on it
class Cr3BattleCamera {
public:
    bool load(const MvglArchive& archive, const std::string& figure);
    bool has(const std::string& joint) const;
    // false: no such camera
    bool at(const std::string& joint, float t, Mat34& world, float& fovY, float& zn, float& zf) const;
    float duration() const { return animated_ ? anim_.duration() : 0.0f; }

private:
    Geom geom_;
    Skeleton skel_;
    Animation anim_;
    std::vector<std::string> names_;
    bool animated_ = false, loaded_ = false;
};

} // namespace cr3
