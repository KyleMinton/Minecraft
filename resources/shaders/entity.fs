#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in float fragLight;
in vec3 fragWorldPos;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Same fog and daylight as the terrain (chunk.fs), set each frame by the sky.
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float fogDensity; // > 0 switches to exponential fog (underwater)
uniform float brightness;

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a < 0.1) discard; // e.g. the gaps in leaves
    vec3 color = texel.rgb * fragColor.rgb * colDiffuse.rgb * fragLight * brightness;

    float dist = length(fragWorldPos - cameraPos);
    float visibility = (fogDensity > 0.0)
        ? exp(-fogDensity * dist)
        : clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);

    finalColor = vec4(mix(fogColor, color, visibility), 1.0);
}
