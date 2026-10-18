#include "field/cr3field.hpp"

#include <algorithm>
#include <cmath>

#include "assets/skeleton.hpp"

namespace cr3 {

namespace {

using V3 = std::array<float, 3>;
V3 operator+(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 operator-(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 operator*(const V3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float len(const V3& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
constexpr float kDeg = 0.01745329f;

// the camera's target: the player 1.2 up (Fld2CameraGom::Initialize: +0x60 (0, 1.2, 0))
V3 targetOf(const V3& player) { return {player[0], player[1] + 1.2f, player[2]}; }

} // namespace

bool Cr3Area::load(const MvglArchive& a, const std::string& area, std::string* error) {
    *this = Cr3Area();
    name = area;
    const std::string c = area + "c";
    std::vector<uint8_t> d;
    Skeleton sk;
    if (!a.read(c + ".skel", d) || !parseSkeleton(d, sk)) {
        if (error) *error = c + ": not in the release";
        return false;
    }
    std::vector<std::string> names;
    if (a.read(c + ".name", d)) names = parseJointNames(d);
    const std::vector<Mat34> world = sk.world(sk.basePose);
    for (size_t j = 0; j < names.size() && j < world.size(); ++j)
        if (names[j] == "defaultPos") { // (Cr3GetPositionFromFigure / Cr3GetRotationFromFigure)
            const Mat34& m = world[j];
            spawn = {m.m[3], m.m[7], m.m[11]};
            spawnYaw = std::atan2(m.m[2], m.m[10]); // (its z axis, along the ground)
        }
    for (const char* suffix : {"f", "s", "m1"})
        if (a.contains(area + suffix + ".geom")) figures.push_back(area + suffix);
    PhysScene scene;
    std::string err;
    if (a.read(c + ".phys", d) && parsePhys(d, scene, &err)) {
        PhysScene solid = scene, camera = scene;
        solid.bodies.clear(), camera.bodies.clear();
        for (const PhysBody& b : scene.bodies) (b.name.find("camhit") != std::string::npos ? camera : solid).bodies.push_back(b);
        walls.addScene(solid);
        cameraWalls.addScene(camera);
    } else if (!err.empty()) {
        if (error) *error = c + ".phys: " + err;
        return false;
    }
    return true;
}

void Cr3GomCamera::reset(const V3& player, float playerYaw) {
    // (SetupCamera: the spring's point a unit behind the player, at the target's height)
    const V3 t = targetOf(player);
    spring_ = t - V3{std::sin(playerYaw), 0, std::cos(playerYaw)};
    speed_ = 0, yaw = 0, swinging_ = false;
    at = t;
    step(player, playerYaw, false, 0);
}

void Cr3GomCamera::turn(float dyaw, float dpitch) {
    yaw += dyaw;
    pitch = std::clamp(pitch + dpitch, 0.05f, 1.3f);
}

void Cr3GomCamera::step(const V3& player, float playerYaw, bool moving, float dt) {
    const V3 t = targetOf(player);
    const float frames = dt * 60; // (its rates are a 1/60 s step's)
    // GomAction: the spring to 3 away along where it is, accelerating 0.005 a step and braking to stop there
    V3 d = spring_ - t;
    float l = len(d);
    if (l < 1e-4f) d = {0, 0, -1}, l = 1;
    const V3 e = (t + d * (3.0f / l)) - spring_;
    const float dist = len(e);
    if (dist < 0.1f) speed_ = 0;
    const float accel = 0.005f;
    speed_ += speed_ * speed_ / (accel * 2) <= dist ? accel : -accel;
    if (speed_ <= 0) speed_ = 0;
    else {
        spring_ = spring_ + e * (speed_ * frames);
        spring_[1] = t[1] + 0.5f;
    }
    // (never nearer than 2 across)
    const float across = std::hypot(spring_[0] - t[0], spring_[2] - t[2]);
    if (across < 2 && across > 1e-4f) {
        const V3 out = spring_ - t;
        spring_ = spring_ + out * ((2 - across) / len(out));
    }
    // GomRotate: moving across the view, swing round behind the player
    const float cameraAngle = std::atan2(eye[0] - t[0], eye[2] - t[2]);
    const float behind = playerYaw + 3.14159265f; // (the direction behind the player)
    float a = std::remainder(cameraAngle - behind, 2 * 3.14159265f) / kDeg;
    const float mag = std::fabs(a);
    if (!moving || mag > 150) swinging_ = false;
    else if (mag >= 30 || swinging_) {
        swinging_ = true;
        const float rate = std::fabs((mag - 180) / 180) * 1.5f * 60; // (degrees a second)
        yaw -= std::copysign(std::min(rate * dt, mag), a) * kDeg;
    }
    // GomUpdate: the spring's direction across, turned by yaw, pitched down by pitch, at its distance
    const float dist3 = len(spring_ - t);
    const float base = std::atan2(spring_[0] - t[0], spring_[2] - t[2]) + yaw;
    const float across3 = dist3 * std::cos(pitch);
    eye = {t[0] + std::sin(base) * across3, t[1] + std::max(dist3 * std::sin(pitch), 3 * std::sin(pitch)), t[2] + std::cos(base) * across3};
    at = t;
}

const char* cr3MotionName(Cr3Motion m) {
    switch (m) {
    case Cr3Motion::Run: return "ff01";
    case Cr3Motion::Walk: return "ff02";
    default: return "fn01";
    }
}

} // namespace cr3
