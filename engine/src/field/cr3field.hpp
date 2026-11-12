#pragma once
// CR3's field (Fld2: docs/FORMATS.md "Fields (CR3)"). An area (Fld2TaskMap::CreateArea) is four
// figures named after it: <area>c - not drawn; its lights, its joints (defaultPos ...) and its
// <area>c.phys, the walls and floors (bodies named "camhit..." the camera's alone) -, <area>f the
// field itself, <area>s over it 0.01 up (<area>m1: a post effect mask, not drawn). The player (Fld2TaskPlayer) is a
// Fld2CharaNameTbl model - j001 .. j005, c101 - walking (1.2 a second) or running (4.8) as a
// sphere of 0.2 (Fld2CharaCtrl: step 0.1, slopes up to 45 degrees); the camera follows it
// (Fld2CameraGom).

#include <array>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "field/collision.hpp"

namespace cr3 {

struct Cr3Area {
    std::string name;                 // "d0101"
    std::vector<std::string> figures; // drawn, in order: <area>f, <area>s (those it has; not m1 - a post effect's mask)
    std::vector<Mat34> places;        // each figure's (a random area's pieces: at their joints)
    std::array<float, 3> spawn{};     // <area>c's defaultPos
    float spawnYaw = 0;               // (its rotation about y: the way the player faces)
    CollisionWorld walls;             // the player's (<area>c.phys but its camhit bodies)
    CollisionWorld cameraWalls;       // the camera's (its camhit bodies)
    bool load(const MvglArchive& archive, const std::string& area, std::string* error = nullptr);
};

// Fld2CameraGom, a step a 1/60 s: it looks at the player 1.2 up; a spring keeps it 3 away (an
// acceleration of 0.005 a step, braking to stop there), 0.5 above that point and never nearer
// than 2 across; the view pitched down 0.3 rad (and at least 3 sin 0.3 up) and turned by yaw.
// Moving bakes yaw into the spring's point (+0x30 -> +0x20, +0x14). While a touch drag moves the
// player across the view (GetIsPress(9), 30 .. 150 degrees between camera and move direction), it
// swings round toward behind them (GomRotate: (1 - angle / 180) x 1.5 x 60 degrees a second)
class Cr3GomCamera {
public:
    void reset(const std::array<float, 3>& player, float playerYaw);
    // (with walls: CheckCollision - the camera stopped at the first it meets from the target; the
    // camera's own bodies (camhit) alone where there are some below the target, else the walls too)
    void step(const std::array<float, 3>& player, float moveAngle, bool moving, float dt, const CollisionWorld* cameraWalls = nullptr,
              const CollisionWorld* walls = nullptr, bool touchMove = false);
    float playerAlpha = 1; // (the camera within 1 of the target across: the player drawn at 0.3)
    void turn(float yawRadians, float pitchRadians); // (the player's own turning of it)
    // Fld2CameraGom::UpdateControl: the camera pad's turn - the pitch kept within 20 degrees either
    // way (its +0x10), one already further out only brought back in
    void drag(float yawRadians, float pitchRadians);
    // GomBehind (Vita L): swing behind the player's facing in 0.3 seconds.
    void behind(float playerYaw);
    bool turnable() const { return !lookDown_ && !external_; } // (Fld2TaskCamera's type 1, Gom: the screen's camera half)
    // Fld2CameraLookDown (an area's camera type 3 - the executable's map table -: houses, the Space
    // Port): the eye at the player plus the area's offset (FieldCameraLD), looking at the player
    // 1.2 up; each followed at 4 a second once 0.3 or more off (a still zone), no turning
    void setLookDown(bool on, const std::array<float, 3>& offset = {}) { lookDown_ = on, ldOffset_ = offset; }
    bool lookDown() const { return lookDown_; }
    // another camera sets eye and at itself (Fld2CameraRail): reset, step and turn leave them
    void setExternal(bool on) { external_ = on; }
    // Fld2CameraGom::SetRideCamera (Fld2TaskPlayer::SetStateRide / SetRideStatus, flag 4105): the
    // target's lift (+0x40's +0x64) 1.6 riding, 1.2 on foot; the pad's pitch limit (+0x10) 24 / 20 degrees
    void setRide(bool on) { lift_ = on ? 1.6f : 1.2f, most_ = on ? 24.0f : 20.0f; }
    std::array<float, 3> eye{}, at{};
    float fovY = 45, zNear = 0.1f, zFar = 5000; // (MVGL::Draw::Camera's 45 degrees; Fld2CameraGom::Initialize's)
    float yaw = 0, pitch = 0.3f;

private:
    std::array<float, 3> spring_{}; // (the point the spring holds: +0x20)
    float speed_ = 0;
    bool baked_ = false, swinging_ = false; // (+0x14, +0x15)
    float behindAge_ = -1, behindYaw_ = 0, behindPitch_ = 0, behindDelta_ = 0;
    bool lookDown_ = false, external_ = false;
    std::array<float, 3> ldOffset_{};
    float lift_ = 1.2f, most_ = 20.0f;
};

// Fld2AnimeNameTbl: the player's motions
enum class Cr3Motion {
    Idle = 0, Walk = 2, Run = 3, Fall = 4,
    LadderOn = 6, LadderUp = 7, LadderOffTop = 8, LadderOnTop = 9, LadderDown = 10, LadderOffBottom = 11, LadderHold = 12,
    RideIdle = 0xd, RideWalk = 0xe, RideRun = 0xf
};
// fn01, ff01 (SM_Walk), ff02 (SM_Run), ff03 (SM_Fall); the ladder's fh01, fh03, fh04, fh05, fh06, fh07, fh02;
// riding fr01 .. fr03
const char* cr3MotionName(Cr3Motion m);

} // namespace cr3
