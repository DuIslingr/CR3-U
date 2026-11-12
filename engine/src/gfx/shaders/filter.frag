#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// The bloom's filters (MVGL's filterCopyFP / filterBrightFP / filterBlurFP): texMode 0 copy,
// 1 bright (colour x 2 where r + g + b exceeds color.z - bloomLimit), 2 blur (five taps, x color.w -
// bloomScale). Alpha 1: the original's targets are RGB565 - each pass's output clamped and rounded
// to 5 / 6 / 5 bits as that write would (Vulkan's images are RGBA8)
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vTex[5];
layout(location = 0) out vec4 fragColor;
void main() {
    vec4 c;
    if (pc.texMode == 1) {
        c = texture(uTex, vTex[2]) * 2.0;
        c *= float(c.x + c.y + c.z > pc.color.z);
    } else if (pc.texMode == 2) {
        c = 0.05448868 * texture(uTex, vTex[0]);
        c += 0.24420134 * texture(uTex, vTex[1]);
        c += 0.40261995 * texture(uTex, vTex[2]);
        c += 0.24420134 * texture(uTex, vTex[3]);
        c += 0.05448868 * texture(uTex, vTex[4]);
        c *= pc.color.w;
    } else {
        c = texture(uTex, vTex[2]);
    }
    const vec3 bits = vec3(31.0, 63.0, 31.0);
    fragColor = vec4(floor(clamp(c.rgb, 0.0, 1.0) * bits + 0.5) / bits, 1.0);
}
