#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in float fragLight;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float brightness; // light level where the player stands (day/night)

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a < 0.1) discard; // e.g. empty parts of a skin's sleeve layer
    finalColor = vec4(texel.rgb * fragColor.rgb * colDiffuse.rgb * fragLight * brightness, 1.0);
}
