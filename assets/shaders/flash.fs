#version 330
// Hit flash: blends the sprite's colour towards white (alpha untouched), `amount` 0 = normal .. 1 = pure white.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float amount;

void main() {
    vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    finalColor = vec4(mix(c.rgb, vec3(1.0), amount), c.a);
}
