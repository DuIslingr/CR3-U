#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>
#endif

namespace mobile {
// Screen fractions, so the same gesture works with letterboxing and high-DPI windows.
struct MenuSwipe {
    bool down = false, moved = false;
    int context = 0;
    float x0 = 0, y0 = 0, lastY = 0, remainder = 0;
    void begin(float x, float y, int menu) {
        *this = MenuSwipe();
        down = true, context = menu, x0 = x, y0 = lastY = y;
    }
    int move(float x, float y) {
        if (!down) return 0;
        moved |= std::hypot(x - x0, y - y0) >= 0.012f;
        if (!moved) return 0;
        remainder += lastY - y;
        lastY = y;
        const int rows = int(remainder / 0.1f);
        remainder -= rows * 0.1f;
        return rows;
    }
};
// LuckyBoxMenu's swipe (TouchSimplePress 0xad1ca4 / TouchSimpleMove 0xad1dc4 / TouchSimpleRelease 0xad1d04), in the touch's
// units: InterfaceMain::TouchSingleEvent 0x890a84 passes (touch - centre) in InputEquipment's size - GameApp::onSize's
// DisplayInfo +0x30 / +0x34, 1920 by 1920 x H / W (its +0x38 1920, +0x3c 0) -, y up, times W / (1.5 H) on screen type 2
// (GameSystem +0x13c outside 16 .. 22): y +-640 on any screen, x +-640 x W / H - two to a pixel of the 640-high layout.
// A press inside x -760 .. 760, y -320 .. 240 starts it (a move with none: there); past 20 from where it started, a move
// inside arms it; let go within 0.5 s (+0xec, counted while pressed) more than 80 across and more across than up or down:
// -1 (left: the next box - state 1) or 1 (right: state 2), else 0
struct BoxSwipe {
    bool pressed = false, moved = false, armed = false;
    float x0 = 0, y0 = 0, held = 0;
    static bool inside(float x, float y) { return x >= -760 && x <= 760 && y <= 240.00002f && y >= -320; }
    // (screen fractions, y down, and the screen's width over its height: the touch's units)
    static void units(float fx, float fy, float aspect, float& x, float& y) { x = (fx - 0.5f) * 1280.0f * aspect, y = (0.5f - fy) * 1280.0f; }
    void press(float x, float y) {
        if (inside(x, y)) pressed = true, moved = false, armed = false, x0 = x, y0 = y, held = 0;
    }
    void move(float x, float y) {
        if (!pressed) pressed = true, moved = false, armed = false, x0 = x, y0 = y, held = 0;
        if (!moved) {
            if (std::hypot(x - x0, y - y0) < 20) return;
            moved = true;
        }
        armed |= inside(x, y);
    }
    void step(float dt) {
        if (pressed) held += dt;
    }
    int release(float x, float y) {
        const bool go = armed && held < 0.5f;
        const float dx = x - x0, dy = y - y0;
        *this = BoxSwipe();
        if (!go || std::abs(dx) <= 80 || std::abs(dy) >= std::abs(dx)) return 0;
        return dx < 0 ? -1 : 1;
    }
};
inline int scrollCursor(int cursor, int count, int rows) {
    return count > 0 ? std::clamp(cursor + rows, 0, count - 1) : 0;
}
inline void orientation(SDL_Window* window, bool locked, bool& flipped, bool capture = false) {
    if (capture && window) {
        const auto o = SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(window));
        if (o == SDL_ORIENTATION_LANDSCAPE || o == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
            flipped = o == SDL_ORIENTATION_LANDSCAPE_FLIPPED;
    }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, !locked ? "LandscapeLeft LandscapeRight" : flipped ? "LandscapeRight" : "LandscapeLeft");
#ifdef SDL_PLATFORM_ANDROID
    if (!window) return;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return;
    auto cls = env->GetObjectClass(activity);
    auto method = cls ? env->GetMethodID(cls, "setOrientationLock", "(ZZ)V") : nullptr;
    if (method) env->CallVoidMethod(activity, method, jboolean(locked), jboolean(flipped));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not apply the orientation lock");
    }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#endif
}
}
