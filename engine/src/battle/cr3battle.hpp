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
#include <map>
#include <string>
#include <vector>

#include "assets/mbe.hpp"
#include "assets/mvgl.hpp"
#include "assets/geom.hpp"
#include "assets/skeleton.hpp"

namespace cr3 {

// the battle's tables (data\*.mbe), read once
struct Cr3BattleTables {
    MbeFile model, monster, coupling, field, battle, character, command, direction, gene, item, skill;
    MbeFile attachment; // (attachment_para.mbe's Attachment: an alteration's item - MbItemInfo +0x20; empty where a release lacks it)
    MbeFile battleAnim, effect, locator;
    bool load(const MvglArchive& archive, std::string* error = nullptr);
    // The animation's authored cue, used for hit generation and the knockout fade. Returns -1 if absent.
    float animationCueTime(int modelId, int motion) const;
    struct MotionTravel {
        float stepStart, stepEnd, turnStart, turnEnd;
    };
    bool motionTravel(int modelId, int motion, MotionTravel& out) const;
    int directionCommand(int commandId, int basicAttack) const;
    struct EffectPoint {
        std::string joint;
        std::array<float, 3> offset{};
    };
    EffectPoint effectPoint(int modelId, int point) const;
    struct Projectile {
        int effect = -1, sourcePoint = 0, targetPoint = 0;
        float speed = 0, acceleration = 0, waypoint = 0;
        std::array<float, 2> spreadX{}, spreadY{};
        float duration = 0;
    };
    struct ActionTiming {
        struct Motion { int type; float time; };
        struct EffectCue { float time; bool launch; };
        enum class Placement { Target, Group, Field };
        struct CastEffect { int effect, point; float start, end; bool follow; };
        std::vector<Motion> motions;
        std::vector<float> hits;
        std::vector<EffectCue> effects;
        int hitEffect = -1, targetPoint = 0;
        bool waitEffect = false;
        bool scaleToTarget = false;
        float sizeSpread = 0;
        Placement placement = Placement::Target;
        Projectile projectile;
        float spellHandoff = -1;
        int receiverCamera = -1;
        std::string castCamera;
        std::string castSound;
        std::vector<CastEffect> castEffects;
    };
    // Source animation cues followed by any delay in the hit effect's own timer. Empty hits means
    // the direction or its timing data could not be resolved.
    ActionTiming actionTiming(int modelId, int directionCommand, size_t hitCount, int repeats, int element = 0) const;
    // an attachment's effect (MbItemInfo::GetConstant: the row's +0x0c category - 0 weight, 1 break
    // power, 2 .. 7 a weapon's element lowered (element - 1), 8 .. 13 an armour's element resisted
    // (element - 7), 14 .. 19 an accessory's HP, MP, attack, guard, speed, luck - and +0x10 its
    // amount), its +0x08 the equipment it fits (1 weapons, 2 armour, 4 accessories), +0x14 the
    // levels it gives and +0x18 its price in gold; false: not an attachment
    struct Attachment {
        int item = 0, group = 0, fits = 0, category = -1, amount = 0, levels = 0, gold = 0;
    };
    bool attachmentOf(int item, Attachment& out) const;
    // a character's armour (CrxCharacter: a guest's its char_para row's +0x14; anyone else's the
    // save's - where no script has put one on yet, the starting kit's: test_equip.i's PartyEquip,
    // which the row's are but for j001's armour 83 - body model 2, as the reference shows it -),
    // and the body it dresses a parts model in (CrxModel::GetBodyModelNo: the armour's +0x30 -
    // "%sb%02d", b01 without one)
    int defaultArmour(int character) const;
    std::string body(const std::string& model, int armour) const;
    // a character's weapon (a guest's - and anyone's with none set - its row's +0x10) and the
    // model it holds (CrxModel::LoadWeapon: the Weapon's +0x30 - model.mbe's AttachmentModel:
    // the figure, its texture variation - on the body's DummyWP joint)
    int weaponOf(int character, const std::map<int, int>* worn) const;
    bool weaponModel(int weapon, std::string& figure, std::string& variant) const;
};

struct Cr3BattleUnit {
    int model = 0;          // model.mbe's id
    std::string name;       // its name (m234, j006)
    std::string figure;     // the figure drawn: the name, a parts model's body <name>b<costume>
    std::string head;       // (a parts model's head, <name>h01; empty: none)
    std::string variant;    // the texture variation (the monster's: "a" the figure's own)
    std::string weapon, weaponVariant; // (a character's weapon's attachment model; empty: none)
    bool player = false;    // (the party's side: BtlUnit +0xe4)
    int character = -1, monster = -1;
    float width = 0, depth = 0, scale = 1; // (model.mbe +0x18, +0x1c / 10; +0x24 x the unit's)
    float effectScale = 1; // (Model row +0x28, before multiplying by the unit's scale)
    int modelFlags = 0;    // (Model row +0x30: bit 0 parts, bits 1 and 4 knockout presentation)
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
    float fieldSpan = 4.0f;           // (MbBattleInfo::GetFieldLength)
    // party: the characters in the party's slots (Cr3System.SetParty: char_para ids, 0 none);
    // armours, weapons: theirs where the game has set one (else Cr3BattleTables::defaultArmour, weaponOf)
    bool setup(const Cr3BattleTables& t, int coupling, int fieldId, const std::vector<int>& party, uint32_t seed,
               std::string* error = nullptr, const std::map<int, int>* armours = nullptr, const std::map<int, int>* weapons = nullptr);
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
