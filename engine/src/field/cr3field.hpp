#pragma once
// CR3's field (Fld2: docs/FORMATS.md "Fields (CR3)"). An area (Fld2TaskMap::CreateArea) is four
// figures named after it: <area>c - not drawn; its lights, its joints (defaultPos ...) and its
// <area>c.phys, the walls and floors (bodies named "camhit..." the camera's alone) -, <area>f the
// field itself, <area>s over it 0.01 up and <area>m1. The player (Fld2TaskPlayer) is a
// Fld2CharaNameTbl model - j001 .. j005, c101 - walking (1.2 a second) or running (4.8) as a
// sphere of 0.2 (Fld2CharaCtrl: step 0.1, slopes up to 45 degrees); the camera follows it
// (Fld2CameraGom).

#include <array>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "field/collision.hpp"

namespace cr3 {

struct Cr3Area {
    std::string name;                 // "d0101"
    std::vector<std::string> figures; // drawn, in order: <area>f, <area>s, <area>m1 (those it has)
    std::array<float, 3> spawn{};     // <area>c's defaultPos
    float spawnYaw = 0;               // (its rotation about y: the way the player faces)
    CollisionWorld walls;             // the player's (<area>c.phys but its camhit bodies)
    CollisionWorld cameraWalls;       // the camera's (its camhit bodies)
    bool load(const MvglArchive& archive, const std::string& area, std::string* error = nullptr);
};

// Fld2CameraGom, a step a 1/60 s: it looks at the player 1.2 up; a spring keeps it 3 away (an
// acceleration of 0.005 a step, braking to stop there), 0.5 above that point and never nearer
// than 2 across; the view pitched down 0.3 rad (and at least 3 sin 0.3 up) and turned by yaw.
// While the player moves across the view (30 .. 150 degrees from facing away), it swings round
// behind them (GomRotate: (1 - angle / 180) x 1.5 x 60 degrees a second)
class Cr3GomCamera {
public:
    void reset(const std::array<float, 3>& player, float playerYaw);
    // (with walls: CheckCollision - the camera stopped at the first it meets from the target; the
    // camera's own bodies (camhit) alone where there are some below the target, else the walls too)
    void step(const std::array<float, 3>& player, float playerYaw, bool moving, float dt, const CollisionWorld* cameraWalls = nullptr,
              const CollisionWorld* walls = nullptr);
    float playerAlpha = 1; // (the camera within 1 of the target across: the player drawn at 0.3)
    void turn(float yawRadians, float pitchRadians); // (the player's own turning of it)
    // Fld2CameraLookDown (an area's camera type 3 - the executable's map table -: houses, the Space
    // Port): the eye at the player plus the area's offset (FieldCameraLD), looking at the player
    // 1.2 up; each followed at 4 a second once 0.3 or more off (a still zone), no turning
    void setLookDown(bool on, const std::array<float, 3>& offset = {}) { lookDown_ = on, ldOffset_ = offset; }
    bool lookDown() const { return lookDown_; }
    // another camera sets eye and at itself (Fld2CameraRail): reset, step and turn leave them
    void setExternal(bool on) { external_ = on; }
    std::array<float, 3> eye{}, at{};
    float fovY = 45, zNear = 0.1f, zFar = 5000; // (MVGL::Draw::Camera's 45 degrees; Fld2CameraGom::Initialize's)
    float yaw = 0, pitch = 0.3f;

private:
    std::array<float, 3> spring_{}; // (the point the spring holds: +0x20)
    float speed_ = 0;
    bool swinging_ = false;
    bool lookDown_ = false, external_ = false;
    std::array<float, 3> ldOffset_{};
};

// Fld2AnimeNameTbl: the player's motions
enum class Cr3Motion { Idle = 0, Run = 2, Walk = 3 };
const char* cr3MotionName(Cr3Motion m); // fn01, ff01, ff02

} // namespace cr3
