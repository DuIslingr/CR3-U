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
    std::printf("camera: %d problems\n", problems);
    return problems ? 1 : 0;
}
