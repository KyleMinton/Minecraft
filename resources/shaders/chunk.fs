#version 330

in vec2 fragTexCoord;   // tiled faces: world-anchored UV, 1 unit per block; others: the atlas UV itself
in vec2 fragTileOrigin; // tiled faces: this face's tile origin in the atlas; others: (-1, -1)
in vec4 fragColor;
in vec3 fragWorldPos;
in vec2 fragLight;      // sky light, block light

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float tileSize;        // one atlas tile's width/height in UV space
uniform float skyDarken;       // light levels night takes off sky light (Minecraft's skylightSubtracted)
uniform float translucentPass; // 1 while drawing water, ice and portals

// Minecraft's fog, set each frame by the sky.
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogStart;   // linear fog (normal)
uniform float fogEnd;
uniform float fogDensity; // > 0 switches to exponential fog (underwater)

out vec4 finalColor;

// Beta's light table: each level down is dimmer by a growing step, never below 5%.
float Brightness(float level)
{
    float f = 1.0 - clamp(level, 0.0, 15.0) / 15.0;
    return (1.0 - f) / (f * 3.0 + 1.0) * 0.95 + 0.05;
}

void main()
{
    vec2 atlasUV = fragTileOrigin.x < 0.0 ? fragTexCoord : fragTileOrigin + fract(fragTexCoord) * tileSize;
    vec4 texelColor = texture(texture0, atlasUV);
    if (translucentPass < 0.5) {
        if (texelColor.a < 0.5) discard; // leaves, glass, plants: cut out
    } else if (texelColor.a < 0.004) {
        discard;
    }

    float level = max(fragLight.x - skyDarken, fragLight.y);
    vec3 color = texelColor.rgb * colDiffuse.rgb * fragColor.rgb * Brightness(level);

    float dist = length(fragWorldPos - cameraPos);
    float visibility = (fogDensity > 0.0)
        ? exp(-fogDensity * dist)
        : clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);

    float alpha = translucentPass > 0.5 ? texelColor.a : 1.0;
    finalColor = vec4(mix(fogColor, color, visibility), alpha);
}
