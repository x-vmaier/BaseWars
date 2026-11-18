#version 330
// Bloom pass 2: separable 9-tap gaussian. direction = (1/width, 0) or (0, 1/height).
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 direction;

void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 acc = texture(texture0, fragTexCoord).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        vec2 o = direction * float(i) * 1.6;
        acc += texture(texture0, fragTexCoord + o).rgb * w[i];
        acc += texture(texture0, fragTexCoord - o).rgb * w[i];
    }
    finalColor = vec4(acc, 1.0);
}
