#version 450
// PC only (28 Nov 2026): what the cinematic glow and the sun rays of the look pass (grade.frag) are made from, at
// half size, with mipmaps (gs_gpu.c). rgb: the part of each pixel brighter than the threshold (a soft knee, so the
// glow does not switch on and off), the open sky much less, so a bright noon sky does not wash the picture out;
// energy (beams, blasts, auras, a Super Saiyan's hair) is what glows. a: how much light the open sky sends there (0
// where something stands in front of it), the source of the sun rays.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D src;      // the copy of the picture, with mipmaps
layout(set = 2, binding = 1) uniform sampler2D depthTex; // the target's depth (GS depth / its maximum)
layout(set = 3, binding = 0) uniform Params {
    vec4 a; // x: threshold, y: the sky's share of the glow, z: the depth buffer's maximum / 65536, w: unused
} p;

void main() {
    vec3 c = textureLod(src, vUv, 1.0).rgb; // (level 1: this target's own size, the 2 x 2 average)
    float sky = 1.0 - clamp(texture(depthTex, vUv).r * p.a.z, 0.0, 1.0) > 0.9999 ? 1.0 : 0.0;
    float m = max(c.r, max(c.g, c.b));
    const float knee = 0.12;
    float s = clamp(m - p.a.x + knee, 0.0, 2.0 * knee);
    s = max(s * s / (4.0 * knee), m - p.a.x) / max(m, 1e-4);
    vec3 glow = c * s * mix(1.0, p.a.y, sky);
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    outColor = vec4(glow, sky * smoothstep(0.35, 0.85, l));
}
