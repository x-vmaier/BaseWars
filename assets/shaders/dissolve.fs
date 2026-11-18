#version 330
// Burn / ash dissolve for destroyed units and buildings: noise-thresholded alpha, glowing ember edge, charring.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 texSize;
uniform vec4 uvRect;
uniform float progress;      // 0 intact .. 1 gone
uniform float seed;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + seed) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

void main() {
    vec4 c = texture(texture0, fragTexCoord);
    vec2 local = (fragTexCoord - uvRect.xy) / (uvRect.zw - uvRect.xy);
    // burn front sweeps from the bottom up, perturbed by pixelated noise
    vec2 px = floor(fragTexCoord * texSize / 3.0);
    float n = noise(px * 0.35) * 0.6 + hash(px) * 0.4;
    float front = (1.0 - local.y) * 0.45 + n * 0.55;
    float t = progress * 1.25 - 0.1;
    if (front < t) discard;
    float edge = 1.0 - smoothstep(0.0, 0.12, front - t);
    vec3 col = c.rgb * mix(1.0, 0.25, clamp(progress * 1.4, 0.0, 1.0));
    vec3 ember = mix(vec3(1.0, 0.35, 0.05), vec3(1.0, 0.85, 0.3), n);
    col = mix(col, ember, edge);
    float a = c.a * (1.0 - smoothstep(0.85, 1.0, progress));
    finalColor = vec4(col, a) * colDiffuse * fragColor;
}
