#version 450
// PC only: the look a script mod asks for (bt3.look; gs_draw.c grade_add, native effect 6), drawn over the fight's
// picture just before the HUD, so the HUD and the menus keep their own colours. In order:
//   - Ambient shadows: soft darkening where a point is ringed by nearer surfaces (corners, a rock's foot on the
//     ground), worked out from the depth buffer (screen-space ambient occlusion). The PS2's depth grows toward the
//     camera; 0 is the cleared far end (sky), left alone.
//   - Energy light: the frame's energy (every ki blast, beam and flash the game drew adding light, gs_draw.c
//     light_add) as lights: what is near one on the screen and in depth (the ground under a ki blast, a rock beside
//     it, a fighter) takes its colour and some of its light. The far background behind it does not.
//   - Balance: part of the scene's own colour cast taken out (its average, followed slowly: avg.frag), so a warm
//     stage does not go orange and the look stays neutral and clean, as in the anime.
//   - Vivid colours: livelier colours (the dull ones more than the strong; orange and skin tones less, so warm
//     stages and faces do not burn), a little more contrast, the shadows a touch cool.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D src;        // a copy of the picture as it is now, with mipmaps
layout(set = 2, binding = 1) uniform sampler2D depthTex;   // the target's depth (GS depth / its maximum)
layout(set = 2, binding = 2) uniform sampler2D balanceTex; // 1 x 1: the scene's average colour, followed slowly
layout(set = 2, binding = 3) uniform sampler2D brightTex;  // half size, mipmaps: what glows (rgb), the open sky's light (a)
layout(set = 3, binding = 0) uniform Params {
    vec4 g; // x: saturation, y: vibrance, z: contrast, w: strength of the grade 0..1
    vec4 h; // xyz: the shadows' tint, w: a GS pixel's width on the screen over its height (the picture's stretch)
    vec4 o; // x: ambient shadow strength 0..1, y: its radius in uv across, z: the same down, w: debug (1 shadow, 2 depth)
    vec4 q; // x: the depth buffer's maximum / 65536, y: balance strength, z: energy light strength, w: number of lights
    vec4 L[32]; // per light: (x, y, reach, depth) in GS pixels and the game's depth / 65536, then (r, g, b, strength)
    vec4 z0;    // atmosphere: x strength 0..1, y where it starts (far_of), zw the colour's r, g
    vec4 z1;    // x: the colour's b; y: cinematic glow strength, z: its source's base level, w: sun rays strength
    vec4 z2;    // xy: where the sun's light comes from (the buffer's uv), zw: the picture's part of the buffer
    vec4 z3;    // xyz: the sun's colour
} p;

vec3 glow_at(vec2 uv, float lod) { // a soft tent of four samples at a level of the glow's source
    vec2 t = 0.75 / vec2(textureSize(brightTex, int(lod)));
    return (textureLod(brightTex, uv + vec2(t.x, t.y), lod).rgb + textureLod(brightTex, uv + vec2(-t.x, t.y), lod).rgb +
            textureLod(brightTex, uv + vec2(t.x, -t.y), lod).rgb + textureLod(brightTex, uv - t, lod).rgb) * 0.25;
}

const vec3 kLum = vec3(0.2126, 0.7152, 0.0722);

float far_of(vec2 uv) { // distance from the camera, as a fraction of the game's depth range (0 = at the camera)
    return 1.0 - clamp(texture(depthTex, uv).r * p.q.x, 0.0, 1.0);
}

void main() {
    vec3 c0 = textureLod(src, vUv, 0.0).rgb, c = c0;
    float ao = 1.0;
    if (p.o.x > 0.0) {
        float here = far_of(vUv);
        if (here < 0.9999) { // (not the sky)
            // pairs of opposite samples: on a flat surface (a slope too) the two sides average to the middle and
            // nothing darkens; in a crease both sides come nearer than the middle. A pair with a big step in it
            // is another object's edge (a rock against the sky): left out, or every outline gets a dark halo.
            const vec2 kDisc[8] = vec2[](vec2(0.25, 0.05), vec2(-0.10, 0.40), vec2(0.55, -0.30), vec2(0.20, 0.70),
                                         vec2(-0.75, 0.25), vec2(0.90, 0.20), vec2(-0.30, -0.90), vec2(0.60, 0.75));
            float scale = clamp(0.05 / max(here, 0.002), 0.35, 2.5); // (nearer things fill more of the picture)
            float occ = 0.0;
            for (int i = 0; i < 8; i++) {
                vec2 off = kDisc[i] * p.o.yz * scale;
                float r1 = (here - far_of(vUv + off)) / max(here, 0.0005);
                float r2 = (here - far_of(vUv - off)) / max(here, 0.0005);
                // a crease: both sides nearer, about as much; an object's edge: one side only (sym near 0.5)
                float spread = max(abs(r1), abs(r2)), crease = (r1 + r2) * 0.5, sym = crease / max(spread, 1e-5);
                float valid = 1.0 - smoothstep(0.06, 0.15, spread);
                // one side only counts when it is very near in depth: something resting on or against the surface
                // (a rock's foot on the ground), not an object far in front of it
                float contact = 1.0 - smoothstep(0.008, 0.02, spread);
                occ += smoothstep(0.0015, 0.012, crease) * max(smoothstep(0.65, 0.9, sym), contact) * valid;
            }
            ao = 1.0 - p.o.x * clamp(occ / 8.0 * 1.8, 0.0, 1.0);
            // light drawn over the scenery (a beam, an aura, a flash) has the depth of what is behind it: no shadow
            // in it, or the shapes behind show through as grey smudges
            float glow = max(c0.r, max(c0.g, c0.b)) * 0.5 + dot(c0, kLum) * 0.5;
            ao = mix(ao, 1.0, smoothstep(0.6, 0.85, glow));
        }
        c *= ao;
    }
    if (p.o.w > 3.5) { // (debug 4: the energy light alone)
        vec3 b = textureLod(src, vUv, p.q.w).rgb * 0.55 + textureLod(src, vUv, p.q.w + 1.5).rgb * 0.45;
        outColor = vec4(b, 1.0);
        return;
    }
    if (p.o.w > 2.5) { // (debug 3: the scene's average as the balance reads it)
        outColor = vec4(texture(balanceTex, vec2(0.5)).rgb, 1.0);
        return;
    }
    if (p.o.w > 1.5) { // (debug 2: the depth itself, in bands: where its steps fall)
        float f = far_of(vUv);
        outColor = vec4(fract(f * 10.0), fract(f * 100.0), fract(f * 1000.0), 1.0);
        return;
    }
    if (p.o.w > 0.5) {
        outColor = vec4(vec3(ao), 1.0);
        return;
    }
    vec3 avg = texture(balanceTex, vec2(0.5)).rgb;
    float avgL = dot(avg, kLum);
    if (p.z0.x > 0.0) {
        // atmosphere (27 Nov 2026): what is far fades into the air's colour, more the farther it is; the fighters
        // and what is near stay clear, the sky (nothing drawn: far_of 1) keeps its own colours
        float f = far_of(vUv);
        if (f < 0.9999) {
            float t = smoothstep(p.z0.y, 1.0, f);
            float self = max(c.r, max(c.g, c.b));
            float k = p.z0.x * t * t * (1.0 - smoothstep(0.8, 0.98, self) * 0.7); // (energy keeps its own light)
            c = mix(c, vec3(p.z0.z, p.z0.w, p.z1.x), clamp(k, 0.0, 0.85));
        }
    }
    if (p.q.z > 0.0 && p.q.w > 0.0) {
        vec2 gs = vUv * vec2(textureSize(src, 0)) / vec2(textureSize(src, 0).x / 1024.0); // (the pixel in GS pixels)
        float here = far_of(vUv);
        vec3 light = vec3(0.0);
        int n = int(p.q.w);
        for (int i = 0; i < 16; i++) {
            if (i >= n) break;
            vec4 a = p.L[i * 2], col = p.L[i * 2 + 1];
            vec2 d = (gs - a.xy) * vec2(p.h.w, 1.0);
            float reach = a.z * 2.4 + 24.0;                        // (a pool round it, larger than the blast)
            float fall = exp(-dot(d, d) / (reach * reach) * 2.2);
            float there = 1.0 - a.w;                               // (its distance as far_of gives it)
            float dz = abs(here - there) / max(there, 0.002);
            float near = 1.0 - smoothstep(0.08, 0.45, dz);         // (the ground under it, not the far background)
            light += col.rgb * fall * near * clamp(col.w / 40.0, 0.25, 1.0);
        }
        float self = max(c.r, max(c.g, c.b));
        light *= 1.0 - smoothstep(0.75, 0.95, self) * 0.8;          // (the blast itself is its own light)
        if (here > 0.9999) light *= 0.0;                            // (not the sky)
        c += (c * 1.7 + 0.05) * light * p.q.z;
    }
    if (p.q.y > 0.0) {
        // the stage's cast, not a flash's: weaker while the whole picture is bright (a beam filling the screen), and
        // never on what is itself bright (energy keeps its own colour)
        float al = max(avgL, 0.02);
        float k = p.q.y * (1.0 - smoothstep(0.55, 0.8, avgL)) * (1.0 - smoothstep(0.6, 0.9, max(c.r, max(c.g, c.b))));
        vec3 corr = clamp(mix(vec3(1.0), vec3(al) / max(avg, vec3(0.02)), k), vec3(0.85), vec3(1.15));
        c *= corr / max(dot(corr, kLum), 0.5); // (the cast out, the brightness kept)
    }
    if (p.g.w > 0.0) {
        vec3 a = c;
        float l = dot(c, kLum);
        float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
        float boost = p.g.x + p.g.y * (1.0 - sat);                // saturation, more where there is little of it
        float warm = smoothstep(0.08, 0.4, c.r - c.b) * step(c.g, c.r); // (orange, sand, skin)
        boost = mix(boost, 1.0 + (boost - 1.0) * 0.35, warm);
        c = mix(vec3(l), c, boost);
        c = (c - 0.5) * p.g.z + 0.5;                              // contrast about the middle grey
        float L = clamp(dot(c, kLum), 0.0, 1.0);
        c += p.h.xyz * (1.0 - L) * (1.0 - L) * 0.08;              // the shadows a touch cool
        c = mix(a, clamp(c, 0.0, 1.0), p.g.w);
    }
    if (p.z1.w > 0.0) {
        // sun rays (28 Nov 2026; light scattering as in GPU Gems 3, ch. 13): from this pixel toward the sun, the open
        // sky's light gathered, fading with the distance; where a fighter or a rock stands in front of the sky the
        // gathering is interrupted, so shafts of light and shadow fall from behind them
        vec2 d = (p.z2.xy - vUv) / 32.0, u = vUv;
        float acc = 0.0, w = 1.0;
        for (int i = 0; i < 32; i++) {
            u += d;
            vec2 q = clamp(u, vec2(0.0), p.z2.zw);
            acc += textureLod(brightTex, q, p.z1.z + 2.0).a * w;
            w *= 0.955;
        }
        acc /= 18.0;
        float sky = far_of(vUv) > 0.9999 ? 0.5 : 1.0; // (in the open sky itself: half, or it only goes white)
        c += p.z3.xyz * clamp(acc, 0.0, 1.0) * p.z1.w * 0.6 * sky * (1.0 - dot(c, kLum) * 0.5);
    }
    if (p.z1.y > 0.0) {
        // cinematic glow (28 Nov 2026): what is brighter than the threshold (bright.frag) spread wide in four sizes,
        // the near ones strongest, laid over like light on film (a screen blend: never past white)
        float b0 = p.z1.z;
        vec3 bl = glow_at(vUv, b0 + 1.0) * 0.20 + glow_at(vUv, b0 + 2.0) * 0.28 + glow_at(vUv, b0 + 3.0) * 0.28 +
                  glow_at(vUv, b0 + 4.0) * 0.24;
        bl = clamp(bl * p.z1.y, 0.0, 1.0);
        c = 1.0 - (1.0 - clamp(c, 0.0, 1.0)) * (1.0 - bl);
    }
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
