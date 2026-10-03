#version 330

in vec2 fragTexCoord;
in vec4 fragColor; // tint x light, set per particle
in vec3 fragWorldPos;

uniform sampler2D texture0;

// Same fog as the terrain (chunk.fs).
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float fogDensity;

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a < 0.1) discard;
    vec3 color = texel.rgb * fragColor.rgb;

    float dist = length(fragWorldPos - cameraPos);
    float visibility = (fogDensity > 0.0)
        ? exp(-fogDensity * dist)
        : clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);

    finalColor = vec4(mix(fogColor, color, visibility), 1.0);
}
