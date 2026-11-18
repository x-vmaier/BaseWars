#version 330
// Final scene pass (the UI is drawn afterwards and is never shaded):
//   linearise -> shake / chromatic aberration -> day-night + warm light pools -> cold / warmth / blizzard
//   -> bloom -> exposure -> AgX tone mapping -> colour grading -> vignette -> damage flash -> sRGB.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;      // scene (sRGB, as rendered)
uniform sampler2D bloomTex;      // blurred bright pass, linear
uniform vec4 colDiffuse;
uniform vec2 resolution;
uniform float time;
uniform float shake;
uniform float damage;
uniform float daylight;          // 0 night .. 1 noon
uniform float cold;
uniform float warmth;
uniform float blizzard;
uniform float bloomStrength;
uniform float exposure;
uniform vec3 lights[16];         // xy = screen pixel, z = radius px
uniform int lightCount;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

// ---- AgX (Troy Sobotka / Benjamin Wrensch polynomial approximation) ----
vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 agx(vec3 v) {
    const mat3 inset = mat3(0.842479062253094, 0.0784335999999992, 0.0792237451477643,
                            0.0423282422610123, 0.878468636469772, 0.0791661274605434,
                            0.0423756549057051, 0.0784336, 0.879142973793104);
    const float minEv = -12.47393, maxEv = 4.026069;
    v = inset * v;
    v = clamp(log2(max(v, vec3(1e-10))), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    return agxContrast(v);
}
vec3 agxLook(vec3 v, float sat, float pw) {
    float l = dot(v, vec3(0.2126, 0.7152, 0.0722));
    v = pow(max(v, vec3(0.0)), vec3(pw));
    return l + sat * (v - l);
}
vec3 agxEotf(vec3 v) {
    const mat3 outset = mat3(1.19687900512017, -0.0980208811401368, -0.0990297440797205,
                             -0.0528968517574562, 1.15190312990417, -0.0989611768448433,
                             -0.0529716355144438, -0.0980434501171241, 1.15107367264116);
    return pow(max(outset * v, vec3(0.0)), vec3(2.2));
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 px = vec2(gl_FragCoord.x, resolution.y - gl_FragCoord.y);
    uv += shake * vec2(sin(time * 71.0), cos(time * 59.0)) / resolution;
    uv += blizzard * 0.0015 * vec2(sin(uv.y * 40.0 + time * 6.0), 0.0);

    vec2 ca = (uv - 0.5) * damage * 0.012;
    vec3 col = vec3(texture(texture0, uv + ca).r, texture(texture0, uv).g, texture(texture0, uv - ca).b);
    col = pow(col, vec3(2.2));                                   // to linear light
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));

    // ---- lighting: day/night ambient + warm light pools ----
    float night = 1.0 - daylight;
    vec3 ambient = mix(vec3(1.0), vec3(0.16, 0.22, 0.46), night * 0.9);      // moonlight
    float dusk = smoothstep(0.0, 0.5, daylight) * (1.0 - smoothstep(0.5, 0.85, daylight));
    ambient = mix(ambient, ambient * vec3(1.25, 0.82, 0.58), dusk * 0.6);   // golden hour
    vec3 pool = vec3(0.0);
    float lit = 0.0;
    for (int i = 0; i < 16; i++) {
        if (i >= lightCount) break;
        float d = length(px - lights[i].xy) / lights[i].z;
        float g = exp(-d * d * 2.4);
        float flick = 0.88 + 0.12 * sin(time * 9.0 + float(i) * 3.1) * sin(time * 5.3 + float(i));
        pool += vec3(1.0, 0.55, 0.2) * g * flick;
        lit = max(lit, g);
    }
    vec3 lightMul = ambient + pool * (0.25 + 1.5 * night);
    col *= lightMul;
    col += pool * vec3(1.0, 0.5, 0.15) * lum * 0.8 * night;         // lit surfaces bloom orange

    // ---- weather ----
    vec2 c = uv - 0.5;
    float r = length(c * vec2(resolution.x / resolution.y, 1.0));
    col = mix(col, vec3(lum) * vec3(0.75, 0.9, 1.15), cold * 0.35);
    float frost = smoothstep(0.38, 0.85, r) * cold * (0.45 + 0.5 * noise(px * 0.05));
    col = mix(col, vec3(0.75, 0.88, 1.0), clamp(frost, 0.0, 0.7));
    col = mix(col, col * vec3(1.14, 0.95, 0.76), warmth * 0.7);
    col += vec3(0.10, 0.035, 0.0) * warmth * smoothstep(0.2, 1.0, uv.y);
    if (blizzard > 0.01) {
        float s = 0.0;
        for (int layer = 0; layer < 2; layer++) {
            float sc = layer == 0 ? 90.0 : 160.0;
            vec2 q = vec2(px.x + px.y * 0.35, px.y) / sc;
            q.x -= time * (layer == 0 ? 3.2 : 5.0);
            q.y -= time * (layer == 0 ? 1.1 : 1.8);
            vec2 cell = floor(q), f = fract(q);
            float h = hash(cell + float(layer) * 17.0);
            float streak = smoothstep(0.35, 0.0, abs(f.y - h)) * smoothstep(0.0, 0.5, f.x) * smoothstep(1.0, 0.5, f.x);
            s += streak * step(0.55, hash(cell * 1.7 + 3.0));
        }
        col = mix(col, vec3(0.85, 0.92, 1.0), clamp(s * blizzard * 0.7, 0.0, 0.8));
        col = mix(col, vec3(0.6, 0.7, 0.82), blizzard * 0.18);
    }

    // ---- bloom (added in linear HDR, before tone mapping) ----
    vec3 bloom = texture(bloomTex, uv).rgb;
    col += bloom * (bloomStrength + 0.6 * night * bloomStrength);

    // ---- exposure + AgX ----
    col *= exposure * mix(1.0, 1.35, night);
    vec3 mapped = agx(col);
    mapped = agxLook(mapped, 1.12, 1.04);                              // slightly punchy look
    col = agxEotf(mapped);

    // ---- colour grading (linear): warm highlights / cool shadows, shifting with the time of day ----
    float l2 = dot(col, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadowTint = mix(vec3(0.94, 1.0, 1.08), vec3(0.88, 0.96, 1.18), night);
    vec3 highTint = mix(vec3(1.06, 1.0, 0.92), vec3(1.0, 0.95, 0.9), night);
    col *= mix(shadowTint, highTint, smoothstep(0.05, 0.6, l2));
    col = mix(vec3(l2), col, 1.08);                                    // saturation
    col = (col - 0.18) * 1.06 + 0.18;                                  // contrast around mid grey

    // ---- vignette ----
    float vig = smoothstep(0.95, 0.25, r);
    col *= mix(0.62, 1.0, vig);

    // ---- damage flash ----
    col = mix(col, vec3(0.55, 0.02, 0.02), damage * (0.2 + 0.6 * smoothstep(0.25, 0.85, r)));

    finalColor = vec4(pow(max(col, vec3(0.0)), vec3(1.0 / 2.2)), 1.0) * colDiffuse * fragColor;
}
