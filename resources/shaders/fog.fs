#version 330

in vec3 fragWorldPos;
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float fogDensity; // > 0 switches to exponential fog (underwater)

out vec4 finalColor;

void main()
{
    vec4 color = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (color.a < 0.1) discard; // Minecraft's alpha test; clouds.png marks empty sky with alpha 1

    float dist = length(fragWorldPos - cameraPos);
    float visibility = (fogDensity > 0.0)
        ? exp(-fogDensity * dist)
        : clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);

    finalColor = vec4(mix(fogColor, color.rgb, visibility), color.a);
}
