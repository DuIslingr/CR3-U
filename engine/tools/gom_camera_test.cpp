#include "field/cr3field.hpp"
#include <cmath>
#include <cstdio>

int main() {
    cr3::Cr3GomCamera camera;
    const std::array<float, 3> player{0, 0, 0};
    constexpr float pi = 3.14159265f;
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("camera FAILED: %s\n", what); }
    };
    camera.reset(player, 0);
    camera.drag(pi - 0.1f, -0.2f);
    camera.step(player, 0, false, 0);
    const float before = camera.yaw;
    // Cross the +/- pi boundary: the swing must take the short route.
    camera.behind(-pi + 0.1f);
    camera.step(player, 0, false, 0.15f);
    check(std::fabs(camera.yaw - before) < 0.2f, "recentering crosses angle boundary by the short route");
    check(std::fabs(camera.pitch - 0.2f) < 0.0001f, "pitch interpolates rather than snapping");
    camera.step(player, 0, false, 0.15f);
    const float angle = std::atan2(camera.eye[0] - camera.at[0], camera.eye[2] - camera.at[2]);
    check(std::fabs(std::remainder(angle - 0.1f, 2 * pi)) < 0.0001f, "camera is behind the requested facing after 0.3 seconds");
    check(std::fabs(camera.pitch - 0.3f) < 0.0001f, "recenter restores default tilt");
    camera.behind(1);
    camera.drag(0.1f, 0);
    const float manual = camera.yaw;
    camera.step(player, 0, false, 0.3f);
    check(camera.yaw == manual, "manual camera input interrupts recentering");
    camera.setLookDown(true, {0, 5, 0});
    camera.reset(player, 0);
    camera.behind(1);
    camera.step(player, 0, false, 0.3f);
    check(camera.yaw == manual, "fixed cameras ignore recentering");

    // Vita stick / keyboard movement (touchMove == false): GomRotate is gated off (0x810c48c0), so
    // walking toward or across the camera lets the character move freely without spinning behind.
    camera.setLookDown(false);
    camera.reset(player, 0);
    for (int i = 0; i < 60; ++i) camera.step(player, 0, false, 1.0f / 60.0f); // settle spring at z = -3
    std::array<float, 3> p = player;
    for (int i = 0; i < 30; ++i) {
        p[2] -= 1.2f / 60.0f; // walk straight toward the camera (moveAngle = pi)
        camera.step(p, pi, true, 1.0f / 60.0f, nullptr, nullptr, false);
    }
    const float towardAngle = std::atan2(camera.eye[0] - camera.at[0], camera.eye[2] - camera.at[2]);
    check(std::fabs(std::remainder(towardAngle - pi, 2 * pi)) < 0.001f, "walking toward camera on stick does not flip camera behind");

    // Anchor baking (+0x14, +0x30 -> +0x20): rotating camera 180 degrees manually and walking away
    // from the new view must keep the camera on the new +Z side instead of collapsing onto old -Z spring.
    camera.reset(player, 0);
    for (int i = 0; i < 60; ++i) camera.step(player, 0, false, 1.0f / 60.0f);
    camera.drag(pi, 0);
    camera.step(player, 0, false, 1.0f / 60.0f); // camera now on +Z (azimuth 0)
    p = player;
    for (int i = 0; i < 60; ++i) {
        p[2] -= 4.8f / 60.0f; // run away from new camera toward -Z (moveAngle = pi)
        camera.step(p, pi, true, 1.0f / 60.0f, nullptr, nullptr, false);
    }
    const float bakedAngle = std::atan2(camera.eye[0] - camera.at[0], camera.eye[2] - camera.at[2]);
    check(std::fabs(std::remainder(bakedAngle, 2 * pi)) < 0.001f, "manual yaw bakes into spring without phantom orbiting on move");

    // Touch GomRotate (0x9f3284 / 0x810c48c0): walking toward camera (< 30 deg) does not rotate,
    // walking sideways (90 deg) swings toward behind and stops at 150 deg (30 deg from behind).
    camera.reset(player, 0);
    for (int i = 0; i < 60; ++i) camera.step(player, 0, false, 1.0f / 60.0f); // camera at -Z (pi)
    camera.step(player, pi, true, 0.2f, nullptr, nullptr, true); // toward camera: |pi - pi| = 0 < 30
    const float touchToward = std::atan2(camera.eye[0] - camera.at[0], camera.eye[2] - camera.at[2]);
    check(std::fabs(std::remainder(touchToward - pi, 2 * pi)) < 0.001f, "touch walk toward camera (< 30 deg) leaves azimuth still");

    for (int i = 0; i < 180; ++i) // sideways along +X (moveAngle = +pi/2, behind = -pi/2), stationary target to isolate GomRotate
        camera.step(player, 0.5f * pi, true, 1.0f / 60.0f, nullptr, nullptr, true);
    const float touchSide = std::atan2(camera.eye[0] - camera.at[0], camera.eye[2] - camera.at[2]);
    const float sepDeg = std::fabs(std::remainder(touchSide - 0.5f * pi, 2 * pi)) * 180.0f / pi;
    check(sepDeg > 149.5f && sepDeg < 153.0f, "touch sideways swing decelerates and stops ~30 degrees from behind");
    check(camera.yaw == 0.0f, "stopping GomRotate bakes rotated spring and zeroes yaw");

    std::printf("camera: %d problems\n", problems);
    return problems ? 1 : 0;
}
