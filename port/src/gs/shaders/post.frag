#version 450
// Finishing pass over the whole picture as it goes to the window (`post` in the settings file): the kind of image
// treatment newer games add. Contrast-adaptive sharpening, a soft glow around bright parts, a little more colour
// (most where colour is weakest), a gentle S-curve and a light vignette. p.a.x blends the result over the original.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D img;
layout(set = 3, binding = 0) uniform Params {
    vec4 a; // x: amount 0..1, y: glow, z: vignette, w: sharpen
    vec4 b; // x: colour, y: contrast, z: glow threshold, w: unused
    vec4 c; // xy: the part of the buffer the picture fills (the buffer is larger than the 512 x 448 page)
} p;

vec3 at(vec2 uv) {
    return texture(img, clamp(uv, vec2(0.0), p.c.xy)).rgb;
}

void main() {
    vec2 tx = 1.0 / vec2(textureSize(img, 0));
    vec2 uv = vUv * p.c.xy;
    vec3 orig = at(uv);
    vec3 c = orig;

    // sharpen, weighted down where the neighbourhood already has strong contrast (so edges do not ring)
    vec3 n = at(uv - vec2(0.0, tx.y)), s = at(uv + vec2(0.0, tx.y));
    vec3 e = at(uv + vec2(tx.x, 0.0)), w = at(uv - vec2(tx.x, 0.0));
    vec3 mn = min(c, min(min(n, s), min(e, w)));
    vec3 mx = max(c, max(max(n, s), max(e, w)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-3)), 0.0, 1.0));
    vec3 k = -amp * (0.125 * p.a.w);
    c = clamp((c + (n + s + e + w) * k) / (1.0 + 4.0 * k), 0.0, 1.0);

    // glow: what is brighter than the threshold, gathered from two rings around the pixel
    vec3 glow = vec3(0.0);
    for (int i = 0; i < 12; i++) {
        float ang = float(i) * 0.5236 + (i < 6 ? 0.0 : 0.2618);
        float r = i < 6 ? 6.0 : 14.0;
        vec3 t = at(uv + vec2(cos(ang), sin(ang)) * tx * r * 1.5);
        glow += max(t - vec3(p.b.z), vec3(0.0));
    }
    c += glow / 12.0 * p.a.y * 2.0;

    // colour: more saturation where there is little of it already (vibrance)
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = mix(vec3(l), c, 1.0 + p.b.x * (1.0 - sat));

    // contrast: a smooth S-curve
    c = clamp(c, 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), p.b.y);

    // vignette
    vec2 q = vUv - 0.5; // (the window's corners)
    c *= 1.0 - p.a.z * dot(q, q) * 1.4;

    outColor = vec4(mix(orig, clamp(c, 0.0, 1.0), p.a.x), 1.0);
}
