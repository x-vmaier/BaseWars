#version 330
// Single-pass hover outline: finds the sprite's silhouette edge by probing neighbouring texels' alpha.
// No duplicated draws; the outline pulses gently. Samples outside the current animation frame are ignored
// so neighbouring frames of a sprite sheet never bleed in.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 texelSize;      // 1 / texture size
uniform vec4 uvRect;         // current frame in uv space: (u0, v0, u1, v1)
uniform vec4 outlineColor;
uniform float thickness;     // in texels
uniform float time;

float alphaAt(vec2 uv) {
    if (uv.x < uvRect.x || uv.y < uvRect.y || uv.x > uvRect.z || uv.y > uvRect.w) return 0.0;
    return texture(texture0, uv).a;
}

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    float neighbour = 0.0;
    for (int ring = 1; ring <= 2; ring++) {
        float r = thickness * float(ring) * 0.5;
        for (int i = 0; i < 12; i++) {
            float a = 6.2831853 * float(i) / 12.0;
            neighbour = max(neighbour, alphaAt(fragTexCoord + vec2(cos(a), sin(a)) * texelSize * r));
        }
    }
    float edge = clamp(neighbour - texel.a, 0.0, 1.0);
    float pulse = 0.8 + 0.2 * sin(time * 6.0);
    vec4 base = texel * colDiffuse * fragColor;
    vec4 line = vec4(outlineColor.rgb, outlineColor.a * pulse * edge);
    // premultiplied-style composite of the outline underneath the sprite
    float outA = base.a + line.a * (1.0 - base.a);
    vec3 rgb = outA > 0.0 ? (base.rgb * base.a + line.rgb * line.a * (1.0 - base.a)) / outA : vec3(0.0);
    finalColor = vec4(rgb, outA);
}
