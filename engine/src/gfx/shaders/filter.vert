#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// The bloom's filters' quad (MVGL's filterCopyVP / filterBlurVP): the position its UV; color.xy
// blurOffset. Vulkan's clip y down and its images' row 0 the top agree, as GL's both bottom-up
layout(location = 0) out vec2 vTex[5];
void main() {
    const vec2 kPos[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const vec2 p = kPos[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);
    const vec2 uv = (p + 1.0) * 0.5, off = pc.color.xy;
    vTex[0] = uv - off * 2.0;
    vTex[1] = uv - off;
    vTex[2] = uv;
    vTex[3] = uv + off;
    vTex[4] = uv + off * 2.0;
}
