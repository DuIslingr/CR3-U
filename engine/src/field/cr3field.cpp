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
    // (Fld2TaskMap::CreateArea's <area>c; CreateArea2D's - the e00 maps, an ortho camera's - <area>
    // itself: its defaultPos, its walls)
    const std::string c = a.contains(area + "c.skel") ? area + "c" : area;
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
    // a random area (Fld2TaskMap::CreateArea: d06's and d18's maps - CreateAreaRandom): <area>c's
    // joints d0600partNN_MM (a last x / y / z: mirrored on that axis) each place a piece - its
    // <piece>c.phys, moved to the joint (CollisionObject::SetPosition / SetRotation, the mirror its
    // shape's scaling), and <piece>f and <piece>s drawn there -, with <area>f at the origin
    if (area.compare(0, 3, "d06") == 0 || area.compare(0, 3, "d18") == 0) {
        const std::string kit = area.substr(0, 3) + "00";
        // (a joint's piece and place: its name less "_xx" and the mirror's letter; the place <area>c's
        // joint of that name - Cr3GetPositionFromFigure / Cr3GetRotationFromFigure -, the origin if none)
        auto pieceOf = [&](std::string n, Mat34& place) {
            float scale[3] = {1, 1, 1};
            const char last = n.back();
            if (last == 'x' || last == 'y' || last == 'z') scale[last - 'x'] = -1, n.pop_back();
            Mat34 at;
            for (size_t j = 0; j < names.size() && j < world.size(); ++j)
                if (names[j] == n || names[j] == n + last) at = world[j];
            for (int r = 0; r < 3; ++r) { // (its position and rotation alone)
                const float l = std::sqrt(at.m[r] * at.m[r] + at.m[4 + r] * at.m[4 + r] + at.m[8 + r] * at.m[8 + r]);
                if (l > 1e-6f) at.m[r] /= l, at.m[4 + r] /= l, at.m[8 + r] /= l;
            }
            Mat34 mirror;
            mirror.m[0] = scale[0], mirror.m[5] = scale[1], mirror.m[10] = scale[2];
            place = at * mirror;
            return n.size() > 3 ? n.substr(0, n.size() - 3) : std::string();
        };
        figures.push_back(area + "f"), places.push_back(Mat34());
        std::vector<std::string> fNames; // (the pieces drawn: <area>f's joints - Fld2TaskMap +0x110)
        if (a.read(area + "f.name", d)) fNames = parseJointNames(d);
        for (const std::string& n : fNames) {
            if (n.find(kit) == std::string::npos) continue;
            Mat34 place;
            const std::string piece = pieceOf(n, place);
            for (const char* suffix : {"f", "s"})
                if (!piece.empty() && a.contains(piece + suffix + ".geom")) figures.push_back(piece + suffix), places.push_back(place);
        }
        for (size_t j = 0; j < names.size() && j < world.size(); ++j) {
            if (names[j].find(kit) == std::string::npos) continue;
            Mat34 place;
            const std::string piece = pieceOf(names[j], place);
            PhysScene ps;
            if (!a.read(piece + "c.phys", d) || !parsePhys(d, ps)) continue;
            for (PhysShape& sh : ps.shapes) { // (baked: the joint's place, mirrored)
                for (Vec3& v : sh.vertices) {
                    const Vec3 u = v;
                    for (int r = 0; r < 3; ++r) v[size_t(r)] = place.m[r * 4] * u[0] + place.m[r * 4 + 1] * u[1] + place.m[r * 4 + 2] * u[2] + place.m[r * 4 + 3];
                }
                if (sh.kind == PhysShape::Box) { // (a box: its 12 triangles, placed)
                    const Vec3 h = sh.halfExtents;
                    sh.kind = PhysShape::Mesh;
                    for (int c = 0; c < 8; ++c) {
                        const Vec3 u = {c & 1 ? h[0] : -h[0], c & 2 ? h[1] : -h[1], c & 4 ? h[2] : -h[2]};
                        Vec3 v;
                        for (int r = 0; r < 3; ++r) v[size_t(r)] = place.m[r * 4] * u[0] + place.m[r * 4 + 1] * u[1] + place.m[r * 4 + 2] * u[2] + place.m[r * 4 + 3];
                        sh.vertices.push_back(v);
                    }
                    static const uint32_t kFaces[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5}, {0, 4, 5}, {0, 5, 1},
                                                           {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
                    for (const auto& f : kFaces) sh.triangles.push_back({f[0], f[1], f[2]});
                    sh.surfaces.assign(12, sh.surfaces.empty() ? 0 : sh.surfaces[0]);
                }
            }
            for (PhysBody& b : ps.bodies) b.position = {0, 0, 0}, b.rotation = {0, 0, 0, 1};
            PhysScene solid = ps, camera = ps;
            solid.bodies.clear(), camera.bodies.clear();
            for (const PhysBody& b : ps.bodies) (b.name.find("camhit") != std::string::npos ? camera : solid).bodies.push_back(b);
            walls.addScene(solid);
            cameraWalls.addScene(camera);
        }
        return true;
    }
    for (const char* suffix : {"f", "s", "m1"})
        if (a.contains(area + suffix + ".geom")) figures.push_back(area + suffix), places.push_back(Mat34());
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
    if (external_) return;
    if (lookDown_) { // (Fld2CameraLookDown::Reset: at once where they go)
        eye = player + V3{ldOffset_[0], ldOffset_[1], ldOffset_[2]}, at = player + V3{0, 1.2f, 0};
        return;
    }
    // (SetupCamera: the spring's point a unit behind the player, at the target's height)
    const V3 t = targetOf(player);
    spring_ = t - V3{std::sin(playerYaw), 0, std::cos(playerYaw)};
    speed_ = 0, yaw = 0, swinging_ = false;
    at = t;
    step(player, playerYaw, false, 0);
}

void Cr3GomCamera::turn(float dyaw, float dpitch) {
    if (lookDown_ || external_) return;
    yaw += dyaw;
    pitch = std::clamp(pitch + dpitch, 0.05f, 1.3f);
}

void Cr3GomCamera::step(const V3& player, float playerYaw, bool moving, float dt, const CollisionWorld* cameraWalls, const CollisionWorld* walls) {
    if (external_) return playerAlpha = 1, void();
    if (lookDown_) { // (Fld2CameraLookDown::Update)
        auto follow = [dt](V3& at, const V3& want) {
            const V3 d = want - at;
            const float l = len(d);
            if (l >= 0.3f) at = at + d * (std::min(4.0f * dt, l) / l);
        };
        follow(eye, player + V3{ldOffset_[0], ldOffset_[1], ldOffset_[2]});
        follow(at, player + V3{0, 1.2f, 0});
        playerAlpha = 1;
        return;
    }
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
    // CheckCollision: the first wall from the target to the camera stops it (HitTestNearest; mask
    // 0x80 - camhit - where there is some within 20 below the target, else 0x180 - the walls too)
    playerAlpha = 1;
    if (cameraWalls) {
        CollisionWorld::Hit below;
        const bool camOnly = cameraWalls->sweep(t, {t[0], t[1] - 20, t[2]}, 0.01f, {0, 1, 0}, -1.0f, below);
        const V3 back = t - eye;
        float f = 1;
        for (const CollisionWorld* w : {cameraWalls, camOnly ? nullptr : walls}) {
            CollisionWorld::Hit h;
            if (w && w->sweep(t, eye, 0.01f, back, -1.0f, h)) f = std::min(f, h.fraction);
        }
        if (f < 1) {
            eye = t + (eye - t) * f;
            const float d = std::hypot(eye[0] - t[0], eye[2] - t[2]);
            if (d < 2) eye[1] += 2 - d; // (raised as it comes in)
            if (d < 1) playerAlpha = 0.3f;
        }
    }
}

const char* cr3MotionName(Cr3Motion m) {
    switch (m) {
    case Cr3Motion::Run: return "ff01";
    case Cr3Motion::Walk: return "ff02";
    default: return "fn01";
    }
}

} // namespace cr3
