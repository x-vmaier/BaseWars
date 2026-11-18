#version 330
// Bloom pass 1: soft-knee bright extraction on the (half-resolution) scene.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float threshold;

void main() {
    vec3 c = pow(texture(texture0, fragTexCoord).rgb, vec3(2.2));
    float l = max(c.r, max(c.g, c.b));
    float knee = threshold * 0.5;
    float soft = clamp(l - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float w = max(soft, l - threshold) / max(l, 1e-4);
    finalColor = vec4(c * w, 1.0);
}
