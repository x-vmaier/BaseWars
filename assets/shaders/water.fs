#version 330
// Animated ocean: flat water colour + moving caustic streaks + expanding ripple rings (ships, splashes, clicks).
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;      // "Water Background color" tile
uniform vec4 colDiffuse;
uniform vec2 resolution;
uniform vec2 camTarget;          // world pixels
uniform float zoom;
uniform float time;
uniform vec3 ripples[8];         // xy = world pixel position, z = age in seconds (<0 = unused)

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

void main() {
    vec2 frag = vec2(gl_FragCoord.x, resolution.y - gl_FragCoord.y);
    vec2 world = camTarget + (frag - resolution * 0.5) / zoom;
    vec3 base = texture(texture0, vec2(0.5)).rgb;

    vec2 p = world / 64.0;
    float w1 = noise(p * 1.6 + vec2(time * 0.25, time * 0.18));
    float w2 = noise(p * 2.7 - vec2(time * 0.2, time * 0.31));
    float caustic = smoothstep(0.62, 0.9, (w1 + w2) * 0.5 + 0.12 * sin(p.x * 3.0 + time));
    float shade = (noise(p * 0.7 + time * 0.05) - 0.5) * 0.10;
    vec3 col = base * (1.0 + shade) + vec3(0.55, 0.85, 0.95) * caustic * 0.16;

    float ring = 0.0;
    for (int i = 0; i < 8; i++) {
        if (ripples[i].z < 0.0) continue;
        float d = length(world - ripples[i].xy);
        float r = ripples[i].z * 90.0;
        float band = smoothstep(14.0, 0.0, abs(d - r)) * smoothstep(2.2, 0.0, ripples[i].z);
        ring += band;
    }
    col += vec3(0.8, 0.95, 1.0) * ring * 0.35;
    finalColor = vec4(col, 1.0) * colDiffuse * fragColor;
}
